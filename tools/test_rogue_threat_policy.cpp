// Standalone check for runtime/RogueThreatPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_rogue_threat_policy.cpp -o /tmp/test_rogue_threat && /tmp/test_rogue_threat
#include "../runtime/RogueThreatPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

static RogueThreatInputs dungeon()
{
    RogueThreatInputs in;
    in.toggleOn = true;
    in.inFiveManDungeon = true;
    in.grouped = true;
    in.otherTankInFight = true;
    in.tankHoldsMob = true;
    in.victimIsTankOrRogue = true;
    return in;
}

int main()
{
    std::cout << "Starting TortoiseBots rogue-threat tests...\n";

    // Feint: medium threat in a grouped dungeon with a tank.
    {
        RogueThreatInputs in = dungeon();
        CHECK(!ShouldRogueDungeonFeint(in)); // no threat yet
        in.mediumThreat = true;
        CHECK(ShouldRogueDungeonFeint(in));
        in.highThreat = true;
        CHECK(ShouldRogueDungeonFeint(in));
    }
    std::cout << "  [PASS] feint on medium threat\n";

    // Vanish: high threat only.
    {
        RogueThreatInputs in = dungeon();
        in.mediumThreat = true;
        CHECK(!ShouldRogueDungeonVanish(in));
        in.highThreat = true;
        CHECK(ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] vanish only on high threat\n";

    // Feint needs a living tank or party pet to take the threat (#915).
    CHECK(CanRogueFeintShedThreat(true, true, false));
    CHECK(CanRogueFeintShedThreat(true, false, true));
    CHECK(!CanRogueFeintShedThreat(true, false, false)); // tank just died
    CHECK(!CanRogueFeintShedThreat(false, true, true));  // solo
    std::cout << "  [PASS] feint needs a living tank or pet\n";

    // Toggle off: nothing fires.
    {
        RogueThreatInputs in = dungeon();
        in.toggleOn = false;
        in.mediumThreat = in.highThreat = true;
        CHECK(!ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] toggle off changes nothing\n";

    // Outside a 5-man dungeon (open world, raid): not this feature.
    {
        RogueThreatInputs in = dungeon();
        in.inFiveManDungeon = false;
        in.mediumThreat = in.highThreat = true;
        CHECK(!ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] only in 5-man dungeons\n";

    // Solo fight: never Vanish (it would reset the fight), never Feint either.
    {
        RogueThreatInputs in = dungeon();
        in.grouped = false;
        in.mediumThreat = in.highThreat = true;
        CHECK(!ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] solo fights are left alone\n";

    // No other tank alive and fighting: nobody to take the mob over.
    {
        RogueThreatInputs in = dungeon();
        in.otherTankInFight = false;
        in.mediumThreat = in.highThreat = true;
        CHECK(!ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] needs a living tank in the fight\n";

    // Vanish needs the mob on the tank or on the rogue (not on a healer or pet).
    {
        RogueThreatInputs in = dungeon();
        in.mediumThreat = in.highThreat = true;
        in.victimIsTankOrRogue = false;
        CHECK(ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] vanish only when the mob is on the tank or the rogue\n";

    // A loose add the tank never touched (tank threat 0, mob not on the tank):
    // the threat value still reads high, but neither Feint nor Vanish may fire.
    {
        RogueThreatInputs in = dungeon();
        in.mediumThreat = in.highThreat = true;
        in.tankHoldsMob = false;
        CHECK(!ShouldRogueDungeonFeint(in));
        CHECK(!ShouldRogueDungeonVanish(in));
        in.victimIsTankOrRogue = true; // the add is on the rogue itself
        CHECK(!ShouldRogueDungeonVanish(in));
    }
    std::cout << "  [PASS] a mob the tank never touched is left alone\n";

    std::cout << "All rogue-threat tests passed.\n";
    return 0;
}
