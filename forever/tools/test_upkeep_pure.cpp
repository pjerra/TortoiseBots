// Core-free unit tests for forever/src/host/player/TbUpkeepPure.h.
// Build & run: g++ -std=c++17 -I. -Wall -Wextra -Werror forever/tools/test_upkeep_pure.cpp -o /tmp/test_upkeep && /tmp/test_upkeep

#include "forever/src/host/player/TbUpkeepPure.h"

#include <cstdio>

namespace
{

int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            std::printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

} // namespace

int main()
{
    using namespace TortoiseBots::upkeep;

    // 1. The step order is TB's maintenance order: reagents, potions, food,
    // consumes, bandages, ammo, thrown, repair, save.
    {
        CHECK(kStepCount == 9, "nine steps");
        CHECK(kSteps[0] == Step::Reagents, "reagents first");
        CHECK(kSteps[1] == Step::Potions, "potions second");
        CHECK(kSteps[2] == Step::Food, "food third");
        CHECK(kSteps[3] == Step::Consumes, "consumes fourth");
        CHECK(kSteps[4] == Step::Bandages, "bandages fifth");
        CHECK(kSteps[5] == Step::Ammo, "ammo sixth");
        CHECK(kSteps[6] == Step::Thrown, "thrown seventh");
        CHECK(kSteps[7] == Step::Repair, "repair eighth");
        CHECK(kSteps[8] == Step::Save, "save last");
    }

    // 2. The Forever subset: reagents, bandages, ammo, repair and save run;
    // potions, food and consumes need the unported item caches and thrown
    // only ever served pool bots with the item cheat.
    {
        CHECK(RunsOnForever(Step::Reagents), "reagents run");
        CHECK(!RunsOnForever(Step::Potions), "potions skipped");
        CHECK(!RunsOnForever(Step::Food), "food skipped");
        CHECK(!RunsOnForever(Step::Consumes), "consumes skipped");
        CHECK(RunsOnForever(Step::Bandages), "bandages run");
        CHECK(RunsOnForever(Step::Ammo), "ammo runs");
        CHECK(!RunsOnForever(Step::Thrown), "thrown skipped");
        CHECK(RunsOnForever(Step::Repair), "repair runs");
        CHECK(RunsOnForever(Step::Save), "save runs");
    }

    // 3. A healthy owned bot with the flag on is allowed.
    {
        Input in;
        in.enabled = true;
        in.poolBot = false;
        in.alive = true;
        CHECK(Decide(in) == Refusal::None, "healthy owned bot allowed");
        CHECK(CodeOf(Refusal::None)[0] == '\0', "allowed has no code");
    }

    // 4. Pool bots are refused even with the flag on and alive.
    {
        Input in;
        in.enabled = true;
        in.poolBot = true;
        in.alive = true;
        CHECK(Decide(in) == Refusal::Pool, "pool refused");
    }

    // 5. A disabled flag refuses an owned, alive bot.
    {
        Input in;
        in.enabled = false;
        in.poolBot = false;
        in.alive = true;
        CHECK(Decide(in) == Refusal::Disabled, "flag off refused");
    }

    // 6. A dead bot is refused even with the flag on.
    {
        Input in;
        in.enabled = true;
        in.poolBot = false;
        in.alive = false;
        CHECK(Decide(in) == Refusal::Dead, "dead refused");
    }

    // 7. Refusal order: pool first, then the flag, then alive (TB checks
    // eligibility, then the flag, then life).
    {
        Input in;
        in.enabled = false;
        in.poolBot = true;
        in.alive = false;
        CHECK(Decide(in) == Refusal::Pool, "pool beats flag and death");
        in.poolBot = false;
        CHECK(Decide(in) == Refusal::Disabled, "flag beats death");
    }

    if (g_failures == 0)
        std::printf("PASS: upkeep pure (%d checks)\n", 30);
    return g_failures == 0 ? 0 : 1;
}
