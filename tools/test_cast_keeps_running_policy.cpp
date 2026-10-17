// Standalone regression test for the cast-keeps-running policy (#1062,
// ai/playerbot/CastKeepsRunningPolicy.h): alt bots walked to mining/herb
// nodes, played the gather animation and sound, but got no loot - the 3.2 s
// Mining / Herb Gathering cast never completed.
//
// Two interference paths broke it while the AI kept ticking mid-cast:
//  - follow station-keeping ("update follow", idle) re-issued Follow(), which
//    stops any cast before checking whether the bot needs to move at all;
//  - the loot chain re-selected the cleared node ("add gathering loot" ->
//    "loot") and "open loot" re-fired into the cast gate, burning one of the
//    three open attempts per tick until the node was abandoned mid-cast.
//
// The fix stands both down while a non-melee cast runs (mod-playerbots
// parity: its LootAction/OpenLootAction refuse while IsNonMeleeSpellCast).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_cast_keeps_running_policy.cpp -o /tmp/test_cast_keeps_running
//   /tmp/test_cast_keeps_running

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

// While the gather cast runs, follow must stand down - even station-keeping
// beside a waiting master, or the step breaks the cast.
static void TestFollowStandsDownWhileCasting()
{
    CHECK(ai::FollowBlockedByCast(true));
    CHECK(!ai::FollowBlockedByCast(false));
}

// While the gather cast runs, the loot chain must neither reselect nor
// re-open: the re-cast is refused and each refusal used to count toward the
// three-strike abandon that wedged the node before the cast finished.
static void TestLootChainStandsDownWhileCasting()
{
    CHECK(ai::LootChainBlockedByCast(true));
    CHECK(!ai::LootChainBlockedByCast(false));
}

// Once the cast finishes (or when idle), both resume: a master that kept
// walking is followed, and the next node opens normally.
static void TestBothResumeWhenCastEnds()
{
    CHECK(!ai::FollowBlockedByCast(false));
    CHECK(!ai::LootChainBlockedByCast(false));
}

int main()
{
    std::printf("Starting cast-keeps-running policy tests...\n");
    TestFollowStandsDownWhileCasting();
    std::printf("  [PASS] follow stands down while casting\n");
    TestLootChainStandsDownWhileCasting();
    std::printf("  [PASS] loot chain stands down while casting\n");
    TestBothResumeWhenCastEnds();
    std::printf("  [PASS] both resume when the cast ends\n");
    std::printf("All cast-keeps-running policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
