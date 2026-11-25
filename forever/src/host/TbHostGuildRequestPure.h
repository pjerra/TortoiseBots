#pragma once

// The other direction of guild participation (TBF #30): a guildless player whispers a bot of a bot-run guild and asks
// for an invite. Core-free: every fact is passed in, so forever/tools/test_guild_request_pure.cpp proves the phrases,
// the order of the checks and the rate limit without the server. The driver that fills the facts is
// host/TbHostGuildJoin.cpp (HandleJoinRequest).
//
// A request is answered only in a guild whose leader is a pool character (a player's own guild led by an owned alt is
// not bot-run) and only when the guild has room below the same 1000-member limit free bots use for themselves
// (host/TbHostGuildJoinPure.h). One handled request per asker per cooldown, so a player who repeats the line is not
// whispered at every time.

#include "host/TbHostGuildInvitePure.h"   // InviteOutcome: the invite seam's refusal codes
#include "host/TbHostGuildJoinPure.h"   // GuildMemberLimit: one limit for accepting and for inviting

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guildjoin
{

// One handled request per asker; a repeat inside the window is handled silently (no whisper).
inline constexpr std::uint32_t kRequestCooldownMs = 20000;
// A cooldown entry older than this is pruned when another request is recorded, so the map does not grow with players.
inline constexpr std::uint32_t kRequestPruneMs = 60000;

enum class RequestVerdict
{
    NotHandled,     // the key is off or the line is not one of the phrases: the line goes on to the inbox
    Silent,         // a handled request inside the asker's cooldown: no answer
    NotInGuild,     // the whispered bot is in no guild
    NotBotRun,      // the guild's leader is not a pool character
    AlreadyInGuild, // the asker is in a guild
    WrongFaction,   // other side, two-side guilds off
    Full,           // the guild reached the member limit
    NoOfficer,      // no online bot of the guild may invite
    Invite,
};

struct RequestFacts
{
    bool featureOn = false;      // TortoiseBots.World.GuildAcceptPlayers and TortoiseBots.World.Guilds
    bool isRequest = false;    // IsJoinRequest(text)
    bool rateLimited = false;  // a handled request from this asker inside kRequestCooldownMs
    bool botInGuild = false;
    bool guildBotRun = false;  // the guild leader's account is a pool account
    bool askerInGuild = false;
    bool sameTeam = true;
    bool twoSideGuilds = false;
    std::uint32_t memberCount = 0;
    bool inviterFound = false;
};

// The order is the spec's: off or not a phrase (not ours) -> cooldown (silent) -> the bot's guild, its kind, the
// asker's own guild, the sides, the member limit, an inviter.
inline RequestVerdict DecideRequest(RequestFacts const& facts)
{
    if (!facts.featureOn || !facts.isRequest)
        return RequestVerdict::NotHandled;
    if (facts.rateLimited)
        return RequestVerdict::Silent;
    if (!facts.botInGuild)
        return RequestVerdict::NotInGuild;
    if (!facts.guildBotRun)
        return RequestVerdict::NotBotRun;
    if (facts.askerInGuild)
        return RequestVerdict::AlreadyInGuild;
    if (!facts.sameTeam && !facts.twoSideGuilds)
        return RequestVerdict::WrongFaction;
    if (facts.memberCount >= GuildMemberLimit)
        return RequestVerdict::Full;
    if (!facts.inviterFound)
        return RequestVerdict::NoOfficer;
    return RequestVerdict::Invite;
}

// Wrap-safe on the core's millisecond counter: 0 means "never asked".
inline bool RequestCooldownPassed(std::uint32_t lastMs, std::uint32_t nowMs)
{
    if (lastMs == 0)
        return true;
    return std::uint32_t(nowMs - lastMs) >= kRequestCooldownMs;
}

// The per-asker record of handled requests. Admit answers false inside the asker's cooldown and records nothing;
// otherwise it drops every entry older than kRequestPruneMs and records the asker, so the map is bounded by the
// players who asked in the last minute, not by everyone who ever did.
class RequestCooldowns
{
public:
    bool Admit(std::uint32_t askerKey, std::uint32_t nowMs)
    {
        auto const seen = _times.find(askerKey);
        if (seen != _times.end() && !RequestCooldownPassed(seen->second, nowMs))
            return false;
        for (auto it = _times.begin(); it != _times.end();)
        {
            if (std::uint32_t(nowMs - it->second) >= kRequestPruneMs)
                it = _times.erase(it);
            else
                ++it;
        }
        _times[askerKey] = nowMs;
        return true;
    }

    std::size_t Size() const { return _times.size(); }

private:
    std::unordered_map<std::uint32_t, std::uint32_t> _times;
};

// One guild member as the inviter choice sees it.
struct InviterCandidate
{
    bool preferred = false; // the whispered bot itself: it invites first when its rank may
    bool bot = false;       // a pool bot still playing free: a player's owned alt never speaks for the guild
    bool online = false;
    bool canInvite = false;
    std::uint8_t rank = 0;
};

// The inviter: the whispered bot when it may invite, else the eligible candidate with the lowest rank id (the guild
// master's is the lowest, as the core orders ranks). -1 when no candidate may invite. An owned alt and an offline bot
// are never candidates.
inline int PickInviterIndex(std::vector<InviterCandidate> const& candidates)
{
    int best = -1;
    bool bestPreferred = false;
    std::uint8_t bestRank = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        InviterCandidate const& candidate = candidates[i];
        if (!candidate.bot || !candidate.online || !candidate.canInvite)
            continue;
        if (best >= 0)
        {
            if (bestPreferred && !candidate.preferred)
                continue;
            if (bestPreferred == candidate.preferred && candidate.rank >= bestRank)
                continue;
        }
        best = int(i);
        bestPreferred = candidate.preferred;
        bestRank = candidate.rank;
    }
    return best;
}

