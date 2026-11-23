// Pure unit tests for the host fishing errand phase walk (TbHostFishingPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_fishing_host.cpp -o /tmp/test_fishing_host && /tmp/test_fishing_host

#include "forever/src/host/TbHostFishingPure.h"

#include <cstdio>
#include <string>

using namespace tbhost::fishing;

static int g_failures = 0;
#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (!(cond))                                                                \
        {                                                                           \
            ++g_failures;                                                           \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);                   \
        }                                                                           \
    } while (0)

static Facts At(Phase phase)
{
    Facts f;
    f.phase = phase;
    f.hasSpot = true;
    f.distanceToSpot = 20.0f;
    return f;
}

int main()
{
    // Hard stops come first: no world, dead, combat, no spot.
    {
        Facts f = At(Phase::Walking);
        f.inWorld = false;
        CHECK(Step(f).action == Action::Fail);
        f.inWorld = true;
        f.alive = false;
        CHECK(Step(f).action == Action::Fail);
        f.alive = true;
        f.inCombat = true;
        CHECK(Step(f).action == Action::Fail);
        f.inCombat = false;
        f.hasSpot = false;
        Decision const d = Step(f);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "no-water");
    }

    // Walking: far away asks for a move, near switches to the cast, and a
    // bot that cannot get there fails after a minute.
    {
        Facts f = At(Phase::Walking);
        Decision d = Step(f);
        CHECK(d.action == Action::MoveToSpot);
        CHECK(d.next == Phase::Walking);
        f.distanceToSpot = 5.9f;
        d = Step(f);
        CHECK(d.action == Action::None);
        CHECK(d.next == Phase::Casting);
        f.distanceToSpot = 20.0f;
        f.phaseMs = 240001;
        d = Step(f);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "unreachable");
    }

    // Casting sends the cast; the wait follows the channel or the bobber.
    {
        Facts f = At(Phase::Casting);
        Decision d = Step(f);
        CHECK(d.action == Action::Cast);
        CHECK(d.next == Phase::CastingWait);

        f = At(Phase::CastingWait);
        f.castMs = 500;
        CHECK(Step(f).action == Action::None);
        f.channelingFishing = true;
        d = Step(f);
        CHECK(d.action == Action::None);
        CHECK(d.next == Phase::BobberWait);
        f.channelingFishing = false;
        f.hasBobber = true;
        d = Step(f);
        CHECK(d.action == Action::None);
        CHECK(d.next == Phase::BobberWait);
    }

    // A cast that never shows a channel or a bobber retries twice, then fails.
    {
        Facts f = At(Phase::CastingWait);
        f.castMs = 12001;
        f.casts = 1;
        Decision d = Step(f);
        CHECK(d.action == Action::Cast);
        f.casts = 3;
        d = Step(f);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "no-cast");
    }

    // Bobber wait: the splash is used once, the loot is taken after it.
    {
        Facts f = At(Phase::BobberWait);
        f.hasBobber = true;
        f.castMs = 6000;
        f.casts = 1;
        CHECK(Step(f).action == Action::None);
        f.bobberReady = true;
        Decision d = Step(f);
        CHECK(d.action == Action::UseBobber);
        CHECK(d.next == Phase::BobberWait);
        f.lootAvailable = true;
        d = Step(f);
        CHECK(d.action == Action::TakeLoot);
        CHECK(d.next == Phase::Looting);
    }

    // A bobber used with no catch retries after a short wait.
    {
        Facts f = At(Phase::BobberWait);
        f.hasBobber = true;
        f.bobberUsed = true;
        f.castMs = 4001;
        f.casts = 1;
        Decision d = Step(f);
        CHECK(d.action == Action::Cast);
        f.casts = 3;
        d = Step(f);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "no-catch");
    }

    // A bobber that never splashed and never despawned does not wedge the
    // errand: after the channel is long over, the cast is retried.
    {
        Facts f = At(Phase::BobberWait);
        f.hasBobber = true;
        f.castMs = 40001;
        f.casts = 1;
        Decision d = Step(f);
        CHECK(d.action == Action::Cast);
        f.casts = 3;
        d = Step(f);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "no-catch");
    }

    // Looting finishes once an item reached the bags.
    {
        Facts f = At(Phase::Looting);
        f.lootAvailable = true;
        CHECK(Step(f).action == Action::TakeLoot);
        f.lootAvailable = false;
        f.itemsGained = 1;
        Decision d = Step(f);
        CHECK(d.action == Action::Done);
        CHECK(d.next == Phase::Idle);

        // Loot that will not fit after two takes stops the errand.
        Facts full = At(Phase::Looting);
        full.lootAvailable = true;
        full.lootTries = 2;
        d = Step(full);
        CHECK(d.action == Action::Fail);
        CHECK(d.fail && std::string(d.fail) == "bags-full");
    }

    if (g_failures == 0)
        std::printf("PASS: test_fishing_host\n");
    else
        std::printf("FAIL: test_fishing_host (%d failed)\n", g_failures);
    return g_failures ? 1 : 0;
}
