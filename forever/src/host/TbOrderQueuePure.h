// Slice 6, review fix R6 (item 1): the order queue's cancel-by-provenance, core-free so a unit test can prove it (tools/test_run_cancel.cpp).
//
// The run's orders are queued (TbOrders.cpp Enqueue) and run by the bot's engine at its next tick (BeforeEngineTick) when younger than OrderMaxAgeMs. A
// `.tb run stop` or a pause that came between the executor's call and that tick left the order in the queue, and it ran: a pull, a move, a focus attack or a
// rest after the stop. CancelFromRun removes exactly the orders the run's executor queued (fromRun) and leaves a hand-typed `.tb order` alone.
#pragma once

#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{
namespace h3
{
// Removes every entry whose `fromRun` is true from `queue` (a deque of orders), keeps the order of the rest, calls `cancelled(order)` for each removed one.
// Returns how many were removed.
template <class Queue, class OnCancelled>
inline std::size_t CancelFromRun(Queue& queue, OnCancelled&& cancelled)
{
    std::size_t removed = 0;
    for (auto it = queue.begin(); it != queue.end();)
    {
        if (it->fromRun)
        {
            cancelled(*it);
            it = queue.erase(it);
            ++removed;
        }
        else
            ++it;
    }
    return removed;
}

// R6 (review item 5): the run's leader must not follow its commander. EndStayForOrder (the legacy relax of a pull) gives a bot with no movement mode the default
// back, which is `follow` of its commander, and the run never rebinds its leader (the commander may stand outside the instance, far away). The hold: a run's
// leader out of combat, with no pull under way, that follows, is put on `stay` at the spot it stands on. The commander's position is not an input: it must not
// matter. (A pull under way is left alone: its walk is the pull's own; a fight is left alone.)
inline bool ShouldHoldLead(bool following, bool inCombat, bool pullActive)
{
    return following && !inCombat && !pullActive;
}

// R6 (review item 8): `focus` answers ok although "attack rti target" refused only when the mark waits for sight: the marked unit exists, is in the world, alive
// and on the bot's map (so the empty `rti target` value means "not in sight or range yet"). Any other empty value (dead, despawned, another map) is fail:focus.
inline bool FocusMarkWaits(bool exists, bool inWorld, bool alive, bool sameMap)
{
    return exists && inWorld && alive && sameMap;
}

// Codex review fix: an order's point is a spot on ONE Map object. The map id is equal for two copies of a dungeon, so a queued point that names the instance
// must still be in that instance when the bot runs it. `instance = 0` = the order recorded none (a hand-typed order, an open-world map): the map id alone.
inline bool PointOnBotMap(std::uint32_t map, std::uint32_t instance, std::uint32_t botMap, std::uint32_t botInstance)
{
    return (!map || map == botMap) && (!instance || instance == botInstance);
}
}
}
