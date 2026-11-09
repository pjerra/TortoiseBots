// Standalone check for ai/playerbot/ThreatCapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_threat_cap_policy.cpp -o /tmp/test_threat_cap_policy
//   /tmp/test_threat_cap_policy
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cstdlib>
#include <iostream>

// The case a failing CHECK belongs to, printed with it (set by CASE).
static const char* g_case = "(unnamed)";
#define CASE(name) (g_case = (name))

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << " in " << g_case << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots threat cap policy tests...\n";

    // ThreatCapIsTheHighestTankByDefault: without a `threat_successor` row the reference is the highest tank, whatever
    // the target's taunt immunity. Tanks {1000, 600}: 1000, not 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}, {400.0f, true, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({}, false) == -1.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {600.0f, true, true}}, false).offTank);

    // ThreatCapsAtTheAssignedOffTank: on a target whose boss data asks for a successor (Hakkar) the damage dealers stay
    // below the assigned off-tank so a Cause Insanity threat reset hands the boss to it. Main 1000, off-tank 600: 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, true) == 600.0f);
    // An off-tank already above the main tank stands at its own threat: the cap is the highest off-tank, not always
    // the second number.
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}, {400.0f, true, true}}, true) == 600.0f);
    // Two off-tanks, the higher of them stands.
    CHECK(ThreatCapReference({{1000.0f, false}, {400.0f, true, true}, {700.0f, true, true}}, true) == 700.0f);

    // TauntImmuneSuccessorIgnoresHalfFloor: the floor is absolute (1 % of the highest tank, never below 100 threat),
    // not half of the main tank's threat. Tanks at 1000 and 300 cap at the off-tank's 300 — the old 50 % floor sat the
    // cap at 1000 and Cause Insanity then deleted the main tank's threat, moving the boss to the top damage dealer.
    CHECK(kThreatCapOffTankMinShare == 0.01f);
    CHECK(kThreatCapOffTankMinThreat == 100.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {300.0f, true, true}}, true) == 300.0f);
    CHECK(ThreatCapPickReference({{1000.0f, false}, {300.0f, true, true}}, true).offTank);
    // Well above the floor: unchanged by the new floor.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true, true}}, true) == 600.0f);

    // ThreatCapIgnoresATinyOffTankThreat: an off-tank below the absolute floor is not the successor — capping the
    // damage dealers there would sit the cap at almost nothing and stop all damage, so the highest tank's threat
    // stands instead. A 50 off-tank on a 1000 main tank is below 100; 101 is above it.
    CHECK(ThreatCapReference({{1000.0f, false}, {50.0f, true, true}}, true) == 1000.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {50.0f, true, true}}, true).offTank);
    CHECK(ThreatCapReference({{1000.0f, false}, {101.0f, true, true}}, true) == 101.0f);
    // A charmed main tank whose threat is gone leaves the off-tank's 600 as the reference.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true, true}}, true) == 600.0f);
    // The 1 % share wins over the fixed 100 on a huge main-tank lead: 2 000 000 * 1 % = 20 000.
    CHECK(ThreatCapReference({{2000000.0f, false}, {15000.0f, true, true}}, true) == 2000000.0f);
    CHECK(ThreatCapReference({{2000000.0f, false}, {25000.0f, true, true}}, true) == 25000.0f);

    // ThreatCapWithoutAZeroReference: an off-tank with no threat on the target (dead, not yet engaged, or its threat
    // deleted) never becomes the reference: a zero reads as "the tank has no threat" and vetoes every damage action
    // (Hakkar's Cause Insanity zeroing the main tank was the bug). The highest tank stands instead.
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true, true}, {0.0f, true, true}}, true) == 1000.0f);
    // The main tank is charmed and its threat gone; the off-tank still stands as the successor.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true, true}}, true) == 600.0f);

    // ThreatCapWithoutAnAssignedOffTank: a lone tank (or none of the assigned off-tanks on the target) leaves the
    // highest tank as the reference, never a zero.
    CHECK(ThreatCapReference({{1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{900.0f, false}, {1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({}, true) == -1.0f);

    // ThreatCapFallsBackWhileTheOffTanksAreAway: Hakkar's off-tanks are away on Sons (not on the boss), so their threat
    // on the boss is the leftover of an earlier hold. The 1 % reference would freeze every damage dealer at 2000 while
    // the main tank holds 40000: while no off-tank has the boss as its victim the reference is the main tank's again
    // (the old 50 % floor). With an off-tank on the boss the successor rule stands.
    CHECK(kThreatCapOffTankAwayShare == 0.5f);
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, false}}, true) == 40000.0f);
    CHECK(!ThreatCapPickReference({{40000.0f, false}, {2000.0f, true, false}}, true).offTank);
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, true}}, true) == 2000.0f);
    CHECK(ThreatCapPickReference({{40000.0f, false}, {2000.0f, true, true}}, true).offTank);
    // Away but still above the old 50 % floor: that off-tank is the cap, as before the 1 % rule.
    CHECK(ThreatCapReference({{40000.0f, false}, {25000.0f, true, false}}, true) == 25000.0f);
    // One of two off-tanks on the boss: the successor rule over both.
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, true}, {900.0f, true, false}}, true) == 2000.0f);
    // The cap is only for the successor target: the plain max ignores the flag.
    CHECK(ThreatCapReference({{40000.0f, false}, {2000.0f, true, false}}, false) == 40000.0f);


    // AiPlayerbot.ThreatCapCountsCastThreat (ThreatCapCastOvershoots): the cast's own expected threat counts.
    CASE("fireball_from_79_pct_vetoed");
    // 79 % now, a cast worth 25 % of the tank's threat: 104 % when it lands. The 80 % high-threat line lets it
    // through today; the cast cap holds it.
    CHECK(ThreatCapCastOvershoots(true, 79.0f, 25.0f, 100.0f));
    CHECK(ThreatCapCastOvershoots(true, 790.0f, 450.0f, 1000.0f));
    CASE("cast_landing_exactly_on_the_tank_vetoed");
    // 100 % is the line itself (the core's aggro point before the pull margin): equal is held.
    CHECK(ThreatCapCastOvershoots(true, 75.0f, 25.0f, 100.0f));
    CHECK(!ThreatCapCastOvershoots(true, 74.0f, 25.0f, 100.0f));
    CASE("cast_under_the_line_passes");
    CHECK(!ThreatCapCastOvershoots(true, 50.0f, 25.0f, 100.0f));
    CHECK(!ThreatCapCastOvershoots(true, 500.0f, 280.0f, 1000.0f));
    CASE("key_off_never_vetoes");
    CHECK(!ThreatCapCastOvershoots(false, 79.0f, 25.0f, 100.0f));
    CHECK(!ThreatCapCastOvershoots(false, 200.0f, 25.0f, 100.0f));
    CASE("no_tank_threat_left_to_old_veto");
    CHECK(!ThreatCapCastOvershoots(true, 79.0f, 25.0f, 0.0f));
    CHECK(!ThreatCapCastOvershoots(true, 79.0f, 25.0f, -1.0f));
    CASE("no_estimate_left_to_old_veto");
    // A cast without direct damage (no estimate) is the old veto's business, even at or over the tank.
    CHECK(!ThreatCapCastOvershoots(true, 120.0f, 0.0f, 100.0f));

    // ThreatCapAverageEffectPoints: the core's CalculateSpellDamage without the roll.
    CASE("average_points_is_the_middle_of_the_roll");
    // Fireball-like row: base 392, dice 1, sides 107 -> roll 1..107, average 392 + 54 = 446 (not the max 499).
    CHECK(ThreatCapAverageEffectPoints(392, 1, 107, 0.0f, 0.0f, 0) == 446.0f);
    // No roll range (sides 0 or 1): base + dice, as the core's `case 0: case 1:`.
    CHECK(ThreatCapAverageEffectPoints(99, 1, 1, 0.0f, 0.0f, 0) == 100.0f);
    CHECK(ThreatCapAverageEffectPoints(99, 1, 0, 0.0f, 0.0f, 0) == 100.0f);
    // Level growth: 4 levels at 0.5 per level adds 2 to the base and 4 levels at 1 per level widens the roll by 4.
    CHECK(ThreatCapAverageEffectPoints(100, 1, 9, 0.5f, 1.0f, 4) == 102.0f + (1.0f + 13.0f) / 2.0f);

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
