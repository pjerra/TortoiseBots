// Commander port, package P5 (the fight debugger): the PURE half of the ForeverCore feed into the combat monitor. FOREVER-COMMANDER-PORT-SPEC.md
// sections 5 and 7 (P5), Runtime/BoundFightPolicy.h and runtime/CombatMonitorPolicy.h for the shared decisions.
//
// Plain data and the two pure policy headers only: no core header, no database, no legacy type. Tested standalone by
// forever/tools/cmd_combat_check.cpp (g++ -std=c++17 -I. forever/tools/cmd_combat_check.cpp runtime/CombatMonitorService.cpp).
//
// What it decides, so a core-facing tick only reads live objects: whether a card is fed (a binding with no live group has no book), when a
// pass is due (the world timer wraps, so the elapsed time is an unsigned subtraction), which engaged unit is the debuff focus, which aura
// ids changed between two 1 Hz samples, and which creature ranks the cast poll reads.
#pragma once

#include "runtime/CombatMonitorPolicy.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace combat
{
namespace pure
{
namespace CM = ::TortoiseBots::CombatMonitor;

constexpr uint32_t kSampleMs = 1000;      // the bound-group sampler, the run tick's own cadence
constexpr uint32_t kCastPollMs = 250;     // the elite and boss cast starts of the groups in a fight
constexpr uint32_t kEventsMs = 100;       // how often the queued COMBAT_EVENTS are drained
constexpr uint32_t kEventGapMs = 250;     // the least time between two COMBAT_EVENTS of one group (the emitter's realtime default)
constexpr size_t kWatchCap = 8;           // units the cast poll reads per binding per pass, and the sampler resolves for the focus

// A card is fed only when the lead is in a live group: the book is keyed by the group's session counter, and a party-less binding has no
// counter to key it with (T's own rule: groupId 0 is never sampled). Feeding it would put every single-bot binding on one book.
inline bool Feedable(uint32_t groupId)
{
    return groupId != 0;
}

// True when a pass is due. `sinceMs` 0 is the first pass after boot or after the monitor was turned on; the subtraction is the core's
// getMSTime rule (unsigned, so a tick wrap reads as the small elapsed time it is).
inline bool Due(uint32_t nowMs, uint32_t sinceMs, uint32_t everyMs)
{
    return sinceMs == 0 || nowMs - sinceMs >= everyMs;
}

// What a creature reads as in a fight: the fight's boss (the dungeon-boss flag, an instance-bind flag, or a plan boss step of the map),
// an elite (the elite and rare-elite classifications: the cast poll's other rank), or a plain mob.
enum class Rank : uint8_t
{
    None = 0,
    Elite = 1,
    Boss = 2
};

inline Rank RankOf(bool boss, bool elite)
{
    return boss ? Rank::Boss : elite ? Rank::Elite : Rank::None;
}

// The cast event's rank letter ('b' boss, 'e' elite), 0 for a mob the poll does not read. The dashboard draws the two apart.
inline char CastRank(Rank rank)
{
    return rank == Rank::Boss ? 'b' : rank == Rank::Elite ? 'e' : 0;
}

inline bool CastWatched(Rank rank)
{
    return rank != Rank::None;
}

// One engaged unit as the focus pick reads it: the entry (to match the run's boss step) and the health the tie-break uses.
struct FocusCandidate
{
    uint32_t entry = 0;
    uint32_t maxHealth = 0;
};

// The engaged unit whose tracked debuffs are sampled: the run's boss when he is among them (a boss step's pull), else the one with the most
// maximum health (the tank's current target). -1 when nothing is engaged.
inline int PickFocus(std::vector<FocusCandidate> const& engaged, uint32_t bossEntry)
{
    if (bossEntry)
        for (size_t i = 0; i < engaged.size(); ++i)
            if (engaged[i].entry == bossEntry)
                return static_cast<int>(i);
    int best = -1;
    uint32_t bestHealth = 0;
    for (size_t i = 0; i < engaged.size(); ++i)
        if (engaged[i].maxHealth > bestHealth)
        {
            bestHealth = engaged[i].maxHealth;
            best = static_cast<int>(i);
        }
    return best;
}

// The auras that changed between two samples of one bot: (spell id, applied). Both lists are the applied, non-passive aura ids, sorted and
// unique, so the walk is linear. ForeverCore has no aura hook (its UnitScript carries no apply/remove), so the timeline's aura rows come
// from this 1 Hz diff; an aura that comes and goes inside one second is not seen (the Turtle hook saw it).
inline std::vector<std::pair<uint32_t, bool>> AuraEdges(std::vector<uint32_t> const& before, std::vector<uint32_t> const& now)
{
    std::vector<std::pair<uint32_t, bool>> edges;
    size_t a = 0, b = 0;
    while (a < before.size() || b < now.size())
    {
        if (b == now.size() || (a < before.size() && before[a] < now[b]))
        {
            edges.emplace_back(before[a], false);
            ++a;
        }
        else if (a == before.size() || now[b] < before[a])
        {
            edges.emplace_back(now[b], true);
            ++b;
        }
        else
        {
            ++a;
            ++b;
        }
    }
    return edges;
}

// A group with no living member: the wipe the book ends the fight on. An empty member list is not a wipe (there is nothing to have died).
inline bool AllDead(size_t members, size_t alive)
{
    return members > 0 && alive == 0;
}

// The card's role text ("tank", "healer", "dps"; "" for a character that is no host bot) as the fight record's role.
inline CM::Role RoleOf(std::string const& role)
{
    if (role == "tank")
        return CM::Role::Tank;
    if (role == "healer")
        return CM::Role::Healer;
    return CM::Role::Damage;
}

// The sample's aura list for the timeline diff: sorted and unique, the edge walk's contract.
inline void SortUnique(std::vector<uint32_t>& ids)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

} // namespace pure
} // namespace combat
} // namespace cmd
} // namespace TortoiseBots
