// Commander port, package P2 (binding lifecycle and persistence): the PURE half. FOREVER-COMMANDER-PORT-SPEC.md section 5 (P2), section 7 (P2 tests)
// and decisions D2 (character guids only) and D3 (opt-in persistence).
//
// Standard library and the shared commander release rule only (runtime/RemoteCommanderPolicy.h: plain data, no core include). Tested standalone by
// forever/tools/cmd_binding_life_check.cpp (g++ -std=c++17 -I. forever/tools/cmd_binding_life_check.cpp).
//
// What it decides:
//  * the persisted binding row: five fields only (commander, lead, members, flags, bound_at). A character guid is the whole key (D2); NO group number
//    is written, because the core reuses its stored group ids (GroupMgr::FreeGroupDbStoreId) and a stale id would bind another party (section 7, the
//    failing case). The live group is always found through the lead.
//  * when a standing binding ends, and why: the shared CheckBinding's verdicts (lead left, group changed, leader changed, a player joined, the group
//    became a raid or grew beyond five, the commander left an in-party binding) plus the two timers P2 adds - a lead dead past its grace and a
//    commander away past the mastered-bot grace. The timers never release sooner than their grace. A battleground bind (BG2 live row 5) is apart,
//    and per BOT: a bg bot is never stored, never a member of the record, no normal rule touches it, and it ends only with the match it was bound
//    in (`BgBook`, `CheckBg`, `BgBindEnd`); its commander may hold normal members at the same time.
//  * how a stored row is restored: only with the commander and the lead back in the world (the spec's mutation: restoring with the lead offline must
//    not happen), then meetings to join and bots to bind, online ones only - and only the members the HOST says are still this commander's
//    (`Claim`: a bot handed to another commander, or a character that is no host bot, is never taken).
#pragma once

