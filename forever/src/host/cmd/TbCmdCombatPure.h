// Commander port, package P5 (the fight debugger): the PURE half of the ForeverCore feed into the combat monitor. FOREVER-COMMANDER-PORT-SPEC.md
// sections 5 and 7 (P5), Runtime/BoundFightPolicy.h and runtime/CombatMonitorPolicy.h for the shared decisions.
//
// Plain data and the two pure policy headers only: no core header, no database, no legacy type. Tested standalone by
// forever/tools/cmd_combat_check.cpp (g++ -std=c++17 -I. forever/tools/cmd_combat_check.cpp runtime/CombatMonitorService.cpp).
//
// What it decides, so a core-facing tick only reads live objects: which book a card's fight lives in, whether a card is fed (a binding with
// no live group has no book), when a pass is due (the world timer wraps, so the elapsed time is an unsigned subtraction), which engaged units
// a pass keeps and at what cost, which of them is the debuff focus, which aura ids changed between two 1 Hz samples, and which creature ranks
// the cast poll reads.
#pragma once

#include "runtime/CombatMonitorPolicy.h"

#include <algorithm>
#include <array>
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
constexpr size_t kWatchCap = 8;           // units a pass keeps: the cast poll reads them all, the sampler resolves the focus among them
constexpr size_t kScanRefs = 128;         // combat references a pass visits (every member's attackers and combat references together)

// The book one card's fight is recorded in: keyed by the commander binding, never by the WoW group counter. Two commander cards can lead
// parties that stand in one group; whichever sampled last would otherwise own the members, the boss and the fight of both. The group counter
// stays metadata: the payload reports it, the walk does not key by it, and a card that changes group keeps its book.
struct BookRef
{
    uint32_t key = 0;      // what the fight's state and its window are stored under
    uint32_t group = 0;    // the WoW group counter, what the dashboard shows
};

inline BookRef BookOf(uint32_t commanderGuid, uint32_t groupId)
{
    return BookRef{ commanderGuid, groupId };
}

// A card is fed only when the lead stands in a live group and the card has an identity to key its book with: T's rule (group counter 0 is
// never sampled), and the sample's lead check compares that counter against the binding's, so a party-less binding would have nothing to feed
// a book for.
inline bool Feedable(uint32_t bookKey, uint32_t groupId)
{
    return bookKey != 0 && groupId != 0;
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

// The engaged units one pass keeps, found in a single walk that is bounded twice: at most kScanRefs references visited and at most kWatchCap
// kept. Nothing of the pull is materialised: a visited guid stands in a fixed array (each guid looked at once), the ranks are compared as they
// arrive, and the kept units stay ordered best rank first, earliest first within a rank, so the choice is stable and the first kept unit is the
// best the walk saw. The walk ends at the budget, or when kWatchCap units of the best rank that exists are held and nothing left can displace
// them. A raid pull therefore costs the world thread what a party pull does, however many references it has.
template <typename Guid>
class EngagedScan
{
public:
    explicit EngagedScan(bool watchedOnly = false) : m_watched(watchedOnly) { }

    // The walk is over: the budget is spent, or the kept set cannot improve.
    bool Over() const { return m_visited >= kScanRefs || Saturated(); }

    // One reference. False when its guid was already visited or the walk is over, so the caller ranks nothing for it.
    bool Visit(Guid const& guid)
    {
        if (Over())
            return false;
        for (size_t i = 0; i < m_visited; ++i)
            if (m_seen[i] == guid)
                return false;
        m_seen[m_visited++] = guid;
        return true;
    }

    // A visited reference's rank. With `watchedOnly` (the cast poll) the plain mobs are not kept: they are visited, since the cost bound is
    // over references, not over the ones that end up kept, but the poll reads no row for them.
    void Keep(Guid const& guid, Rank rank)
    {
        if (m_watched && !CastWatched(rank))
            return;
        size_t at = 0;
        while (at < m_kept && static_cast<uint8_t>(m_keys[at].rank) >= static_cast<uint8_t>(rank))
            ++at;
        if (at == kWatchCap)
            return;   // kWatchCap units of this rank or better are already held
        for (size_t i = m_kept < kWatchCap ? m_kept : kWatchCap - 1; i > at; --i)
            m_keys[i] = m_keys[i - 1];
        m_keys[at] = Key{ guid, rank };
        if (m_kept < kWatchCap)
            ++m_kept;
    }

    size_t size() const { return m_kept; }
    Guid const& key(size_t i) const { return m_keys[i].guid; }
    size_t visited() const { return m_visited; }

private:
    struct Key
    {
        Guid guid{};
        Rank rank = Rank::None;
    };

    // Nothing better than a boss exists, so a full set of bosses is final.
    bool Saturated() const { return m_kept == kWatchCap && m_keys[kWatchCap - 1].rank == Rank::Boss; }

    bool m_watched = false;
    std::array<Guid, kScanRefs> m_seen{};
    std::array<Key, kWatchCap> m_keys{};
    size_t m_visited = 0;
    size_t m_kept = 0;
};

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
