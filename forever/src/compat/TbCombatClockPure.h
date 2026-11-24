// Core-free half of CombatTimeCompat (TbCompat_Unit.h) on ForeverCore: seconds since a creature entered combat.
//
// T: Creature::GetCombatTime() (Creature.h:833). F keeps no engage time (Creature::AtEngage is the only hook, and a module may not
// change the core), and the port answered -1 ("unknown") for every creature. -1 keeps the crowd-control mark's fight-start hold
// window (CcMarkHoldPolicy.h CcMarkHeld, CcMarkHoldSeconds) closed for good: from the first second of a pull the party took an
// ordered mark that was not sheeped yet as a normal enemy (the tank's Taunt and Thunder Clap, the priest's Shadow Word: Pain) and
// broke every Polymorph the mage got out (live #208, recheck 2: thirteen Polymorph casts that started on the mark, none held).
//
// So the module measures it: the first time a bot asks about the creature while it is in combat starts its clock, and the clock
// is dropped as soon as a question finds it out of combat (evade, death, reset). The bots ask every tick of a fight that has a
// crowd-control mark in it, so the start is the engage time within a tick. DIFFERENT ANSWER: a mark first looked at mid-fight
// (an order given 30 s into the fight) starts its clock then, so the mark gets its hold window from the order on.
//
// Not thread-safe: the caller holds a lock (the bots of different maps ask from different map threads).
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <unordered_map>

namespace TortoiseBots
{
class CombatClock
{
public:
    // A creature that despawns in combat is never asked about out of combat again: past this many clocks the stale ones
    // (older than kStaleSeconds, no fight lasts that long) are dropped before a new one starts.
    static constexpr std::size_t kPruneAbove = 4096;
    static constexpr std::time_t kStaleSeconds = 3600;

    // Seconds since `key` was first seen in combat, 0 on that first sight, -1 when it is not in combat now.
    long Observe(std::uint64_t key, bool inCombat, std::time_t now)
    {
        if (!inCombat)
        {
            starts.erase(key);
            return -1;
        }
        auto const found = starts.find(key);
        if (found == starts.end())
        {
            if (starts.size() >= kPruneAbove)
                Prune(now);
            starts.emplace(key, now);
            return 0;
        }
        return now > found->second ? long(now - found->second) : 0;
    }

    std::size_t Size() const { return starts.size(); }

private:
    void Prune(std::time_t now)
    {
        for (auto it = starts.begin(); it != starts.end();)
            it = now - it->second > kStaleSeconds ? starts.erase(it) : std::next(it);
    }

    std::unordered_map<std::uint64_t, std::time_t> starts;
};
}
