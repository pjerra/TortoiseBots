#pragma once

// Pure gate for "party member to heal out of spell range" (the trigger that
// walks a healer to its heal target).
//
// The "party member to heal" value always names a tank, even at full health:
// in a fight that keeps the healer within reach of the tank before the first
// hit lands. Out of combat the same pick made the healer walk to the tank
// instead of following its leader. Holy and discipline priests, holy
// paladins and restoration druids and shamans wire the trigger into their
// non-combat strategies above the follow action. In Uldaman (2026-10-12) a
// holy priest of a full-health party stood 38-47 yd from its leader for over
// a minute on "reach party member to heal" while the others followed.
//
// mod-playerbots (79bd428) only wires this trigger into combat strategies.
// Here the out-of-combat rows stay, for the member who really is hurt after a
// fight: out of combat the reach fires only for a target below the
// almost-full-health line, the same line the out-of-combat heal triggers use.
//
// In combat a healer also stays put when another living healer of the group
// already has the ally in heal range and in sight (issue #904): walking across
// the room to an ally somebody else can heal killed healers (Magmadar, 2026-10-13:
// 18 of 86 healer deaths ended on "reach party member to heal"). When no other
// healer can reach the ally, the walk stays. Out of combat the rule does not
// apply: the healer's walk there only fires for a hurt member anyway.
//
// No core includes; tested in tools/test_heal_reach_policy.cpp.

namespace ai
{
    // True when one other group member covers the ally: a healer (not the
    // requester, not the ally itself), alive, on the same map, within heal
    // range of the ally and with line of sight to it. "maxDistance" is the
    // reach trigger's own limit, so every caller uses the same range.
    // The tests run cheapest first and stop at the first miss: the plain
    // flags and the distance, then the class filter, then "isHealer" (it
    // walks talents for a role the bot was not told), then "inSight" (a
    // line-of-sight ray). The last two are callables so they only run when
    // everything before them passed. This runs every tick in a raid.
    // A healer only covers while it can act: not stunned, feared, confused,
    // charmed, silenced or pacified, and with enough mana left to heal. Out of
    // mana or crowd-controlled it covers nobody, so the others do not leave the
    // ally to it. Every healing class uses mana.
    constexpr float kCoverMinManaPct = 20.0f;

    inline bool HealerFitToCover(bool lostControl, bool silencedOrPacified, float manaPct)
    {
        return !lostControl && !silencedOrPacified && manaPct >= kCoverMinManaPct;
    }

    // "isFit" (HealerFitToCover from the unit's flags and mana) and "isHealer"
    // run after the plain tests and the class filter, "inSight" last.
    template <typename IsFit, typename IsHealer, typename InSight>
    inline bool HealerCoversAlly(bool alive, bool sameMap, float distanceToAlly, float maxDistance,
        bool classCanHeal, IsFit isFit, IsHealer isHealer, InSight inSight)
    {
        return alive && sameMap && distanceToAlly <= maxDistance && classCanHeal && isFit() && isHealer() && inSight();
    }

    // The reach limit of the trigger, the walk-up and the target pick: heal
    // range (the battleground one inside a battleground) less the contact
    // distance.
    inline float HealReachLimitFor(bool inBattleground, float healDistance, float healDistanceBg, float contactDistance)
    {
        return (inBattleground ? healDistanceBg : healDistance) - contactDistance;
    }

    // Cover never applies to an ally at or below the critical line: every
    // healer keeps it on its list and may walk to it (AnotherHealerCoversAlly
    // answers false for it, so the trigger, the walk-up and the pick agree).
    inline bool CoverApplies(float healthPct, float criticalHealthPct)
    {
        return healthPct > criticalHealthPct;
    }

    // Target pick, in combat: an ally this healer cannot reach and another
    // healer covers is left to that healer, so this one falls back to somebody
    // it can reach. An ally at or below the critical line always stays on the
    // list (CoverApplies). The two callables run only when needed.
    template <typename OutOfReach, typename Covered>
    inline bool DropCoveredCandidate(float healthPct, float criticalHealthPct, OutOfReach outOfReach, Covered covered)
    {
        return CoverApplies(healthPct, criticalHealthPct) && outOfReach() && covered();
    }

    inline bool HealReachWanted(bool inCombat, float targetHealthPct, float almostFullHealthPct,
        bool otherHealerCovers = false)
    {
        if (inCombat)
            return !otherHealerCovers;
        return targetHealthPct < almostFullHealthPct;
    }
}
