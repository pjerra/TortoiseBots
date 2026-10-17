// Standalone regression test for issue #903 (casters cast while moving and
// lose their casts), extending ai/playerbot/CastKeepsRunningPolicy.h (#1062):
// while a cast-time cast runs, non-escape moves stand down, and a cast-time
// cast never starts while the bot is still moving. Hazard escapes (standing
// in fire / void zone, the bomb carrier's runout) still interrupt.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_cast_while_moving_policy.cpp -o /tmp/test_cast_while_moving
//   /tmp/test_cast_while_moving

#include "../ai/playerbot/CastKeepsRunningPolicy.h"

#include <cstdio>
#include <cstdlib>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// The #1062 half still holds: follow and the loot chain stand down while any
// non-melee cast runs.
static void TestOldGuardsUnchanged()
{
    CHECK(ai::FollowBlockedByCast(true));
    CHECK(!ai::FollowBlockedByCast(false));
    CHECK(ai::LootChainBlockedByCast(true));
    CHECK(!ai::LootChainBlockedByCast(false));
}

// A cast-time cast must not start while the bot is moving (or its last
// spline is still in flight): the core cancels it on the first movement
// packet. Instant casts start anyway.
static void TestCastStartWaitsForStandstill()
{
    CHECK(ai::CastStartBlockedByMove(true, false, true));
    CHECK(ai::CastStartBlockedByMove(false, true, true));
    CHECK(!ai::CastStartBlockedByMove(false, false, true));
    CHECK(!ai::CastStartBlockedByMove(true, false, false));
    CHECK(!ai::CastStartBlockedByMove(false, false, false));
}

// "reach spell", spread, move-away and flee repositioning must not start
// mid-cast: the first step cancels the bar. Hazard escapes (standing in
// fire, the bomb carrier's runout) still interrupt.
static void TestNonEscapeMovesStandDownMidCast()
{
    CHECK(ai::MoveBlockedByCast(true, false));
    CHECK(!ai::MoveBlockedByCast(false, false));
    CHECK(!ai::MoveBlockedByCast(true, true));
    CHECK(!ai::MoveBlockedByCast(false, true));
}

int main()
{
    std::printf("Starting cast-while-moving policy tests...\n");
    TestOldGuardsUnchanged();
    std::printf("  [PASS] #1062 follow/loot guards unchanged\n");
    TestCastStartWaitsForStandstill();
    std::printf("  [PASS] cast-time cast waits for standstill\n");
    TestNonEscapeMovesStandDownMidCast();
    std::printf("  [PASS] non-escape moves stand down mid-cast\n");
    std::printf("All cast-while-moving policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
