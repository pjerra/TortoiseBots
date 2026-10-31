#pragma once

// Pure decision rules for POS-2 (proactive AoE avoidance).
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   AvoidAoeAction::Execute (3-case sensor: dynobj aura, trap GO,
//     trigger NPC) + BestPositionForMeleeToFlee /
//     BestPositionForRangedToFlee (strafe-first, stay-in-range) +
//     MaxAoeAvoidRadius cap.
// Donor shape kept: radius cap 15yd; melee strafes +-90 degrees off the
// angle to the target first (non-strict), straight away only when tanking
// or as fallback; ranged strafes +-90 degrees off the angle to the target
// first, then straight away from the target, then straight away from the
// hazard. Strict candidates (the straight-line ones) must keep the bot in
// its combat band: melee stays within TooClose reach of the target, ranged
// stays inside the TooClose..Spell band.
// Adapted: callers own game state (positions, LoS, path, aggro checks via
// FindStep); this header owns the candidate ordering and the band tests so
// the rules stay testable in tools/test_avoid_aoe_policy.cpp.
// No core includes.

#include <cmath>
#include <vector>

namespace ai
{
    // Donor radius cap: hazards bigger than this are not dodged by stepping.
    constexpr float kMaxAoeAvoidRadiusYd = 15.0f;

    // Trap hazard radius. A trap GO's data2 radius (10 yd on Zul'Gurub's Inferno
    // 177764) is the activation radius, not the reach of its spell: Flames 19628
    // is school damage at the source with SpellRadius index 8 = 5 yd. A step out
    // and a walk back in use the spell's own circle plus this margin, so the
    // no-go zone is where the spell hits, not the whole trap trigger.
    constexpr float kTrapHazardMarginYd = 2.0f;

    inline float TrapHazardRadius(float spellRadiusYd, float trapRadius)
    {
        return spellRadiusYd > 0.0f ? spellRadiusYd + kTrapHazardMarginYd : trapRadius;
    }

    // A damaging ground effect as plain numbers (a trap GO's spell circle, a
    // damaging dynamic object). Centre to centre in 3D, the core's measure.
    struct AoeHazardCircle
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
    };

    // Implicit target pair of an effect that hits enemies around its source
    // (Flames 19628: CASTER_SRC + ENEMY_AOE_AT_SRC_LOC).
    constexpr int kAoeAtSourceTargetA = 22;
    constexpr int kAoeAtSourceTargetB = 15;

    inline bool InsideAoeHazard(const AoeHazardCircle& hazard, float x, float y, float z)
    {
        float const dx = hazard.x - x;
        float const dy = hazard.y - y;
        float const dz = hazard.z - z;
        return hazard.radius > 0.0f && std::sqrt(dx * dx + dy * dy + dz * dz) < hazard.radius;
    }

    // A step-out's landing must clear every damaging circle, not only the one it
    // leaves: the next fire is usually beside the one under the bot.
    inline bool LandingInsideHazard(const std::vector<AoeHazardCircle>& hazards, float x, float y, float z)
    {
        for (const AoeHazardCircle& hazard : hazards)
            if (InsideAoeHazard(hazard, x, y, z))
                return true;
        return false;
    }

    // The trap hold (the avoid_cast CastHoldsMove shape): a move whose destination
    // lies in a damaging circle is held, so a bot does not walk back into fire it
    // stepped out of. Not the step-out itself and not a move whose target is the
    // bot.
    inline bool HazardsHoldMove(bool isWalkOut, bool targetIsSelf, bool hasDestination, float x, float y, float z,
                                const std::vector<AoeHazardCircle>& hazards)
    {
        if (isWalkOut || targetIsSelf || !hasDestination)
            return false;
        return LandingInsideHazard(hazards, x, y, z);
    }

    // Radius gate shared by all three sensor cases.
    inline bool IsAvoidableAoeRadius(float radius)
    {
        return radius > 0.0f && radius <= kMaxAoeAvoidRadiusYd;
    }

    // A periodic-trigger aura whose triggered spell hurts around the caster
    // marks a hazard — but not the same one for every unit (#971 review):
    // school damage keeps the old rule (invisible NOT_SELECTABLE emitters
    // only: selectable mobs with a permanent burning aura such as Fire
    // Shield II 184 on the Scarlet Conjuror 4297 must stay fightable, or
    // melee would step out forever), while self-centred weapon-damage spins
    // (Herod's Whirlwind 8989 -> 15578: EFFECT_WEAPON_DAMAGE_NOSCHOOL at
    // TgtA CASTER_SRC / TgtB ENEMY_AOE_AT_SRC_LOC, 8 yd) are accepted on any
    // unit, selectable bosses included. Effect-only test (no radius check):
    // callers own the radius gate and the range test, and the unit test
    // feeds bare effect/target/selectable triples.
    inline bool TriggerSpellMarksSpinHazard(int effect, int targetA, int targetB, bool selectable)
    {
        if (effect == 2) // SPELL_EFFECT_SCHOOL_DAMAGE
            return !selectable;
        if (targetA != kAoeAtSourceTargetA || targetB != kAoeAtSourceTargetB)
            return false;
        return effect == 17 || // SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL (Herod 15578)
            effect == 58 ||    // SPELL_EFFECT_WEAPON_DAMAGE
            effect == 31;      // SPELL_EFFECT_WEAPON_PERCENT_DAMAGE
    }

    // Strafe-first candidate order. Returns the ordered heading offsets
    // (radians, relative to the angle bot->target) for the melee case:
    // left/right strafe, then straight away from the target.
    // `tanking` appends the straight-through-target and away-from-hazard
    // fallbacks the donor adds when the target is on the bot.
    constexpr int kAoeCandidatesMelee = 3;
    constexpr int kAoeCandidatesMeleeTanking = 5;

    // Donor BestPositionForMeleeToFlee order: left/right strafe (non-strict),
    // toward the target (strict — re-approach guard), then, when the target
    // is on the bot, away from the target and away from the hazard (both
    // non-strict fallbacks so a tank can always leave a wide zone).
    inline int MeleeAoeCandidates(bool tanking, float* out)
    {
        float const half = 1.5707963268f; // PI/2
        out[0] = half;
        out[1] = -half;
        out[2] = 0.0f; // toward target (strict)
        if (!tanking)
            return kAoeCandidatesMelee;
        out[3] = 3.1415926536f; // away from target (fallback, non-strict)
        out[4] = 3.1415926536f; // away from hazard (fallback, non-strict)
        return kAoeCandidatesMeleeTanking;
    }

    // Ranged order: left/right strafe, straight away from the target,
    // toward the target (re-approach guard), away from the hazard.
    constexpr int kAoeCandidatesRanged = 5;

    inline int RangedAoeCandidates(float* out)
    {
        float const half = 1.5707963268f; // PI/2
        out[0] = half;
        out[1] = -half;
        out[2] = 3.1415926536f;
        out[3] = 0.0f;
        out[4] = 3.1415926536f; // away-from-hazard slot (caller anchors it)
        return kAoeCandidatesRanged;
    }

    // Melee band test for strict (straight-line) candidates: the landing
    // must stay within TooClose reach of the target (donor:
    // fleePos - reach > TooCloseDistance is rejected while in melee).
    inline bool MeleeAoeLandingInRange(float landingToTarget, float tooCloseDistance)
    {
        return landingToTarget <= tooCloseDistance;
    }

    // Ranged band test for strict candidates: the landing must stay inside
    // the TooClose..Spell band (donor rejects past SpellDistance and inside
    // TooCloseDistance).
    inline bool RangedAoeLandingInRange(float landingToTarget,
        float tooCloseDistance, float spellDistance)
    {
        return landingToTarget >= tooCloseDistance && landingToTarget <= spellDistance;
    }

    // Escape step length: just past the hazard edge, capped at the flee
    // distance (donor: min(radius + 1, FleeDistance)).
    inline float AoeEscapeStep(float radius, float fleeDistance)
    {
        float step = radius + 1.0f;
        return step < fleeDistance ? step : fleeDistance;
    }
} // namespace ai
