// R6 (review items 1, 5, 8): the order queue's cancel-by-provenance and the two small decisions of the run's order layer, core-free.
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_run_cancel.cpp -o /tmp/t && /tmp/t
#include "forever/src/host/TbOrderQueuePure.h"

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace TortoiseBots::h3;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

struct Order { unsigned seq; std::string verb; bool fromRun; };

int main()
{
    // item 1: a pull, a move, a focus and a rest queued by the run, a hand-typed order between them. Stop (or pause) before the engine tick.
    std::deque<Order> queue = { {1, "pull", true}, {2, "stay", false}, {3, "come", true}, {4, "focus", true}, {5, "rest", true} };
    std::vector<unsigned> seen;
    std::size_t const removed = CancelFromRun(queue, [&seen](Order const& o) { seen.push_back(o.seq); });
    CHECK(removed == 4);
    CHECK(seen == (std::vector<unsigned>{1, 3, 4, 5}));
    CHECK(queue.size() == 1 && queue.front().verb == "stay");     // the hand-typed order stays, in its place
    // what the engine tick would have run: nothing of the run's
    for (Order const& o : queue)
        CHECK(!o.fromRun);
    // an empty queue and a queue with no run order are no-ops
    std::deque<Order> empty;
    CHECK(CancelFromRun(empty, [](Order const&) {}) == 0);
    std::deque<Order> manual = { {7, "follow", false}, {8, "attack", false} };
    CHECK(CancelFromRun(manual, [](Order const&) {}) == 0 && manual.size() == 2);
    // a second cancel finds nothing
    CHECK(CancelFromRun(queue, [](Order const&) {}) == 0);

    // item 5: the leader that follows its (far) commander is held; a fight, a pull under way, or no follow are left alone. The commander's distance is no input.
    CHECK(ShouldHoldLead(true, false, false));
    CHECK(!ShouldHoldLead(true, true, false));
    CHECK(!ShouldHoldLead(true, false, true));
    CHECK(!ShouldHoldLead(false, false, false));

    // item 8: the mark waits for sight only for a unit that exists, in the world, alive, on the bot's map.
    CHECK(FocusMarkWaits(true, true, true, true));
    CHECK(!FocusMarkWaits(false, false, false, false));   // despawned
    CHECK(!FocusMarkWaits(true, true, false, true));      // dead
    CHECK(!FocusMarkWaits(true, false, true, true));      // not in the world
    CHECK(!FocusMarkWaits(true, true, true, false));      // another map

    // Codex review: a queued point belongs to one Map object, not to a map id (two copies of a dungeon share the id).
    CHECK(PointOnBotMap(44, 7, 44, 7));
    CHECK(!PointOnBotMap(44, 7, 44, 8));      // equal map ids, another instance
    CHECK(!PointOnBotMap(44, 7, 45, 7));
    CHECK(PointOnBotMap(44, 0, 44, 8));       // the order recorded no instance: the map id alone
    CHECK(PointOnBotMap(0, 0, 44, 8));        // a hand-typed order names neither

    if (failures)
        return 1;
    std::printf("RESULT: PASS (test_run_cancel)\n");
    return 0;
}