#include "runtime/RemoteCommanderPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace life
{
namespace RC = ::TortoiseBots::RemoteCommander;

// One persisted binding. Five fields, and no other field may ever be written: a group id or a group counter here would be the failing case of spec
// section 7 (a reused database id binds the wrong party). Guids are character guid counters, the value of `characters.guid` (decision D2).
struct Stored
{
    uint64_t commander = 0;             // character guid counter
    uint64_t lead = 0;                  // character guid counter
    std::vector<uint64_t> members;      // the bots h3 bound to that commander when the row was written, the lead first
    uint32_t flags = 0;                 // Flag* below
    int64_t boundAt = 0;
};

enum Flags : uint32_t
{
    FlagInParty = 1,
    FlagRaid = 2,
    FlagPlayerLed = 4,                  // the commander himself holds the party's lead (a live fact, refreshed with the group)
    FlagSystem = 8,
};

inline bool HasFlag(Stored const& stored, uint32_t flag) { return (stored.flags & flag) != 0; }

// `1,2,4` for the flags of the binding (readable in a database shell; 0 = a plain commander outside the party).
inline uint32_t FlagsOf(bool inParty, bool raid, bool playerLed, bool system)
{
    return (inParty ? FlagInParty : 0u) | (raid ? FlagRaid : 0u) | (playerLed ? FlagPlayerLed : 0u) | (system ? FlagSystem : 0u);
}

// May a commander's order use this bot right now? Only while the binding stands, no group hook has recorded its release (the hook's
// event is drained on the next tick, and the change it recorded - a player joined, the lead left - is already true), the bot is still
// bound to this commander, and it is still a member of the lead's live party. A bot that left the party is refused before the tick.
inline bool MayCommand(bool bindingStands, bool releasePending, bool boundToCommander, bool inLeadGroup)
{
    return bindingStands && !releasePending && boundToCommander && inLeadGroup;
}

// A battleground bind's authorisation (BG2 live row 5): the same question for a bot that stands in a match, read from its own live facts. The
// 1 Hz check (CheckBg) ends a stale row, but it runs between two ticks at most: an authorisation that only read the stored member would keep
// commanding after the commander left the match (or was moved out of it) until the next tick. The check itself therefore requires both in the
// world, both in the instance the bind was made in, and both still on one team - the very facts the bind was allowed on (TbMasterBindPure.h
// IsBgBind). A leave, a transfer or a logout denies at once; the maintenance pass does the release.
struct BgCommandFacts
{
    bool botOnline = false;             // the bot is in the world
    bool commanderOnline = false;       // the commander is in the world
    bool botInInstance = false;         // the bot's live GetBattlegroundId() is the bind's instance
    bool commanderInInstance = false;   // the commander's live GetBattlegroundId() is the bind's instance
    bool sameTeam = false;              // their live GetBGTeam() are equal
};

inline bool MayCommandBg(bool boundToCommander, BgCommandFacts const& facts)
{
    return boundToCommander && facts.botOnline && facts.commanderOnline && facts.botInInstance && facts.commanderInInstance && facts.sameTeam;
}

// ---- the row text (one line per field group, no escaping: guids and numbers only) ----

inline std::string EncodeMembers(std::vector<uint64_t> const& members)
{
    std::string text;
    for (uint64_t guid : members)
    {
        if (!guid)
            continue;
        if (!text.empty())
            text += ',';
        text += std::to_string(guid);
    }
    return text;
}

inline std::vector<uint64_t> DecodeMembers(std::string const& text)
{
    std::vector<uint64_t> members;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t const comma = text.find(',', start);
        std::string const word = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!word.empty())
        {
            uint64_t guid = 0;
            bool digits = true;
            for (char c : word)
            {
                if (c < '0' || c > '9' || guid > (UINT64_MAX - 9) / 10)   // a value past 64 bits is a bad word, never a wrapped guid
                {
                    digits = false;
                    break;
                }
                guid = guid * 10 + uint64_t(c - '0');
            }
            if (digits && guid && std::find(members.begin(), members.end(), guid) == members.end())
                members.push_back(guid);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return members;
}

// The five fields of a row, checked as one: the lead is one of the members, the members are unique and non-zero, the commander is not one of them
// (a bot cannot command itself, and a row that says so would bind the commander into its own party), the flags are known. A row this build did not
// write is refused whole (do less: none is restored); nothing else may be stored, so a group number can never come back from a database.
inline bool Valid(Stored const& stored)
{
    if (!stored.commander || !stored.lead || stored.commander == stored.lead)
        return false;
    if (std::find(stored.members.begin(), stored.members.end(), stored.commander) != stored.members.end())
        return false;
    if (stored.flags & ~(FlagInParty | FlagRaid | FlagPlayerLed | FlagSystem))
        return false;
    if (stored.members.empty() || std::find(stored.members.begin(), stored.members.end(), stored.lead) == stored.members.end())
        return false;
    std::vector<uint64_t> seen;
    for (uint64_t member : stored.members)
    {
        if (!member || std::find(seen.begin(), seen.end(), member) != seen.end())
            return false;
        seen.push_back(member);
    }
    return true;
}

// ---- when a binding ends ----

enum class End : uint8_t
{
    None,
    Disabled,
    LeadLeft,           // the lead bot left the world
    LeadDead,           // the lead bot has been dead for its grace
    GroupChanged,       // the lead has no group any more, or its group holds no bound follower
    LeaderChanged,      // the lead bot no longer leads the group
    PlayerJoined,       // a real player other than the commander joined a classic (out-of-party) binding
    GroupRaid,          // the party became a raid (or a battleground group)
    GroupTooLarge,      // the group grew beyond five (forty for a raid binding)
    CommanderLeftParty, // an in-party binding: the commander is no longer a member
    CommanderAway,      // the commander has been out of the world for the mastered-bot grace
    BgBotLeft,          // a battleground bind: the bot left the match (or logged out); never a verdict of the party rules
    BgCommanderLeft,    // a battleground bind: the commander left the match (or logged out)
    BgMatchEnded,       // a battleground bind: the match it was made in is over
    BgTeamChanged,      // a battleground bind: the two are no longer on one team (a mid-match transfer)
    Unbound,            // the commander's own release; not a verdict of Decide
};

// ---- a battleground bind (BG2 live row 5) ----

// `.tb master <bot> <human>` inside one battleground instance (host/cmd/TbMasterBindPure.h: both in the same instance, on the same team) is its own
// short-lived kind: it is never stored, and the rules below do not apply to it. Inside a match the lead's group is the battleground raid, so
// groupIsBg / groupIsRaidOrBg would release it within one 1 Hz tick (group-raid), and the raid's leader is the human, not the bot (leader-changed).
// It has exactly one end: the match it was made in, when either side leaves it (the instance changes or goes to 0, which covers a leave, the end of
// the match and both logouts), or when a mid-match transfer puts them on different teams.
struct BgFacts
{
    uint32_t instance = 0;              // the instance the bind was made in (0 = none recorded: a bind with no match is over)
    uint32_t botInstance = 0;           // the bot's live GetBattlegroundId(): 0 = no longer inside a match
    uint32_t commanderInstance = 0;     // the commander's live GetBattlegroundId()
    bool botOnline = true;
    bool commanderOnline = true;
    bool sameTeam = true;               // Player::GetBGTeam() equal (read only while both are here); a transfer ends the bind
};

enum class BgEnd : uint8_t
{
    None,
    BotLeft,            // the bot left the match, or logged out
    CommanderLeft,      // the commander left the match, or logged out
    MatchEnded,         // the match is gone for both (it ended), or the record names none
    TeamChanged,        // the two stand in the instance on different teams now (a mid-match transfer)
};

// Both gone is the match ending; one gone is that one leaving. A logout ends it at once: the kind has no grace, the match is the whole life.
inline BgEnd BgBindEnd(BgFacts const& facts)
{
    if (!facts.instance)
        return BgEnd::MatchEnded;
    if (!facts.botOnline)
        return BgEnd::BotLeft;
    if (!facts.commanderOnline)
        return BgEnd::CommanderLeft;
    bool const botHere = facts.botInstance == facts.instance;
    bool const commanderHere = facts.commanderInstance == facts.instance;
    if (!botHere && !commanderHere)
        return BgEnd::MatchEnded;
    if (!botHere)
        return BgEnd::BotLeft;
    if (!commanderHere)
        return BgEnd::CommanderLeft;
    if (!facts.sameTeam)
        return BgEnd::TeamChanged;
    return BgEnd::None;
}

// The verdict's End, so Decide and the host read one value. No normal end is ever produced for a battleground bind.
inline End BgEndToEnd(BgEnd end)
{
    switch (end)
    {
        case BgEnd::BotLeft: return End::BgBotLeft;
        case BgEnd::CommanderLeft: return End::BgCommanderLeft;
        case BgEnd::MatchEnded: return End::BgMatchEnded;
        case BgEnd::TeamChanged: return End::BgTeamChanged;
        case BgEnd::None: break;
    }
    return End::None;
}

// ---- the kind is per bound bot (BG2 review F2/F3) ----

// Each bound bot carries its own kind. A NORMAL bot belongs to its commander's binding record (the members, the stored row and the 1 Hz life rules
// above and below: leader-changed, group-raid, the timers). A BG bot (`.tb master` made inside one battleground instance, TbMasterBindPure.h
// IsBgBind) is in the BgBook instead: never stored, never in the record, no normal rule reads it, and it is released on its own when THAT bot or its
// commander leaves the instance it was bound in, the instance ends, or either logs out (BgBindEnd). One commander may hold both kinds at once, and
// one kind never changes the other's state: a bg bind leaves the record alone, a normal bind leaves the book alone, and a command in which every
// bot was refused changes nothing.
enum class Kind : uint8_t
{
    Normal,
    Bg,
};

struct BgMember
{
    uint64_t bot = 0;                   // character guid counter
    uint64_t commander = 0;
    uint32_t instance = 0;              // the commander's battleground instance when the bot bound (the handler's, never a bot of the record)
};

using BgBook = std::vector<BgMember>;   // one entry per bot; a bot not in it is normal

// One bot of a `.tb master` command: whether the host really bound it (h3 and the master written) and the kind the handler read for it.
struct BindResult
{
    uint64_t bot = 0;
    bool bound = false;
    Kind kind = Kind::Normal;
};

inline BgMember const* FindBg(BgBook const& book, uint64_t bot)
{
    for (BgMember const& member : book)
        if (member.bot == bot)
            return &member;
    return nullptr;
}

inline void DropBg(BgBook& book, uint64_t bot)
{
    book.erase(std::remove_if(book.begin(), book.end(), [&](BgMember const& member) { return member.bot == bot; }), book.end());
}

// One command's results into the book: every bot that really bound takes the kind of THIS bind (a bg bot with the commander's instance; a normal
// bot leaves the book). A refused bot changes nothing. True when anything bound.
inline bool ApplyBind(BgBook& book, uint64_t commander, std::vector<BindResult> const& results, uint32_t commanderInstance)
{
    bool any = false;
    for (BindResult const& result : results)
    {
        if (!result.bound || !result.bot)
            continue;
        any = true;
        DropBg(book, result.bot);
        if (result.kind == Kind::Bg)
            book.push_back({ result.bot, commander, commanderInstance });
    }
    return any;
}

// The commander's bots of each kind. `bound` is what h3 binds to him now; the normal ones are those the book does not give him as bg.
inline std::vector<uint64_t> NormalMembers(BgBook const& book, uint64_t commander, std::vector<uint64_t> const& bound)
{
    std::vector<uint64_t> normal;
    for (uint64_t bot : bound)
    {
        BgMember const* member = FindBg(book, bot);
        if (!member || member->commander != commander)
            normal.push_back(bot);
    }
    return normal;
}

inline std::vector<uint64_t> BgMembersOf(BgBook const& book, uint64_t commander)
{
    std::vector<uint64_t> bots;
    for (BgMember const& member : book)
        if (member.commander == commander)
            bots.push_back(member.bot);
    return bots;
}

// What one command does to the commander's NORMAL record (`recordMembers`: its members before the command).
enum class NormalUpdate : uint8_t
{
    Keep,       // no normal bot bound and none left the record: the record, its row and its rules stay exactly as they were
    Rebind,     // a normal bot bound: the record is rebuilt from the normal members (lead, flags, row)
    Shrink,     // a member of the record was rebound in a match (now bg): it leaves the record, the rest stands
    Drop,       // ...and it was the record's last member: the record and its row go, no release (the bot is bound, as bg)
};

inline NormalUpdate NormalRecordUpdate(std::vector<BindResult> const& results, std::vector<uint64_t> const& recordMembers)
{
    bool boundNormal = false;
    size_t movedOut = 0;
    for (BindResult const& result : results)
    {
        if (!result.bound || !result.bot)
            continue;
        if (result.kind == Kind::Normal)
            boundNormal = true;
        else if (std::find(recordMembers.begin(), recordMembers.end(), result.bot) != recordMembers.end())
            ++movedOut;
    }
    if (boundNormal)
        return NormalUpdate::Rebind;
    if (!movedOut)
        return NormalUpdate::Keep;
    return movedOut >= recordMembers.size() ? NormalUpdate::Drop : NormalUpdate::Shrink;
}

// The live facts of one bg member, read once a second (and at a logout, with that side offline).
struct BgLive
{
    uint64_t bot = 0;
    bool botOnline = false;
    uint32_t botInstance = 0;           // the bot's GetBattlegroundId()
    uint64_t botCommander = 0;          // h3's commander of the bot now (0 = none)
    bool commanderOnline = false;
    uint32_t commanderInstance = 0;     // the commander's GetBattlegroundId()
    bool sameTeam = true;               // the two live GetBGTeam() values are equal (false = a transfer since the bind)
};

struct BgVerdict
{
    uint64_t bot = 0;
    uint64_t commander = 0;
    End end = End::None;
    bool release = false;               // false: only drop the entry (h3 gives the bot to someone else now; releasing would undo that bind)
};

// The 1 Hz check of every bg member, each on its own facts: a member whose bot left (or logged out, or has no live row) is released alone; the
// commander leaving releases all of his. A verdict carries no group: a bg release never reaches into the battleground raid or its leader.
inline std::vector<BgVerdict> CheckBg(BgBook const& book, std::vector<BgLive> const& live, bool enabled)
{
    std::vector<BgVerdict> out;
    for (BgMember const& member : book)
    {
        BgLive facts;
        facts.bot = member.bot;
        facts.botCommander = member.commander;
        for (BgLive const& candidate : live)
            if (candidate.bot == member.bot)
            {
                facts = candidate;
                break;
            }
        if (facts.botCommander != member.commander)
        {
            out.push_back({ member.bot, member.commander, End::None, false });
            continue;
        }
        if (!enabled)
        {
            out.push_back({ member.bot, member.commander, End::Disabled, true });
            continue;
        }
        BgFacts bg;
        bg.instance = member.instance;
        bg.botInstance = facts.botInstance;
        bg.commanderInstance = facts.commanderInstance;
        bg.botOnline = facts.botOnline;
        bg.commanderOnline = facts.commanderOnline;
        bg.sameTeam = facts.sameTeam;
        End const end = BgEndToEnd(BgBindEnd(bg));
        if (end != End::None)
            out.push_back({ member.bot, member.commander, end, true });
    }
    return out;
}

// Does a release reach into a party (the shared in-party rule, which can hand the lead to the commander)? Only a normal record made in the party.
// A bg member never does: the battleground raid's leader is the match's, not the binding's.
inline bool ReleaseReachesParty(Kind kind, uint32_t flags)
{
    return kind == Kind::Normal && (flags & FlagInParty) != 0;
}

// What the world says about one standing binding now. Timers are in milliseconds; a grace of 0 releases at once.
struct Facts
{
    bool enabled = true;                // TortoiseBots.Enable (and the binding's own gates; P2 has no raid gate)
    bool leadInWorld = true;
    bool leadAlive = true;
    int64_t leadDeadMs = 0;
    int64_t leadDeadGraceMs = 0;
    bool leadGroup = true;              // the lead is in a live group
    bool leadGroupHoldsFollower = true; // that group holds a bound follower (true when the binding has none besides the lead)
    bool leadIsLeader = true;
    uint64_t leaderGuid = 0;            // the lead's live group leader (a character guid counter; 0 = no group)
    bool groupIsRaidOrBg = false;
    bool groupIsBg = false;
    size_t memberCount = 0;
    bool realPlayerInGroup = false;     // a real player other than the commander is a member
    bool commanderOnline = true;
    int64_t commanderGoneMs = 0;
    int64_t commanderGraceMs = 0;
    bool commanderInGroup = false;      // an in-party binding needs him there
};

// The most specific verdict; None while the binding stands. A classic binding (the commander outside the party) ends on a lead, group, leadership,
// party-size or joined-player change; an in-party or player-led one also when the commander is gone from the party. The two timers end last, so a
// binding that already lost its group is released with the group's reason, not with the commander's. Only the normal members reach this; a bg
// member is judged by CheckBg alone.
inline End Decide(Stored const& stored, Facts const& facts)
{
    if (!facts.enabled)
        return End::Disabled;
    if (!facts.leadInWorld)
        return End::LeadLeft;
    if (!facts.leadAlive && facts.leadDeadMs >= facts.leadDeadGraceMs && facts.leadDeadGraceMs >= 0)
        return End::LeadDead;
    if (!facts.leadGroup || !facts.leadGroupHoldsFollower)
        return End::GroupChanged;
    if (HasFlag(stored, FlagInParty) && !facts.commanderInGroup)
        return End::CommanderLeftParty;
    // The lead bot leading stands; so does a binding made IN THE PARTY while its own commander holds the party's lead. A binding made from outside
    // the party keeps its old rule: its commander joining the lead's party and taking the lead is a leader change, not a reason to stand.
    bool const commanderLeads = HasFlag(stored, FlagInParty) && facts.leaderGuid == stored.commander;
    if (!facts.leadIsLeader && !commanderLeads)
        return End::LeaderChanged;
    if (facts.groupIsRaidOrBg && (!HasFlag(stored, FlagRaid) || facts.groupIsBg))
        return End::GroupRaid;
    if (facts.realPlayerInGroup && !HasFlag(stored, FlagInParty) && !HasFlag(stored, FlagSystem))
        return End::PlayerJoined;
    if (facts.memberCount > (HasFlag(stored, FlagRaid) ? RC::kMaxRaidBindSize : RC::kMaxGroupSize))
        return End::GroupTooLarge;
    if (!facts.commanderOnline && facts.commanderGoneMs >= facts.commanderGraceMs && facts.commanderGraceMs >= 0)
        return End::CommanderAway;
    return End::None;
}

// The reply/log code of a verdict (empty for None): the grammar's lower-case word.
inline char const* EndCode(End end)
{
    switch (end)
    {
        case End::None: return "";
        case End::Disabled: return "disabled";
        case End::LeadLeft: return "lead-left";
        case End::LeadDead: return "lead-dead";
        case End::GroupChanged: return "group-changed";
        case End::LeaderChanged: return "leader-changed";
        case End::PlayerJoined: return "player-joined";
        case End::GroupRaid: return "group-raid";
        case End::GroupTooLarge: return "group-full";
        case End::CommanderLeftParty: return "commander-left-party";
        case End::CommanderAway: return "commander-away";
        case End::BgBotLeft: return "bg-bot-left";
        case End::BgCommanderLeft: return "bg-commander-left";
        case End::BgMatchEnded: return "bg-match-ended";
        case End::BgTeamChanged: return "bg-team-changed";
        case End::Unbound: return "unbound";
    }
    return "";
}

// One sentence for the log and the commander's feedback, in the release-reason style of the shared header.
inline char const* EndText(End end)
{
    switch (end)
    {
        case End::None: return "bound";
        case End::Disabled: return "the commander binding is switched off";
        case End::LeadLeft: return "the lead bot left the world";
        case End::LeadDead: return "the lead bot has been dead past the grace";
        case End::GroupChanged: return "the group was disbanded or the lead bot left it";
        case End::LeaderChanged: return "the lead bot no longer leads the group";
        case End::PlayerJoined: return "a player joined the group";
        case End::GroupRaid: return "the group became a raid";
        case End::GroupTooLarge: return "the group grew beyond its size";
        case End::CommanderLeftParty: return "the commander left the party";
        case End::CommanderAway: return "the commander has been away past the grace";
        case End::BgBotLeft: return "the bound bot left the battleground";
        case End::BgCommanderLeft: return "the commander left the battleground";
        case End::BgMatchEnded: return "the battleground the binding was made in has ended";
        case End::BgTeamChanged: return "the commander and the bound bot are on different teams now";
        case End::Unbound: return "released by the commander";
    }
    return "unknown";
}

// The shared policy reason the in-party release rule reads (DecideReleaseParty). A verdict it has no arm for maps to Unbound: with the commander
// present that hands the lead back to him, and with him gone the rule decides from his presence anyway.
inline RC::ReleaseReason PolicyReason(End end)
{
    switch (end)
    {
        case End::LeadLeft:
        case End::LeadDead: return RC::ReleaseReason::LeadLeft;
        case End::CommanderAway:
        case End::Disabled: return RC::ReleaseReason::CommanderLeft;
        case End::CommanderLeftParty: return RC::ReleaseReason::CommanderLeftParty;
        default: return RC::ReleaseReason::Unbound;
    }
}

// ---- a group hook's deferred release ----

// A GroupScript hook (OnAddMember, OnRemoveMember, OnChangeLeader, OnDisband) runs while the core is still walking the group:
// Group::RemoveMember fires it before it changes anything, Group::ChangeLeader before it stores the leader, and Group::Disband
// fires it at the start and then deletes the group. A release there removes members (BotsLeave), which can re-enter the group
// and free it under the core. The hooks only record the event; the next upkeep tick releases it. Nothing but character guids and
// the group's session counter may cross the tick - never a Group*, which the core can free.
struct DeferredFacts
{
    bool bindingStands = false;         // the binding is still recorded when the tick comes
    uint32_t recordedGroup = 0;         // the group counter the hook saw (a hook always has one; 0 = none recorded, no party action)
    bool recordedGroupExists = false;   // a group with the recorded counter is still in GroupMgr
};

enum class Deferred : uint8_t
{
    Release,        // release with the recorded reason, party action included
    WithoutParty,   // the recorded group is gone: release with its reason, but never reach into a group
    Drop,           // the binding is gone: another verdict (or a release) already dealt with it
};

// The release a pending event earns on its tick. The binding and the group are looked up by guid again: a binding that was
// released meanwhile has nothing left to do, and a group that is gone still ends the binding with the recorded reason, but the
// party action is skipped - there is nothing left to clean, and the counter names nothing at all (a hook records the group it saw).
// The group counter is monotonic for the process (GroupMgr::GenerateGroupId), so an existing counter is the same group the hook saw.
inline Deferred DeferredRelease(DeferredFacts const& facts)
{
    if (!facts.bindingStands)
        return Deferred::Drop;
    if (!facts.recordedGroup || !facts.recordedGroupExists)
        return Deferred::WithoutParty;
    return Deferred::Release;
}

// Followers a binding put on the lead that its group no longer holds (kicked, invited elsewhere, left): their follow target goes back now, the rest
// of the binding stands (the shared rule, applied with the guid list the host reads from the live group).
inline std::vector<uint64_t> Departed(std::vector<uint64_t> const& members, uint64_t lead, std::vector<uint64_t> const& groupMembers)
{
    RC::Binding binding;
    binding.leadGuid = lead;
    binding.followers = members;
    binding.followers.erase(std::remove(binding.followers.begin(), binding.followers.end(), lead), binding.followers.end());
    return RC::DepartedFollowers(binding, groupMembers);
}

// ---- restoring a stored row ----

struct RestorePlan
{
    bool ready = false;                 // the commander and the lead are in the world: the spec's failing case is a restore without the lead
    std::vector<uint64_t> join;         // online members (the lead excluded) to put into the lead's party, stored order
    std::vector<uint64_t> bind;         // the lead first, then the online members: each one's commander is the stored commander
};

// What the host knows about one stored member now. `bot` is the module's host-bot table (only a bot may ever be grouped and bound), `commander`
// h3's table and `master` the host master the bot follows. None of them is a value a row can write: the row names characters, the host says what
// they are.
struct Owner
{
    uint64_t member = 0;
    uint64_t commander = 0;             // h3's commander now (0 = none)
    uint64_t master = 0;                // the host master now (0 = none)
    bool bot = false;                   // a host bot of this module
};

struct Claims
{
    std::vector<uint64_t> free;         // unclaimed by anyone else: the restore may join and bind them
    std::vector<uint64_t> foreign;      // bound to another commander, mastered by another character, or not a host bot: dropped, never taken
};

// THE AUTHORIZATION of a restore: a stored member that the host says belongs to another commander (h3 or the master table) is never joined,
// grouped or bound by this row, and a stored character that is not a host bot is never bound at all. The row follows the host: the foreign
// entries are dropped from it. A member the caller does not describe (offline) is free: there is nothing to contradict the row.
inline Claims Claim(uint64_t commander, std::vector<uint64_t> const& members, std::vector<Owner> const& current)
{
    Claims claims;
    for (uint64_t member : members)
    {
        Owner const* owner = nullptr;
        for (Owner const& candidate : current)
            if (candidate.member == member)
            {
                owner = &candidate;
                break;
            }
        bool const foreign = owner && ((owner->commander && owner->commander != commander) ||
            (owner->master && owner->master != commander) || !owner->bot);
        (foreign ? claims.foreign : claims.free).push_back(member);
    }
    return claims;
}

// `online`: the members (the lead included) that are in the world now. A member that is not online yet is skipped - the row stays and the next login
// tries again - but the commander and the lead must both be there. An offline lead must never be "restored" by binding the others around a stale row.
inline RestorePlan PlanRestore(Stored const& stored, bool commanderInWorld, std::vector<uint64_t> const& online)
{
    RestorePlan plan;
    auto const isOnline = [&](uint64_t guid) { return std::find(online.begin(), online.end(), guid) != online.end(); };
    if (!stored.commander || !stored.lead || !commanderInWorld || !isOnline(stored.lead))
        return plan;
    plan.ready = true;
    plan.bind.push_back(stored.lead);
    for (uint64_t member : stored.members)
        if (member != stored.lead && isOnline(member))
        {
            plan.join.push_back(member);
            plan.bind.push_back(member);
        }
    return plan;
}

// ---- which bot leads the binding ----

// What a bind knows when it picks the lead. `namedLeader` is the `who.leader` the command was addressed through. A host bot this commander commands is
// the lead the command named. A player (the commander leading his own party, or another player's party the bots were joined to) names no bot at all,
// and h3 cannot answer for a player, so the lead is a bot of the party the bind was addressed through: a tank first, then the party's order. The
// previous lead is kept while it is still bound (a rebind must not shuffle the card), and the first bound bot is the last resort.
struct LeadFacts
{
    uint64_t namedLeader = 0;
    bool namedLeaderIsBotOfCommander = false;
    std::vector<uint64_t> bound;            // every bot h3 binds to the commander now
    std::vector<uint64_t> partyBots;        // the bound bots in the named leader's party
    std::vector<uint64_t> partyTanks;       // ...and those of them whose role is tank
    uint64_t previousLead = 0;
};

inline uint64_t PickLead(LeadFacts const& facts)
{
    auto const isBound = [&](uint64_t guid) { return std::find(facts.bound.begin(), facts.bound.end(), guid) != facts.bound.end(); };
    if (facts.namedLeader && facts.namedLeaderIsBotOfCommander)
        return facts.namedLeader;
    if (facts.namedLeader)
    {
        for (uint64_t tank : facts.partyTanks)
            if (isBound(tank))
                return tank;
        for (uint64_t bot : facts.partyBots)
            if (isBound(bot))
                return bot;
    }
    if (facts.previousLead && isBound(facts.previousLead))
        return facts.previousLead;
    return facts.bound.empty() ? 0 : facts.bound.front();
}

} // namespace life
} // namespace cmd
} // namespace TortoiseBots
