// Pure unit tests for the host fishing errand phase walk (TbHostFishingPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_fishing_host.cpp -o /tmp/test_fishing_host && /tmp/test_fishing_host

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

    // Idle session budget: one session per hour, 5 minutes or 5 casts.
    {
        CHECK(IdleSessionStartable(0, 0, 0, 1000));
        CHECK(!IdleSessionStartable(0, 1000, 0, 1000 + 3600000 - 1));
        CHECK(IdleSessionStartable(0, 1000, 0, 1000 + 3600000));
        CHECK(!IdleSessionOver(1000, 0, 1000 + 300000 - 1));
        CHECK(IdleSessionOver(1000, 0, 1000 + 300000));
        CHECK(IdleSessionOver(1000, 5, 1000));
    }

    // A failed search parks for 15 minutes; a running one keeps 15 s.
    {
        CHECK(IdleSearchAllowed(0, 0, 1000));
        CHECK(!IdleSearchAllowed(1000, 0, 1000 + 15000 - 1));
        CHECK(IdleSearchAllowed(1000, 0, 1000 + 15000));
        CHECK(!IdleSearchAllowed(0, 1000, 1000 + 900000 - 1));
        CHECK(IdleSearchAllowed(0, 1000, 1000 + 900000));
    }

    // A hole wins over open water; a guarded stand drops either pick.
    {
        CHECK(IdleSpotChosen(true, false, false));
        CHECK(IdleSpotChosen(false, true, false));
        CHECK(!IdleSpotChosen(false, false, false));
        CHECK(!IdleSpotChosen(true, false, true));
        CHECK(!IdleSpotChosen(false, true, true));
    }

    // Open-water fallback geometry the host search uses.
    {
        CHECK(ai::FishingCastInRange(10.0f));
        CHECK(ai::FishingCastInRange(20.0f));
        CHECK(!ai::FishingCastInRange(9.9f));
        CHECK(!ai::FishingCastInRange(20.1f));
        CHECK(ai::FishingWaterDeepEnough(1.0f, 0.4f));
        CHECK(!ai::FishingWaterDeepEnough(1.0f, 1.0f));
        CHECK(ai::FishingStandDry(false, true));
        CHECK(!ai::FishingStandDry(true, true));
        CHECK(ai::FishingZoneSkillOk(-20, 0));
        CHECK(ai::FishingZoneSkillOk(75, 70));
        CHECK(!ai::FishingZoneSkillOk(80, 70));
    }

    if (g_failures == 0)
        std::printf("PASS: test_fishing_host\n");
    else
        std::printf("FAIL: test_fishing_host (%d failed)\n", g_failures);
    return g_failures ? 1 : 0;
}
