// Standalone test of the gather proof by bag snapshots (ai/playerbot/policy/profession/GatherNodePolicy.h, BagGainSince):
//   g++ -std=c++17 -I. -Iai forever/tools/test_gather_bag_proof.cpp -o /tmp/t && /tmp/t
#include "playerbot/policy/profession/GatherNodePolicy.h"
#include <cstdio>

using namespace TortoiseBots::prof;
static int failed = 0, checks = 0;
static void Check(bool ok, char const* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL: %s\n", what); } }

int main()
{
    // The core stored 3 Peacebloom into a new slot before the errand read the loot window.
    BagGain g = BagGainSince({ { 2589, 4 } }, { { 2589, 4 }, { 2447, 3 } });
    Check(g.itemId == 2447 && g.gained == 3, "a new stack counts as the gather's loot");
    // Herbs added onto an existing stack.
    g = BagGainSince({ { 2447, 2 } }, { { 2447, 5 } });
    Check(g.itemId == 2447 && g.gained == 3, "a grown stack counts");
    // Nothing new, an item spent meanwhile: no proof.
    g = BagGainSince({ { 2447, 2 }, { 159, 5 } }, { { 2447, 2 }, { 159, 4 } });
    Check(g.gained == 0, "no rise is no loot");
    g = BagGainSince({}, {});
    Check(g.gained == 0 && g.itemId == 0, "empty bags stay empty");
    std::printf("%s: test_gather_bag_proof (%d checks, %d failed)\n", failed ? "FAIL" : "PASS", checks, failed);
    return failed ? 1 : 0;
}
