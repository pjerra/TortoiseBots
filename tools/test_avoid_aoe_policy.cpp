#include "../ai/playerbot/AvoidAoePolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AoeEscapeStep;
using ai::AoeHazardCircle;
using ai::HazardsHoldMove;
using ai::IsAvoidableAoeRadius;
using ai::kAoeAtSourceTargetA;
using ai::kAoeAtSourceTargetB;
using ai::kMaxAoeAvoidRadiusYd;
using ai::kTrapHazardMarginYd;
using ai::LandingInsideHazard;
using ai::MeleeAoeCandidates;
using ai::MeleeAoeLandingInRange;
using ai::RangedAoeCandidates;
using ai::RangedAoeLandingInRange;
using ai::TrapHazardRadius;
using ai::TriggerSpellMarksSpinHazard;

int main()
{
    std::cout << "Starting TortoiseBots avoid-aoe policy tests...\n";

    // Radius gate: positive and within the 15yd cap.
    CHECK(IsAvoidableAoeRadius(5.0f));
    CHECK(IsAvoidableAoeRadius(15.0f));
    CHECK(!IsAvoidableAoeRadius(0.0f));
    CHECK(!IsAvoidableAoeRadius(-1.0f));
    CHECK(!IsAvoidableAoeRadius(15.1f));
    CHECK(kMaxAoeAvoidRadiusYd == 15.0f);
    std::cout << "  [PASS] radius cap gates all three sensor cases\n";

    // Melee order (donor): strafes first, toward-target third (strict);
    // tanking appends away-from-target and away-from-hazard fallbacks.
    {
        float out[5] = {};
        CHECK(MeleeAoeCandidates(false, out) == 3);
        CHECK(out[0] > 1.57f && out[0] < 1.58f);
        CHECK(out[1] < -1.57f && out[1] > -1.58f);
        CHECK(out[2] == 0.0f);
        CHECK(MeleeAoeCandidates(true, out) == 5);
        CHECK(out[3] > 3.14f && out[4] > 3.14f);
    }
    std::cout << "  [PASS] melee strafe-first order\n";

    // Ranged order: strafes, away, toward, away-from-hazard.
    {
        float out[5] = {};
        CHECK(RangedAoeCandidates(out) == 5);
        CHECK(out[0] > 0.0f && out[1] < 0.0f);
        CHECK(out[2] > 3.14f);
        CHECK(out[3] == 0.0f);
    }
    std::cout << "  [PASS] ranged strafe-first order\n";

    // Band tests: melee stays close, ranged stays in the cast band.
    CHECK(MeleeAoeLandingInRange(5.0f, 8.0f));
    CHECK(!MeleeAoeLandingInRange(8.1f, 8.0f));
    CHECK(RangedAoeLandingInRange(20.0f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(7.9f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(30.1f, 8.0f, 30.0f));
    std::cout << "  [PASS] stay-in-range band tests\n";

    // Spinning self-centred hazard (#971: Herod's Whirlwind 8989 -> 15578
    // is WEAPON_DAMAGE_NOSCHOOL around the caster, 8 yd — the old
    // school-damage-only gate never saw it). School damage keeps the old
    // rule (invisible emitters only: a selectable mob with a permanent
    // burning aura such as Fire Shield II 184 stays fightable); weapon
    // spins list anyone.
    CHECK(TriggerSpellMarksSpinHazard(2, 0, 0, false));    // school damage, emitter: old shape
    CHECK(TriggerSpellMarksSpinHazard(2, 22, 15, false));
    CHECK(!TriggerSpellMarksSpinHazard(2, 0, 0, true));    // school damage, selectable: stay and fight
    CHECK(!TriggerSpellMarksSpinHazard(2, 22, 15, true));  // Fire Shield II 184 shape: no step-out
    CHECK(TriggerSpellMarksSpinHazard(17, 22, 15, true));  // Herod 15578, selectable boss
    CHECK(TriggerSpellMarksSpinHazard(17, 22, 15, false)); // same spin on an emitter
    CHECK(TriggerSpellMarksSpinHazard(58, 22, 15, true));  // plain weapon damage spin
    CHECK(TriggerSpellMarksSpinHazard(31, 22, 15, true));  // weapon percent spin
    CHECK(!TriggerSpellMarksSpinHazard(17, 6, 0, true));   // single-target weapon hit (Cleave 15496): no spin
    CHECK(!TriggerSpellMarksSpinHazard(17, 6, 0, false));
    CHECK(!TriggerSpellMarksSpinHazard(17, 22, 0, true));  // weapon damage, wrong B target
    CHECK(!TriggerSpellMarksSpinHazard(17, 15, 22, true)); // weapon damage, swapped targets
    CHECK(!TriggerSpellMarksSpinHazard(10, 22, 15, true)); // heal around caster: not a hazard
    CHECK(!TriggerSpellMarksSpinHazard(0, 0, 0, false));   // empty effect
    std::cout << "  [PASS] whirlwind-type trigger marks a spin hazard\n";

    // Escape step: just past the edge, capped at flee distance.
    CHECK(AoeEscapeStep(5.0f, 15.0f) == 6.0f);
    CHECK(AoeEscapeStep(20.0f, 15.0f) == 15.0f);
    std::cout << "  [PASS] escape step caps at flee distance\n";

    // Trap hazard radius: the spell's own circle (Flames 19628 is SpellRadius
    // index 8 = 5 yd) plus the margin, not the GO's 10 yd activation radius;
    // the activation radius is the fallback when the spell has none.
    CHECK(TrapHazardRadius(5.0f, 10.0f) == 5.0f + kTrapHazardMarginYd);
    CHECK(TrapHazardRadius(5.0f, 10.0f) < 10.0f);
    CHECK(TrapHazardRadius(0.0f, 10.0f) == 10.0f);
    CHECK(TrapHazardRadius(-1.0f, 10.0f) == 10.0f);
    CHECK(kAoeAtSourceTargetA == 22); // CASTER_SRC
    CHECK(kAoeAtSourceTargetB == 15); // ENEMY_AOE_AT_SRC_LOC
    std::cout << "  [PASS] trap hazard radius is the spell's circle\n";

    // A landing inside any damaging circle is rejected; at its edge and
    // outside all of them it is accepted (the core measures centre to centre
    // in 3D, so a ledge above a fire is out).
    {
        std::vector<AoeHazardCircle> hazards;
        hazards.push_back(AoeHazardCircle{0.0f, 0.0f, 0.0f, 7.0f});
        hazards.push_back(AoeHazardCircle{20.0f, 0.0f, 0.0f, 7.0f});
        CHECK(LandingInsideHazard(hazards, 0.0f, 0.0f, 0.0f));
        CHECK(LandingInsideHazard(hazards, 6.9f, 0.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 7.0f, 0.0f, 0.0f));
        CHECK(LandingInsideHazard(hazards, 20.0f, 6.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 10.0f, 0.0f, 0.0f));
        CHECK(!LandingInsideHazard(hazards, 0.0f, 0.0f, 7.0f));
        std::vector<AoeHazardCircle> none;
        CHECK(!LandingInsideHazard(none, 0.0f, 0.0f, 0.0f));

        // The hold, the CastHoldsMove shape: never the step-out, never a move
        // whose target is the bot, never without a destination.
        CHECK(HazardsHoldMove(false, false, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, false, true, 10.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(true, false, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, true, true, 0.0f, 0.0f, 0.0f, hazards));
        CHECK(!HazardsHoldMove(false, false, false, 0.0f, 0.0f, 0.0f, hazards));
    }
    std::cout << "  [PASS] landing and hold on every damaging circle\n";

    std::cout << "All avoid-aoe policy tests passed.\n";
    return 0;
}
