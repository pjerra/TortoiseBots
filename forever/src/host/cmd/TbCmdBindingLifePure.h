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
//    commander away past the mastered-bot grace. The timers never release sooner than their grace.
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
    FlagPlayerLed = 4,
    FlagSystem = 8,
};

inline bool HasFlag(Stored const& stored, uint32_t flag) { return (stored.flags & flag) != 0; }

// `1,2,4` for the flags of the binding (readable in a database shell; 0 = a plain commander outside the party).
inline uint32_t FlagsOf(bool inParty, bool raid, bool playerLed, bool system)
{
    return (inParty ? FlagInParty : 0u) | (raid ? FlagRaid : 0u) | (playerLed ? FlagPlayerLed : 0u) | (system ? FlagSystem : 0u);
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
    Unbound,            // the commander's own release; not a verdict of Decide
};

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
// binding that already lost its group is released with the group's reason, not with the commander's.
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
    if (!HasFlag(stored, FlagPlayerLed) && !facts.leadIsLeader)
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

} // namespace life
} // namespace cmd
} // namespace TortoiseBots
