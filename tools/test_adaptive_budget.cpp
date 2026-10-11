// Standalone regression test for the self-tuning tick budget
// (runtime/AdaptiveBudget.h): the feedback controller behind the per-tick
// pool/combat budgets. Exercises the rules that must not regress — off holds
// the ceilings, sustained over-target ticks shrink multiplicatively toward
// the floor, sustained at-or-under-target ticks grow additively back to the
// ceiling, only readings above target + deadband shrink, a healthy in-target
// reading reclaims (never holds a shrunken budget: the world thread pads
// healthy ticks to ~50 ms), a single spike barely moves the EMA, the
// floor/ceiling clamps hold under long pressure, and a tick kept over target
// by the core (not the pool turns) holds the budgets instead of cutting them.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_adaptive_budget.cpp -o /tmp/test_adaptive_budget
//   /tmp/test_adaptive_budget

#include "../runtime/AdaptiveBudget.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Drive the controller for n ticks at a fixed measured tick time, all of it
// pool turns (the bots alone make the tick: cutting them always helps).
static void Drive(AdaptiveBudget& budget, uint32_t tickMs, int n)
{
    for (int i = 0; i < n; ++i)
        budget.Update(tickMs, tickMs);
}

// Drive with a split tick: poolMs of pool turns, the rest core time.
static void DriveSplit(AdaptiveBudget& budget, uint32_t tickMs, uint32_t poolMs, int n)
{
    for (int i = 0; i < n; ++i)
        budget.Update(tickMs, poolMs);
}

static void TestOffHoldsCeilings()
{
    AdaptiveBudget budget(0, 10000, 15000);
    Drive(budget, 500, 100);
    CHECK(budget.PoolUs() == 10000);
    CHECK(budget.CombatUs() == 15000);
}

static void TestShrinkIsMultiplicativeAndFloors()
{
    AdaptiveBudget budget(50, 10000, 15000);
    Drive(budget, 10, 50);
    CHECK(budget.PoolUs() == 10000);
    // Over target: geometric retreat, ~3 ticks to halve (0.7^3 ~= 0.34).
    Drive(budget, 300, 3);
    CHECK(budget.PoolUs() < 5000);
    uint64_t const afterThree = budget.PoolUs();
    Drive(budget, 300, 1);
    // Each further tick multiplies, not subtracts: the step itself shrinks.
    CHECK(budget.PoolUs() < afterThree);
    CHECK(afterThree - budget.PoolUs() < 10000 - afterThree + 1);
    // Long overload parks at the floor, never at zero (a zero budget would
    // serve nobody while still paying the rotation scan).
    Drive(budget, 500, 200);
    CHECK(budget.PoolUs() == 2000);
    CHECK(budget.CombatUs() == 2000);
}

static void TestGrowIsAdditiveAndCeilings()
{
    AdaptiveBudget budget(50, 100000, 100000);
    Drive(budget, 300, 30);
    CHECK(budget.PoolUs() < 100000);
    CHECK(budget.SmoothedTickMs() > 60.0);
    // The EMA must decay back into the band before reclaim starts: settle it,
    // then two consecutive windows must grow by exactly the step (linear
    // reclaim, no overshoot sawtooth).
    Drive(budget, 10, 60);
    CHECK(budget.SmoothedTickMs() < 40.0);
    uint64_t const a = budget.PoolUs();
    CHECK(a > 2000);
    Drive(budget, 10, 4);
    uint64_t const b = budget.PoolUs();
    CHECK(b - a == 4 * 1000);
    CHECK(b < 100000);
    // Long healthy stretch returns exactly to the operator's ceiling.
    Drive(budget, 10, 100);
    CHECK(budget.PoolUs() == 100000);
    CHECK(budget.CombatUs() == 100000);
}

static void TestDeadbandReclaimsSteady()
{
    AdaptiveBudget budget(50, 100000, 100000);
    Drive(budget, 10, 50);
    CHECK(budget.PoolUs() == 100000);
    // Shrink first so reclaim has something to restore.
    Drive(budget, 300, 10);
    uint64_t const shrunk = budget.PoolUs();
    CHECK(shrunk < 100000);
    // A healthy in-target reading (the world thread pads healthy ticks to
    // ~50 ms) must reclaim, not hold: holding here made every shrink
    // permanent on a healthy server. The EMA lags, so settle it into the
    // reclaim zone first, then pin the exact step.
    while (budget.SmoothedTickMs() > 60.0)
        budget.Update(50, 50);
    uint64_t const settled = budget.PoolUs();
    for (int i = 0; i < 10; ++i)
        budget.Update(50, 50);
    CHECK(budget.PoolUs() == settled + 10 * 1000);
    Drive(budget, 50, 200);
    CHECK(budget.PoolUs() == 100000);
    // Readings above target + 10 shrink again.
    Drive(budget, 300, 10);
    CHECK(budget.PoolUs() < 100000);
}

static void TestSingleSpikeBarelyMovesEma()
{
    AdaptiveBudget budget(50, 10000, 15000);
    Drive(budget, 40, 50);
    double const calm = budget.SmoothedTickMs();
    CHECK(calm < 50.0);
    // One bad tick (a 300 ms hitch on a 40 ms baseline) moves the EMA by 1/8
    // of the gap — just outside the deadband, so the budget shrinks a few
    // steps instead of collapsing. Calm ticks then decay the EMA back into
    // the band and reclaim starts again.
    budget.Update(300, 300);
    CHECK(budget.SmoothedTickMs() < calm + (300.0 - calm) / 8.0 + 1.0);
    uint64_t const afterSpike = budget.PoolUs();
    CHECK(afterSpike < 10000);
    CHECK(afterSpike > 2000);
    Drive(budget, 40, 60);
    CHECK(budget.SmoothedTickMs() < 45.0);
    // Calm ticks reclaim toward the ceiling instead of holding the dip.
    CHECK(budget.PoolUs() > afterSpike);
    Drive(budget, 10, 200);
    CHECK(budget.PoolUs() == 10000);
}

