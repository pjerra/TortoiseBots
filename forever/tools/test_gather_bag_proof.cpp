// Standalone test of the gather proof by bag snapshots (ai/playerbot/policy/profession/GatherNodePolicy.h, BagGainSince):
//   g++ -std=c++17 -I. -Iai forever/tools/test_gather_bag_proof.cpp -o /tmp/t && /tmp/t
#include "playerbot/policy/profession/GatherNodePolicy.h"
#include <cstdio>
#include <string>

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

    // The errand: the live case (Silverleaf in the bags, no loot window, node still "ready", 1.5 s after the cast).
    GatherErrandFacts f;
    f.phase = GatherPhase::Casting;
    f.inWorld = f.alive = f.nodeExists = f.nodeReady = true;
    f.phaseElapsedMs = 1600;
    GatherErrandStep st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishFail && std::string(st.failCode) == "timeout", "no window, no bag rise: still a timeout");
    f.bagItemsGained = 3;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishOk && st.nextPhase == GatherPhase::Done, "bag rise after the cast: ok, not timeout");
    // The node despawned after the core stored the herbs: ok, not node-gone.
    f.nodeExists = f.nodeReady = false;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishOk, "bag rise beats node-gone");
    // A loot window still goes the looting way first.
    f.nodeExists = f.nodeReady = true;
    f.lootAvailable = true;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::LootNode && st.nextPhase == GatherPhase::Looting, "loot window first");
    // Looting: the window closed and the bags rose.
    f.phase = GatherPhase::Looting;
    f.lootAvailable = false;
    f.nodeExists = false;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishOk, "looting: bag rise is ok");
    f.bagItemsGained = 0;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishFail && std::string(st.failCode) == "node-gone", "looting: no rise, node gone: fail");
    // Combat still wins.
    f.phase = GatherPhase::Casting;
    f.bagItemsGained = 3;
    f.inCombat = true;
    st = StepGatherErrand(f);
    Check(st.action == GatherErrandAction::FinishFail && std::string(st.failCode) == "danger", "combat still fails the errand");

    std::printf("%s: test_gather_bag_proof (%d checks, %d failed)\n", failed ? "FAIL" : "PASS", checks, failed);
    return failed ? 1 : 0;
}
