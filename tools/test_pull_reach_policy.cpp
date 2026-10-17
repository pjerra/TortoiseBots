#include "../ai/playerbot/PullReachPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PullApproachExpired;
using ai::PullApproachFacts;
using ai::PullApproachHeadway;

int main()
{
    std::cout << "Starting TortoiseBots pull reach policy tests...\n";

    // A fresh pull with a walking tank: the window never expires.
    {
        PullApproachFacts facts;
        facts.elapsedOrderSec = 2;
        facts.elapsedHeadwaySec = 2;
        CHECK(!PullApproachExpired(facts));
        std::cout << "  [PASS] fresh pull keeps its target\n";
    }

    // Issue #910: the tank is still closing in after the flat 15 s window
    // (cross-room walk to Rockchewer). Headway is recent, the total is far
    // from the cap: the pull keeps its target through the reach.
    {
        PullApproachFacts facts;
        facts.elapsedOrderSec = 25;
        facts.elapsedHeadwaySec = 3;
        CHECK(!PullApproachExpired(facts));
        std::cout << "  [PASS] progressing approach survives the base window\n";
    }

    // A wedged pull: no headway for a full window ends it, exactly like the
    // old flat timeout did.
    {
        PullApproachFacts facts;
        facts.elapsedOrderSec = 16;
        facts.elapsedHeadwaySec = 15;
        CHECK(PullApproachExpired(facts));
        std::cout << "  [PASS] stalled approach ends the pull\n";
    }

    // Bounded: even a shuffling pull ends at the total cap (4 x 15 s).
    {
        PullApproachFacts facts;
        facts.elapsedOrderSec = 60;
        facts.elapsedHeadwaySec = 1;
        CHECK(PullApproachExpired(facts));
        facts.elapsedOrderSec = 59;
        CHECK(!PullApproachExpired(facts));
        std::cout << "  [PASS] total cap bounds a shuffling pull\n";
    }

    // Headway: closing distance counts, whatever the movement flag says.
    CHECK(PullApproachHeadway(30.0f, 35.0f, true, false));
    CHECK(PullApproachHeadway(30.0f, 35.0f, true, true));
    std::cout << "  [PASS] closing distance is headway\n";

    // Moving without losing ground (a detour) counts; standing still does
    // not, and sub-half-yard jitter never does.
    CHECK(PullApproachHeadway(30.0f, 30.0f, true, true));
    CHECK(!PullApproachHeadway(30.0f, 30.0f, true, false));
    CHECK(!PullApproachHeadway(30.2f, 30.0f, true, true));
    CHECK(!PullApproachHeadway(35.0f, 30.0f, true, true));
    std::cout << "  [PASS] detour counts, standing still does not\n";

    // The first sample after the order anchors the clock, never expires it.
    CHECK(PullApproachHeadway(45.0f, 0.0f, false, false));
    std::cout << "  [PASS] first sample anchors the clock\n";

    std::cout << "pull reach policy: ok\n";
    return 0;
}