static void TestHealthyTickReclaimsToCeiling()
{
    // A healthy tick (at the target cadence) steadily reclaims: shrink once
    // hard, then healthy ticks must walk the budget back to the ceiling.
    AdaptiveBudget budget(50, 22000, 22000);
    Drive(budget, 10, 100);
    CHECK(budget.PoolUs() == 22000);
    Drive(budget, 300, 5);
    uint64_t const shrunk = budget.PoolUs();
    CHECK(shrunk < 22000);
    while (budget.SmoothedTickMs() > 60.0)
        budget.Update(50, 50);
    uint64_t const settled = budget.PoolUs();
    for (int i = 0; i < 30; ++i)
        budget.Update(50, 50);
    CHECK(budget.PoolUs() > settled);
    Drive(budget, 50, 100);
    CHECK(budget.PoolUs() == 22000);
    CHECK(budget.CombatUs() == 22000);
}

static void TestCoreBoundTickHolds()
{
    // Live shape (yulon, 500 bots, target 100): 30-s windows alternate between
    // a ~102 ms tick and a ~136 ms tick while the bot pass stays ~26 ms, so
    // the extra time is the core's. Cutting the pool cannot get under 110.
    AdaptiveBudget budget(100, 10000, 15000);
    DriveSplit(budget, 102, 26, 100);
    CHECK(budget.PoolUs() == 10000);
    CHECK(budget.CombatUs() == 15000);
    // Bad window: the rest of the tick (~110 ms) plus the 4 ms floor is over
    // the line from the very first tick, so the budgets hold all window long
    // instead of sliding to the floor while the EMAs catch up.
    for (int i = 0; i < 100; ++i)
    {
        budget.Update(136, 26);
        CHECK(budget.PoolUs() == 10000);
        CHECK(budget.CombatUs() == 15000);
    }
    CHECK(budget.SmoothedTickMs() > 110.0);
    CHECK(budget.SmoothedRestMs() > 106.0);
    CHECK(budget.Held());
    // Back to the good window: the smoothed tick decays through the line
    // without a cut (this tick is under the line), then reclaim resumes.
    for (int i = 0; i < 100; ++i)
    {
        budget.Update(102, 26);
        CHECK(budget.PoolUs() == 10000);
    }
    CHECK(!budget.Held());
}

static void TestCoreBoundHoldsAShrunkBudget()
{
    // Hold means hold: a budget already cut by the bots stays where it is
    // while the core keeps the tick over target (no further cut, no growth
    // that would only add to an over-target tick).
    AdaptiveBudget budget(100, 10000, 15000);
    DriveSplit(budget, 200, 150, 2);
    uint64_t const shrunk = budget.PoolUs();
    CHECK(shrunk < 10000);
    CHECK(shrunk > 2000);
    DriveSplit(budget, 140, 10, 100);
    CHECK(budget.PoolUs() == shrunk);
    CHECK(budget.Held());
    // Core load gone: reclaim back to the ceiling.
    DriveSplit(budget, 60, 20, 200);
    CHECK(budget.PoolUs() == 10000);
    CHECK(budget.CombatUs() == 15000);
}

static void TestPoolOverloadOnSlowCoreStillShrinks()
{
    // Core 80 ms is under the line on its own; 60 ms of pool turns push the
    // tick to 140. Cutting the pool can reach the target, so it shrinks.
    AdaptiveBudget budget(100, 10000, 15000);
    DriveSplit(budget, 140, 60, 20);
    CHECK(budget.PoolUs() == 2000);
    CHECK(budget.CombatUs() == 2000);
    CHECK(!budget.Held());
}

static void TestPoolSpikeOnBusyCoreStillShrinks()
{
    // A single slow pass (120 ms of pool turns) on a ~75 ms core: the bots
    // caused it, so it still cuts, but only on the tick that was over.
    AdaptiveBudget budget(100, 10000, 15000);
    DriveSplit(budget, 102, 27, 100);
    CHECK(budget.PoolUs() == 10000);
    budget.Update(200, 120);
    uint64_t const afterSpike = budget.PoolUs();
    CHECK(afterSpike < 10000);
    // The EMA tail of the spike does not keep cutting (those ticks are under
    // the line), and reclaim returns the ceiling once the EMA is back.
    DriveSplit(budget, 102, 27, 3);
    CHECK(budget.PoolUs() >= afterSpike);
    DriveSplit(budget, 102, 27, 100);
    CHECK(budget.PoolUs() == 10000);
}

int main()
{
    TestOffHoldsCeilings();
    TestShrinkIsMultiplicativeAndFloors();
    TestGrowIsAdditiveAndCeilings();
    TestDeadbandReclaimsSteady();
    TestSingleSpikeBarelyMovesEma();
    TestHealthyTickReclaimsToCeiling();
    TestCoreBoundTickHolds();
    TestCoreBoundHoldsAShrunkBudget();
    TestPoolOverloadOnSlowCoreStillShrinks();
    TestPoolSpikeOnBusyCoreStillShrinks();
    std::printf("adaptive budget: all %d checks passed\n", checks);
    return 0;
}
