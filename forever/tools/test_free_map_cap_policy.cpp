// Standalone test of the per-map free-bot cap (ai/playerbot/policy/world/FreeMapCapPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_free_map_cap_policy.cpp -o /tmp/test_free_map_cap_policy && /tmp/test_free_map_cap_policy
//
// A dungeon takes the 5-man cap; a raid takes the cap of its size (10, 20, 40 by the map row's player count, unknown
// reads as 40). An open-world map, a battleground and an arena have no cap, and a kind switched to 0 has none either.
#include "ai/playerbot/policy/world/FreeMapCapPolicy.h"

#include <cstdio>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    w::FreeMapCaps const caps;
    CHECK(w::FreeMapCapFor(false, false, 0, caps) == 0);     // open world: no cap
    CHECK(w::FreeMapCapFor(true, false, 5, caps) == 5);      // a dungeon: the 5-man cap
    CHECK(w::FreeMapCapFor(true, false, 0, caps) == 5);      // a dungeon's count is always 5
    CHECK(w::FreeMapCapFor(true, true, 5, caps) == 10);       // a raid plays by raid caps even at dungeon size
    CHECK(w::FreeMapCapFor(true, true, 10, caps) == 10);     // a 10-man raid: the 10-man cap
    CHECK(w::FreeMapCapFor(true, true, 20, caps) == 20);     // Zul'Gurub and AQ20: the 20-man cap
    CHECK(w::FreeMapCapFor(true, true, 40, caps) == 40);     // Molten Core and the rest: the 40-man cap
    CHECK(w::FreeMapCapFor(true, true, 25, caps) == 40);     // past 20: the 40-man cap
    CHECK(w::FreeMapCapFor(true, true, 0, caps) == 40);      // unknown player count: the 40-man cap

    w::FreeMapCaps off;
    off.dungeon = 0;
    off.raid10 = 0;
    off.raid20 = 0;
    off.raid40 = 0;
    CHECK(w::FreeMapCapFor(true, false, 5, off) == 0);
    CHECK(w::FreeMapCapFor(true, true, 10, off) == 0);
    CHECK(w::FreeMapCapFor(true, true, 20, off) == 0);
    CHECK(w::FreeMapCapFor(true, true, 40, off) == 0);

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
