// Pure decision rules for opt-in Holy Paladin melee-weaving
// (AiPlayerbot.HolyPaladinMeleeWeave): when a Holy Paladin may walk into
// melee and stay there.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_holy_melee_weave_policy.cpp -o /tmp/test_holy_melee_weave
//   /tmp/test_holy_melee_weave

#include "../runtime/policy/class/HolyMeleeWeavePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

#define CHECK(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "Assertion failed at line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while (0)

static HolyMeleeWeaveInput BaseInput()
{
    HolyMeleeWeaveInput in;
    in.enabled = true;
    in.isHolyPaladin = true;
    in.inGroup = true;
    in.hasTarget = true;
    in.targetAlive = true;
    in.targetIsPlayer = false;
    in.inRaid = false;
    in.targetEngaged = true;
    in.selfHealthPct = 100;
    in.partyMemberToHealPct = 95;
    in.mediumHealthPct = 70;
    return in;
}

static void TestWeaveAllowed()
{
    CHECK(HolyMeleeWeaveAllowed(BaseInput()));

    // Toggle off -> never allowed.
    {
        auto in = BaseInput();
        in.enabled = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Solo -> never allowed.
    {
        auto in = BaseInput();
        in.inGroup = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Non-holy or non-paladin -> never allowed.
    {
        auto in = BaseInput();
        in.isHolyPaladin = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // Raid, PvP player, unengaged/dead/missing target -> never allowed.
    {
        auto in = BaseInput();
        in.inRaid = true;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetIsPlayer = true;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetEngaged = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.targetAlive = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.hasTarget = false;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // At or above the medium-health line allows the weave; below it on self or
    // a party member suspends it (healing comes first).
    {
        auto in = BaseInput();
        in.partyMemberToHealPct = 70;
        CHECK(HolyMeleeWeaveAllowed(in));
        in.partyMemberToHealPct = 69;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }
    {
        auto in = BaseInput();
        in.selfHealthPct = 70;
        CHECK(HolyMeleeWeaveAllowed(in));
        in.selfHealthPct = 69;
        CHECK(!HolyMeleeWeaveAllowed(in));
    }

    // The line comes from the configured MediumHealth, not a fixed 70.
    {
        auto in = BaseInput();
        in.mediumHealthPct = 80;
        in.partyMemberToHealPct = 75;
        CHECK(!HolyMeleeWeaveAllowed(in));
        in.partyMemberToHealPct = 80;
        CHECK(HolyMeleeWeaveAllowed(in));
    }
}

int main()
{
    TestWeaveAllowed();
    std::printf("holy melee weave policy: all assertions passed\n");
    return 0;
}
