// Pure unit tests for forever/src/host/TbAutoHunterPetPure.h.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_auto_hunter_pet.cpp -o /tmp/test_auto_hunter_pet && /tmp/test_auto_hunter_pet
#include "forever/src/host/TbAutoHunterPetPure.h"

#include <cstdio>

namespace A = TortoiseBots::autohunterpet;

static int g_checks = 0;
static int g_failed = 0;

static void Check(bool ok, char const* name, int line)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failed;
    std::printf("FAIL %s (line %d)\n", name, line);
}

#define CHECK(name, cond) Check((cond), (name), __LINE__)

static A::Input EligibleHunter()
{
    A::Input in;
    in.enabled = true;
    in.isBot = true;
    in.isHunter = true;
    in.level = 10;
    in.inWorld = true;
    in.teleportingFar = false;
    in.hasLivePet = false;
    in.hasActivePet = false;
    in.hasStabledPet = false;
    return in;
}

int main()
{
    // Level 10+ hunter bot in world with no pet gets a starter pet.
    CHECK("eligible level 10", A::Decide(EligibleHunter()) == A::Verdict::Grant);

    {
        A::Input in = EligibleHunter();
        in.level = 60;
        CHECK("eligible level 60", A::Decide(in) == A::Verdict::Grant);
    }

    // Disabled by config.
    {
        A::Input in = EligibleHunter();
        in.enabled = false;
        CHECK("disabled", A::Decide(in) == A::Verdict::SkipDisabled);
    }

    // Never touch a real player's character or owned alt outside BotMgr.
    {
        A::Input in = EligibleHunter();
        in.isBot = false;
        CHECK("not a bot", A::Decide(in) == A::Verdict::SkipNotBot);
    }

    // Only hunters.
    {
        A::Input in = EligibleHunter();
        in.isHunter = false;
        CHECK("not a hunter", A::Decide(in) == A::Verdict::SkipNotHunter);
    }

    // Level 9 hunter is too low.
    {
        A::Input in = EligibleHunter();
        in.level = 9;
        CHECK("level 9 too low", A::Decide(in) == A::Verdict::SkipLevelTooLow);
    }

    // Not in world or mid far-teleport.
    {
        A::Input in = EligibleHunter();
        in.inWorld = false;
        CHECK("not in world", A::Decide(in) == A::Verdict::SkipNotInWorld);
    }
    {
        A::Input in = EligibleHunter();
        in.teleportingFar = true;
        CHECK("teleporting far", A::Decide(in) == A::Verdict::SkipNotInWorld);
    }

    // Never replace a live pet (e.g. hired hunter or already-seeded pool hunter).
    {
        A::Input in = EligibleHunter();
        in.hasLivePet = true;
        CHECK("has live pet", A::Decide(in) == A::Verdict::SkipHasLivePet);
    }

    // Never replace a dismissed/dead pet in an active stable slot.
    {
        A::Input in = EligibleHunter();
        in.hasActivePet = true;
        CHECK("has active stable pet", A::Decide(in) == A::Verdict::SkipHasStablePet);
    }

    // Never replace a pet stored in a stable-master slot.
    {
        A::Input in = EligibleHunter();
        in.hasStabledPet = true;
        CHECK("has stabled pet", A::Decide(in) == A::Verdict::SkipHasStablePet);
    }

    std::printf("%s: %d checks, %d failed\n", g_failed == 0 ? "PASS" : "FAIL", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
