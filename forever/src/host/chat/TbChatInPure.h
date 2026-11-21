// S1-R2 (chat commands in): the routing and ownership decision of one incoming chat line, and the inbox every bot reads its
// commands from. Core-free (the C++ standard library only), so forever/tools/test_chat_in.cpp compiles it with a bare
// `g++ -std=c++17 -I.` and proves exactly what the world thread runs.
//
// A chat line reaches the hook (host/chat/TbChatIn.cpp) as PlayerScript::OnChat: a whisper names its receiver, a party or party-leader
// line names the group. The hook only copies the guids and the text into an Arrival (it may run off the world thread, or in a
// player's logout), and the world thread resolves the receiver and the sender and decides. Judge answers one message for one bot;
// TbChatIn.cpp fills the facts from the core (kind, language, whether the sender is a real player, who may command this bot, whether
// the bot is in the sender's own subgroup of the group, whether it has an engine brain).
//
// The kind numbers are the core's ChatMsg values (SharedDefines.h): CHAT_MSG_PARTY = 0x02, CHAT_MSG_PARTY_LEADER = 0x31, CHAT_MSG_WHISPER = 0x07;
// the language numbers are LANG_ADDON = 183 and LANG_ADDON_LOGGED = 184. TbChatIn.cpp static_asserts them against the core's enum, so a core
// that moved them fails the build instead of the decision going quietly wrong. The whisper hook and the party hook both arrive here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace chatin
{
// What one line is delivered to the engine as: a whisper, or a party line (a leader's line included, stored as PARTY).
enum KindNumber : unsigned
{
    KindParty = 0x02,
    KindPartyLeader = 0x31,
    KindWhisper = 0x07,
};

enum LangNumber : unsigned
{
    LangAddon = 183,
    LangAddonLogged = 184,
};

// The longest line a bot is given: the legacy chat path answered 1 to 255 bytes (PlayerbotAIPackets.cpp), while the core itself accepts 511.
constexpr std::size_t MaxTextBytes = 255;

enum class Code
{
    Accepted,
    Kind,            // not a whisper, a party or a party-leader line (say, yell, raid, guild, channel...)
    Addon,           // LANG_ADDON / LANG_ADDON_LOGGED: addon traffic never commands a bot
    Self,            // the sender is the bot itself: its own reply re-enters the hook
    NotReal,         // the sender has no network session (a bot, a creature, a console character)
    Length,          // empty, or longer than MaxTextBytes
    NotAuthorized,   // not the bot's master, not its commander, not a member of its group
    OtherSubGroup,   // a party line, and the bot is in another subgroup of the raid than the sender
    NoEngine,        // the receiver is not a bot with an engine brain
    Dropped,         // accepted, then thrown away on the way to the engine (sender gone, authority lost, bot not acting)
};

// The refusal words the state block, `.tb hear` and the log use; `accepted` is the word for a line that was queued, `dropped`
// for one that was queued and never ran.
inline char const* CodeName(Code code)
{
    switch (code)
    {
        case Code::Accepted: return "accepted";
        case Code::Kind: return "kind";
        case Code::Addon: return "addon";
        case Code::Self: return "self";
        case Code::NotReal: return "not-real";
        case Code::Length: return "length";
        case Code::NotAuthorized: return "not-authorized";
        case Code::OtherSubGroup: return "other-subgroup";
        case Code::NoEngine: return "no-engine";
        case Code::Dropped: return "dropped";
    }
    return "kind";
}

struct Verdict
{
    Code code = Code::Kind;
    unsigned kind = 0;                 // the kind the line is delivered as: KindWhisper or KindParty
    bool accepted() const { return code == Code::Accepted; }
};

// Everything the decision reads. `injected` marks the line `.tb hear` makes up: a made-up sender has no session, so only the
// `not-real` rule is skipped for it (nothing else is: authorization, length, kind and the addon rule all still apply).
struct ChatInFacts
{
    unsigned kind = 0;
    unsigned lang = 0;
    bool senderIsSelf = false;
    bool senderReal = false;
    bool authorized = false;
    bool sameSubGroup = true;          // a whisper has no subgroup; a party line has the sender's
    bool engine = false;
    bool injected = false;
    std::string_view text;
};

// The decision, in the order the refusal codes are listed: kind, addon, self, not-real, length, not-authorized, other-subgroup,
// no-engine.
inline Verdict Judge(ChatInFacts const& facts)
{
    Verdict verdict;
    if (facts.kind != KindWhisper && facts.kind != KindParty && facts.kind != KindPartyLeader)
    {
        verdict.code = Code::Kind;
        return verdict;
    }
    if (facts.lang == LangAddon || facts.lang == LangAddonLogged)
    {
        verdict.code = Code::Addon;
        return verdict;
    }
    if (facts.senderIsSelf)
    {
        verdict.code = Code::Self;
        return verdict;
    }
    if (!facts.senderReal && !facts.injected)
    {
        verdict.code = Code::NotReal;
        return verdict;
    }
    if (facts.text.empty() || facts.text.size() > MaxTextBytes)
    {
        verdict.code = Code::Length;
        return verdict;
    }
    if (!facts.authorized)
    {
        verdict.code = Code::NotAuthorized;
        return verdict;
    }
    // The core sends a party line only to the sender's own subgroup (ChatHandler.cpp: `BroadcastPacket(..., GetMemberGroup(sender))`),
    // so a raid member's `/p` is not a raid-wide order on the Turtle side either.
    if (facts.kind != KindWhisper && !facts.sameSubGroup)
    {
        verdict.code = Code::OtherSubGroup;
        return verdict;
    }
    if (!facts.engine)
    {
        verdict.code = Code::NoEngine;
        return verdict;
    }
    verdict.code = Code::Accepted;
    verdict.kind = facts.kind == KindWhisper ? KindWhisper : KindParty;
    return verdict;
}

// One queued line. The sender is kept as the two words of its guid (ObjectGuid is a core type and this header has none); the word
// pair is what ObjectGuid::SetRawValue(high, low) takes.
struct Envelope
{
    std::uint64_t senderHigh = 0;      // ObjectGuid::GetRawValue(1)
    std::uint64_t senderLow = 0;       // ObjectGuid::GetRawValue(0)
    unsigned kind = 0;
    unsigned lang = 0;
    std::string text;
    bool injected = false;
};

constexpr std::size_t InboxCapacity = 16;

// One bot's inbox: at most InboxCapacity lines, in arrival order; a line pushed into a full inbox drops the OLDEST one and counts
// it. Counters are what `.tb state` prints: accepted, refused (by the filter), dropped (by this ring).
class Inbox
{
public:
    bool Push(Envelope const& line)
    {
        bool dropped = false;
        if (size_ == InboxCapacity)
        {
            head_ = (head_ + 1) % InboxCapacity;
            --size_;
            ++dropped_;
            dropped = true;
        }
        lines_[(head_ + size_) % InboxCapacity] = line;
        ++size_;
        return dropped;
    }

    bool Pop(Envelope& out)
    {
        if (size_ == 0)
            return false;
        out = lines_[head_];
        lines_[head_] = Envelope();
        head_ = (head_ + 1) % InboxCapacity;
        --size_;
        return true;
    }

    std::size_t Size() const { return size_; }

    // One verdict from the filter: the counters and the last word `.tb state` shows.
    void Note(Code code)
    {
        if (code == Code::Accepted)
            ++accepted_;
        else
            ++refused_;
        last_ = code;
        hasLast_ = true;
    }

    // A queued line thrown away on the way to the engine: counted, and the last word says so, so `.tb state` never shows
    // `accepted` for a command that never ran.
    void Drop()
    {
        ++dropped_;
        last_ = Code::Dropped;
        hasLast_ = true;
    }

    std::uint64_t Accepted() const { return accepted_; }
    std::uint64_t Refused() const { return refused_; }
    std::uint64_t Dropped() const { return dropped_; }
    bool HasLast() const { return hasLast_; }
    Code Last() const { return last_; }

private:
    Envelope lines_[InboxCapacity];
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t accepted_ = 0;
    std::uint64_t refused_ = 0;
    std::uint64_t dropped_ = 0;
    Code last_ = Code::Kind;
    bool hasLast_ = false;
};

// One line as the hook received it, before any world object is resolved: guids, the kind and language, and a copy of the text.
// The hook may run off the world thread, so it copies only these immutable values; the receivers, the sender and the authorization
// are read on the world thread from the guids.
struct Arrival
{
    bool party = false;                // the group form (target is the group) or the whisper form (target is the receiver)
    std::uint64_t senderHigh = 0;      // ObjectGuid::GetRawValue(1)
    std::uint64_t senderLow = 0;       // ObjectGuid::GetRawValue(0)
    std::uint64_t targetHigh = 0;
    std::uint64_t targetLow = 0;
    unsigned kind = 0;
    unsigned lang = 0;
    std::string text;
};

// How many lines may wait between the hook and the world thread's next drain: a flood (a script whispering in a loop) drops the
// oldest line, and no line is counted as accepted before it is judged.
constexpr std::size_t IngressCapacity = 64;

class Ingress
{
public:
    bool Push(Arrival const& line)
    {
        bool dropped = false;
        if (size_ == IngressCapacity)
        {
            head_ = (head_ + 1) % IngressCapacity;
            --size_;
            ++dropped_;
            dropped = true;
        }
        lines_[(head_ + size_) % IngressCapacity] = line;
        ++size_;
        return dropped;
    }

    bool Pop(Arrival& out)
    {
        if (size_ == 0)
            return false;
        out = lines_[head_];
        lines_[head_] = Arrival();
        head_ = (head_ + 1) % IngressCapacity;
        --size_;
        return true;
    }

    std::size_t Size() const { return size_; }
    std::uint64_t Dropped() const { return dropped_; }

private:
    Arrival lines_[IngressCapacity];
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t dropped_ = 0;
};
}
}
