// Standalone test of the tick budget's core-free half: the feedback controller
// (runtime/AdaptiveBudget.h, the self-tuning cut-off) and the pool pass order
// (forever/src/host/TbTickBudgetPure.h: the first bot a pass cuts off runs
// first next pass, so no bot waits two rounds).
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_tick_budget.cpp -o /tmp/test_tick_budget && /tmp/test_tick_budget
#include "forever/src/host/TbTickBudgetPure.h"
#include "runtime/AdaptiveBudget.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace TortoiseBots;

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// Drive the controller at a fixed measured tick, all of it pool turns.
static void Drive(AdaptiveBudget& budget, uint32_t tickMs, int n)
{
    for (int i = 0; i < n; ++i)
        budget.Update(tickMs, tickMs);
}

static void TestBudgetGate()
{
    // 0 in either ceiling or a previous tick under the gate means no cut.
    CHECK(tickbudget::PassIsBudgeted(37000, 0, 1));
    CHECK(tickbudget::PassIsBudgeted(37000, 150, 200));
    CHECK(!tickbudget::PassIsBudgeted(37000, 150, 150));
    CHECK(!tickbudget::PassIsBudgeted(37000, 150, 100));
    CHECK(!tickbudget::PassIsBudgeted(0, 0, 200));
}

static void TestRotation()
{
    std::vector<uint32_t> keys{ 1, 2, 3, 4, 5 };
    // A key that is gone (logged out, or a master bound it) leaves the order alone.
    tickbudget::RotateToFirst(keys, uint32_t(0));
    CHECK((keys == std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
    tickbudget::RotateToFirst(keys, uint32_t(9));
    CHECK((keys == std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
    // The cut bot moves to the front; the ones before it follow in their old order.
    tickbudget::RotateToFirst(keys, uint32_t(3));
    CHECK((keys == std::vector<uint32_t>{ 3, 4, 5, 1, 2 }));
    tickbudget::RotateToFirst(keys, uint32_t(3));
    CHECK((keys == std::vector<uint32_t>{ 3, 4, 5, 1, 2 }));
    // A rotation that wraps every bot is still the same order.
    tickbudget::RotateToFirst(keys, uint32_t(4));
    CHECK((keys == std::vector<uint32_t>{ 4, 5, 1, 2, 3 }));
}

static void TestFairRounds()
{
    // The pass order is rebuilt from the bot map each tick and then rotated to
    // the bot the last pass cut, so simulate that: ten bots, three turns a
    // pass, bot 4 is the first key the pass did not start, and bot 4 must be
    // the first turn of the next pass. Four passes serve every bot at least
    // once; the two that got a second turn are the ones the rotation wrapped
    // onto, not duplicates of one bot.
    std::vector<uint32_t> const base{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    std::vector<uint32_t> order = base;
    std::vector<int> served(11, 0);
    uint32_t next = 0;
    for (int pass = 0; pass < 4; ++pass)
    {
        order = base;
        tickbudget::RotateToFirst(order, next);
        if (next)
            CHECK(order.front() == next);
        for (int i = 0; i < 3; ++i)
            ++served[order[i]];
        next = order[3];
    }
    for (int bot = 1; bot <= 10; ++bot)
        CHECK(served[bot] >= 1);
    CHECK(served[1] == 2 && served[2] == 2);
    CHECK(served[3] == 1 && served[10] == 1);
    CHECK(next == 3);

    // A tight budget (one turn a pass) still reaches every bot in one round.
    std::vector<int> once(11, 0);
    next = 0;
    for (int pass = 0; pass < 10; ++pass)
    {
        order = base;
        tickbudget::RotateToFirst(order, next);
        if (next)
            CHECK(order.front() == next);
        ++once[order.front()];
        next = order[1];
    }
    for (int bot = 1; bot <= 10; ++bot)
        CHECK(once[bot] == 1);
}

static void TestOffHoldsCeilings()
{
    // AiPlayerbot.TargetWorldTickMs = 0: the static path, no cut.
    AdaptiveBudget budget(0, 22000, 15000);
    Drive(budget, 500, 100);
    CHECK(budget.PoolUs() == 22000);
    CHECK(budget.CombatUs() == 15000);
    CHECK(!budget.Held());
}

static void TestPoolOverloadShrinksAndFloors()
{
    AdaptiveBudget budget(100, 22000, 15000);
    // Bot work alone keeps the tick over the line: shrink toward the floor.
    Drive(budget, 200, 100);
    CHECK(budget.PoolUs() == 2000);
    CHECK(budget.CombatUs() == 2000);
    CHECK(!budget.Held());
}

static void TestHealthyTickReclaims()
{
    // ForeverCore's world loop runs near 1 ms: a healthy pass must not pin a
    // shrunken budget, it must walk back to the operator's ceilings.
    AdaptiveBudget budget(100, 22000, 15000);
    Drive(budget, 200, 5);
    uint64_t const shrunk = budget.PoolUs();
    CHECK(shrunk < 22000);
    Drive(budget, 1, 300);
    CHECK(budget.PoolUs() == 22000);
    CHECK(budget.CombatUs() == 15000);
}

static void TestCoreBoundTickHolds()
{
    // The tick is over target but the pool turns are not what keeps it there:
    // cutting the pool cannot help, so the budgets hold.
    AdaptiveBudget budget(100, 22000, 15000);
    for (int i = 0; i < 100; ++i)
        budget.Update(140, 5);
    CHECK(budget.PoolUs() == 22000);
    CHECK(budget.CombatUs() == 15000);
    CHECK(budget.Held());
}

int main()
{
    TestBudgetGate();
    TestRotation();
    TestFairRounds();
    TestOffHoldsCeilings();
    TestPoolOverloadShrinksAndFloors();
    TestHealthyTickReclaims();
    TestCoreBoundTickHolds();
    std::printf("tick budget: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