// The log token (`result=<code>`), and "invited" for the one verdict that sends the core's guild invite.
inline char const* RequestCode(RequestVerdict verdict)
{
    switch (verdict)
    {
        case RequestVerdict::NotHandled: return "not-handled";
        case RequestVerdict::Silent: return "silent";
        case RequestVerdict::NotInGuild: return "not-in-guild";
        case RequestVerdict::NotBotRun: return "not-bot-run";
        case RequestVerdict::AlreadyInGuild: return "already-in-guild";
        case RequestVerdict::WrongFaction: return "wrong-faction";
        case RequestVerdict::Full: return "full";
        case RequestVerdict::NoOfficer: return "no-officer";
        case RequestVerdict::Invite: return "invited";
    }
    return "not-handled";
}

// The answer when the seam refuses the invite after every policy check passed. Each code the core can report has its
// own line, so the asker is never told "no officer" for a refusal that had nothing to do with an officer; anything
// else gets one generic line. Empty for Ok (the caller has something nicer to say then).
inline std::string InviteRefusalAnswer(tbhost::guild::InviteOutcome outcome, std::string const& guildName,
    char const* side, std::string const& inviterName)
{
    using tbhost::guild::InviteOutcome;
    switch (outcome)
    {
        case InviteOutcome::Ok:
            return std::string();
        case InviteOutcome::TargetIgnoring:
            return "You are ignoring " + inviterName + ", so the invite cannot reach you.";
        case InviteOutcome::TargetAlreadyInvited:
            return "You already have a guild invite; accept or decline it first.";
        case InviteOutcome::TargetInGuild:
            return "You are already in a guild.";
        case InviteOutcome::WrongFaction:
            return std::string("We only take ") + side + " members.";
        case InviteOutcome::NoInviteRight:
            return "No one in " + guildName + " can invite you right now, try later.";
        case InviteOutcome::TargetNotFound:
            return "I cannot find you right now.";
        case InviteOutcome::NotInWorld:
        case InviteOutcome::NotInGuild:
        case InviteOutcome::EmptyTarget:
        case InviteOutcome::CoreRefused:
            break;
    }
    return "I could not invite you right now.";
}

namespace detail
{

inline char LowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

// Whole line: ASCII-lowercased, tabs as spaces, one space between words, none at the ends.
inline std::string NormalizeRequestLine(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool gap = false;
    for (char const raw : text)
    {
        char const c = LowerAscii(raw);
        if (c == ' ' || c == '\t')
        {
            gap = !out.empty();
            continue;
        }
        if (gap)
        {
            out.push_back(' ');
            gap = false;
        }
        out.push_back(c);
    }
    return out;
}

inline void StripTrailingMarks(std::string& line)
{
    while (!line.empty() && (line.back() == '?' || line.back() == '!' || line.back() == '.'))
        line.pop_back();
}

inline bool EndsWith(std::string const& line, std::string_view tail)
{
    return line.size() >= tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace detail

// The request phrases, whole line after normalisation. English only (#43 carries other languages). No phrase starts
// with `guild ` plus one of ParseGuildWhisper's verbs, so the owner's management whispers never come here.
inline bool IsJoinRequest(std::string_view text)
{
    std::string line = detail::NormalizeRequestLine(text);
    detail::StripTrailingMarks(line);
    if (detail::EndsWith(line, " please"))
    {
        line.erase(line.size() - 7);
        detail::StripTrailingMarks(line);
    }
    else if (detail::EndsWith(line, " pls"))
    {
        line.erase(line.size() - 4);
        detail::StripTrailingMarks(line);
    }

    static constexpr char const* kPhrases[] = {
        "ginv",
        "ginvite",
        "guild inv",
        "can i join",
        "can i join your guild",
        "can i join the guild",
        "invite me",
        "invite me to your guild",
        "invite me to the guild",
        "inv me to guild",
    };
    for (char const* phrase : kPhrases)
        if (line == phrase)
            return true;
    return false;
}

} // namespace guildjoin
} // namespace tbhost
