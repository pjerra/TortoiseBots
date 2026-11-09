// Standalone check for ai/playerbot/ThreatCapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_threat_cap_policy.cpp -o /tmp/test_threat_cap_policy
//   /tmp/test_threat_cap_policy
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cmath>
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

// The normal-hit rule (a) alone: a spell crit multiplier with no crit chance, so rule (b) never fires.
static bool NormalOvershoots(bool key, float botThreat, float castThreat, float tankThreat)
{
    return ThreatCapCastOvershoots(key, botThreat, castThreat, tankThreat, 1.5f, kThreatCapCastRangedPullLine, false);
}

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
    CHECK(NormalOvershoots(true, 79.0f, 25.0f, 100.0f));
    CHECK(NormalOvershoots(true, 790.0f, 450.0f, 1000.0f));
    CASE("cast_landing_exactly_on_the_tank_vetoed");
    // 100 % is the line itself (the core's aggro point before the pull margin): equal is held.
    CHECK(NormalOvershoots(true, 75.0f, 25.0f, 100.0f));
    CHECK(!NormalOvershoots(true, 74.0f, 25.0f, 100.0f));
    CASE("cast_under_the_line_passes");
    CHECK(!NormalOvershoots(true, 50.0f, 25.0f, 100.0f));
    CHECK(!NormalOvershoots(true, 500.0f, 280.0f, 1000.0f));
    CASE("key_off_never_vetoes");
    CHECK(!NormalOvershoots(false, 79.0f, 25.0f, 100.0f));
    CHECK(!NormalOvershoots(false, 200.0f, 25.0f, 100.0f));
    CASE("no_tank_threat_left_to_old_veto");
    CHECK(!NormalOvershoots(true, 79.0f, 25.0f, 0.0f));
    CHECK(!NormalOvershoots(true, 79.0f, 25.0f, -1.0f));
    CASE("no_estimate_left_to_old_veto");
    // A cast without direct damage (no estimate) is the old veto's business, even at or over the tank.
    CHECK(!NormalOvershoots(true, 120.0f, 0.0f, 100.0f));

    // ThreatCapAverageEffectPoints: the core's CalculateSpellDamage without the roll.
    CASE("average_points_is_the_middle_of_the_roll");
    // Fireball-like row: base 392, dice 1, sides 107 -> roll 1..107, average 392 + 54 = 446 (not the max 499).
    CHECK(ThreatCapAverageEffectPoints(392, 1, 107, 0.0f, 0.0f, 0) == 446.0f);
    // No roll range (sides 0 or 1): base + dice, as the core's `case 0: case 1:`.
    CHECK(ThreatCapAverageEffectPoints(99, 1, 1, 0.0f, 0.0f, 0) == 100.0f);
    CHECK(ThreatCapAverageEffectPoints(99, 1, 0, 0.0f, 0.0f, 0) == 100.0f);
    // Level growth: 4 levels at 0.5 per level adds 2 to the base and 4 levels at 1 per level widens the roll by 4.
    CHECK(ThreatCapAverageEffectPoints(100, 1, 9, 0.5f, 1.0f, 4) == 102.0f + (1.0f + 13.0f) / 2.0f);

    // ThreatCapCastThreat: the core's threat for a landed cast (Unit::DealDamage, Spell::HandleThreatSpells, CalcThreat).
    CASE("mind_blast_threat_multiplier_counts");
    // Mind Blast-like: 500 damage, spell threat table x2 = 1000 threat; from bot 0 against tank 1000 that is the line.
    CHECK(ThreatCapCastThreat(500.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) == 1000.0f);
    CHECK(NormalOvershoots(true, 0.0f, ThreatCapCastThreat(500.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    // Without the multiplier the same cast read 500 and passed.
    CHECK(!NormalOvershoots(true, 0.0f, ThreatCapCastThreat(500.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    CASE("searing_pain_threat_multiplier_counts");
    // Searing Pain-like: 220 damage x2 = 440; bot 600 + 440 passes tank 1000, x1 (220) would not.
    CHECK(ThreatCapCastThreat(220.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) == 440.0f);
    CHECK(NormalOvershoots(true, 600.0f, ThreatCapCastThreat(220.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    CHECK(!NormalOvershoots(true, 600.0f, ThreatCapCastThreat(220.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    CASE("flat_threat_entry_counts");
    // A spell threat table entry with flat threat 180 and AP bonus 0.1 at 1000 AP: 100 damage + 180 + 100 = 380.
    CHECK(ThreatCapCastThreat(100.0f, 1.0f, 180.0f, 0.1f, 1000.0f, 0.0f, 1.0f) == 380.0f);
    // A flat-threat-only cast (no damage) still counts.
    CHECK(ThreatCapCastThreat(0.0f, 1.0f, 300.0f, 0.0f, 0.0f, 0.0f, 1.0f) == 300.0f);
    CASE("threat_mods_scale_the_whole_cast");
    // A -30 % SPELLMOD_THREAT talent and a 1.3 total modifier (Defensive Stance-like) scale damage and flat alike.
    CHECK(std::fabs(ThreatCapCastThreat(100.0f, 2.0f, 100.0f, 0.0f, 0.0f, -30.0f, 1.3f) - 300.0f * 0.7f * 1.3f) < 0.01f);
    CHECK(ThreatCapCastThreat(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) == 0.0f);

    // ThreatCapChannelDamage: a channel counts every tick; a DoT that is not channeled counts none here.
    CASE("channel_ticks_reach_the_line_vetoed");
    // Arcane Missiles-like: 5 s channel, a missile every 1 s at 60 = 300; bot 750 + 300 >= tank 1000.
    CHECK(ThreatCapChannelDamage(true, 60.0f, 5000, 1000) == 300.0f);
    CHECK(NormalOvershoots(true, 750.0f, ThreatCapCastThreat(ThreatCapChannelDamage(true, 60.0f, 5000, 1000),
                                                                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    // Mind Flay-like: 3 s, 1 s ticks.
    CHECK(ThreatCapChannelDamage(true, 50.0f, 3000, 1000) == 150.0f);
    CASE("dot_only_spell_not_vetoed");
    // A DoT that is not channeled (Corruption-like, 18 s, 3 s ticks) adds nothing here: bot 750 is not vetoed.
    CHECK(ThreatCapChannelDamage(false, 60.0f, 18000, 3000) == 0.0f);
    CHECK(!NormalOvershoots(true, 750.0f, ThreatCapCastThreat(ThreatCapChannelDamage(false, 60.0f, 18000, 3000),
                                                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 1000.0f));
    CHECK(ThreatCapChannelDamage(true, 60.0f, 0, 1000) == 0.0f);
    CHECK(ThreatCapChannelDamage(true, 60.0f, 5000, 0) == 0.0f);

    // ThreatCapCastReference: the line is the mob's victim, capped by the top tank.
    CASE("off_tank_above_victim_no_cover");
    // Victim (main tank) 800, an off-tank on 1200: bot 600 + cast 250 = 850 passes the victim and pulls in the core;
    // measured against the top tank (1200) it would pass.
    CHECK(ThreatCapCastReference(800.0f, 1200.0f) == 800.0f);
    CHECK(NormalOvershoots(true, 600.0f, 250.0f, ThreatCapCastReference(800.0f, 1200.0f)));
    CHECK(!NormalOvershoots(true, 600.0f, 250.0f, 1200.0f));
    CASE("cast_reference_falls_back_to_top_tank");
    CHECK(ThreatCapCastReference(1000.0f, 800.0f) == 800.0f);
    CHECK(ThreatCapCastReference(0.0f, 800.0f) == 800.0f);
    CHECK(ThreatCapCastReference(500.0f, 0.0f) == 0.0f);
    CHECK(ThreatCapCastReference(500.0f, -1.0f) == -1.0f);

    // Rule (b): a crit against the core's real pull line.
    CASE("crit_past_ranged_pull_line_vetoed");
    // Bot 0, cast 0.9 of the reference, ranged: the normal hit (90 %) passes (a), a spell crit is 135 % >= 130 %.
    CHECK(kThreatCapCastMeleePullLine == 1.1f);
    CHECK(kThreatCapCastRangedPullLine == 1.3f);
    CHECK(ThreatCapCastPullLine(false) == 1.3f);
    CHECK(ThreatCapCastPullLine(true) == 1.1f);
    CHECK(ThreatCapCastOvershoots(true, 0.0f, 900.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(false), true));
    CASE("crit_without_crit_chance_passes");
    CHECK(!ThreatCapCastOvershoots(true, 0.0f, 900.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(false), false));
    CASE("crit_past_melee_pull_line_vetoed");
    // In melee reach (line 1100): bot 500 + 380 x 1.5 = 1070 passes; 500 + 400 x 1.5 = 1100 is vetoed. The same 400
    // from range (line 1300) passes.
    CHECK(!ThreatCapCastOvershoots(true, 500.0f, 380.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(true), true));
    CHECK(ThreatCapCastOvershoots(true, 500.0f, 400.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(true), true));
    CHECK(!ThreatCapCastOvershoots(true, 500.0f, 400.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(false), true));
    // A melee attack spell crit (x2): 500 + 300 x 2 = 1100 at the melee line.
    CHECK(ThreatCapCastOvershoots(true, 500.0f, 300.0f, 1000.0f, 2.0f, ThreatCapCastPullLine(true), true));
    CASE("small_cast_crit_under_the_line_passes");
    // Bot 600 + 200 x 1.5 = 900 < 1300 (and 800 < 1000): passes.
    CHECK(!ThreatCapCastOvershoots(true, 600.0f, 200.0f, 1000.0f, 1.5f, ThreatCapCastPullLine(false), true));
    CASE("crit_rule_needs_key_tank_and_estimate");
    CHECK(!ThreatCapCastOvershoots(false, 0.0f, 900.0f, 1000.0f, 1.5f, 1.3f, true));
    CHECK(!ThreatCapCastOvershoots(true, 0.0f, 900.0f, 0.0f, 1.5f, 1.3f, true));
    CHECK(!ThreatCapCastOvershoots(true, 1400.0f, 0.0f, 1000.0f, 1.5f, 1.3f, true));

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
