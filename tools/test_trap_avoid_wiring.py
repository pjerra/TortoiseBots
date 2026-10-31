"""Wiring pins for ground-fire avoidance (trap GOs such as Jeklik's Inferno 177764).

A trap GO's data2 radius is its activation radius, not the reach of its spell:
Zul'Gurub's Inferno casts Flames 19628, school damage at the source with
SpellRadius index 8 = 5 yd. The rules live in ai/playerbot/AvoidAoePolicy.h
(TrapHazardRadius, LandingInsideHazard, HazardsHoldMove; their behaviour is
tested in tools/test_avoid_aoe_policy.cpp). These pins only prove the live code
calls them:

 - the hazard radius comes from the trap spell's effect radius through
   sSpellRadiusStore (the GO's data2 radius is the fallback when the spell has
   no radius), and only for an effect that is damage at the source;
 - the step-out sensor (AvoidAoeAction) uses that radius, so a bot already in
   the fire steps out of the circle that hurts, not the whole trigger;
 - a step-out landing is rejected inside any damaging trap (and any damaging
   dynamic object), not only inside creature keep-outs;
 - a move whose destination lies in a fire is held (trap hold), the same shape
   as the avoid_cast hold, on the combat and non-combat engines, and the
   step-out itself is never held.

Run: python3 -m unittest tools/test_trap_avoid_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (STRATEGY / "actions/BossTacticsActions.h").read_text()
TACTICS_STRATEGY = (STRATEGY / "generic/BossTacticsStrategy.cpp").read_text()
VALUES = (STRATEGY / "values/NearestGameObjects.cpp").read_text()
VALUES_H = (STRATEGY / "values/NearestGameObjects.h").read_text()
POLICY = (ROOT / "ai/playerbot/AvoidAoePolicy.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TrapHazardRadiusWiring(unittest.TestCase):
    def test_radius_comes_from_the_trap_spell(self):
        fn = body(VALUES, "float ai::DamagingTrapHazardRadius(")
        self.assertIn("goInfo->trap.spellId", fn)
        self.assertIn("sServerFacade.LookupSpellInfo(", fn)
        self.assertIn("sSpellRadiusStore.LookupEntry(spellProto->EffectRadiusIndex[i])", fn)
        self.assertIn("TrapHazardRadius(spellRadius, trapRadius)", fn)
        # Only damage at the source widens the circle (Flames 19628: school
        # damage, TargetA/TargetB = the source AoE pair).
        self.assertIn("SPELL_EFFECT_SCHOOL_DAMAGE", fn)
        self.assertIn("SPELL_AURA_PERIODIC_DAMAGE", fn)
        self.assertIn("kAoeAtSourceTargetA", fn)
        self.assertIn("kAoeAtSourceTargetB", fn)
        # The GO's activation radius stays the fallback.
        self.assertIn("goInfo->trap.radius + go->GetCombatReach()", fn)
        self.assertLess(fn.index("float const trapRadius ="), fn.index("if (!spellProto)"))
        self.assertIn("return trapRadius;", fn)

    def test_policy_owns_the_margin_and_the_source_targets(self):
        self.assertIn("inline float TrapHazardRadius(float spellRadiusYd, float trapRadius)", POLICY)
        self.assertIn("kTrapHazardMarginYd = 2.0f", POLICY)
        self.assertIn("kAoeAtSourceTargetA = 22", POLICY)
        self.assertIn("kAoeAtSourceTargetB = 15", POLICY)
        self.assertIn("inline bool LandingInsideHazard(", POLICY)
        self.assertIn("inline bool HazardsHoldMove(", POLICY)

    def test_the_listed_traps_become_hazard_circles(self):
        fn = body(VALUES, "void ai::CollectDamagingTrapHazards(")
        self.assertIn('AI_VALUE(std::list<ObjectGuid>, "nearest damaging traps")', fn)
        self.assertIn("DamagingTrapHazardRadius(go)", fn)
        self.assertIn("hazard.radius", fn)
        self.assertIn("out.push_back(hazard)", fn)
        # One cached value read and GO lookups: no grid sweep per tick here.
        self.assertNotIn("VisitAllObjects", fn)
        self.assertIn("void CollectDamagingTrapHazards(", VALUES_H)


class TrapSensorWiring(unittest.TestCase):
    def test_the_step_out_uses_the_spell_circle(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::AvoidDamagingTrap(")
        self.assertIn("DamagingTrapHazardRadius(go)", fn)
        self.assertNotIn("goInfo->trap.radius", fn)
        self.assertIn("IsAvoidableAoeRadius(radius)", fn)
        self.assertIn("bot->GetDistance(go) > radius", fn)


class LandingWiring(unittest.TestCase):
    def test_a_landing_inside_another_fire_is_rejected(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("CollectDamagingTrapHazards(ai, bot, damagingHazards)", fn)
        self.assertIn('"nearest dynamic objects no los"', fn)
        self.assertIn("dyn->GetRadius()", fn)
        check = "if (LandingInsideHazard(damagingHazards, cand.getX(), cand.getY(), cand.getZ()))"
        self.assertIn(check, fn)
        # Built once, checked for every candidate, before the landing is accepted.
        self.assertLess(fn.index("CollectDamagingTrapHazards("), fn.index(check))
        self.assertLess(fn.index(check), fn.index("WorldPosition out = cand;"))


class TrapHoldWiring(unittest.TestCase):
    def test_the_multiplier_is_registered_next_to_the_cast_hold(self):
        self.assertIn("class TrapHoldMultiplier : public Multiplier", ACTIONS_H)
        self.assertIn('Multiplier(ai, "trap hold")', ACTIONS_H)
        for init in ("void BossTacticsStrategy::InitCombatMultipliers(",
                     "void BossTacticsStrategy::InitNonCombatMultipliers("):
            fn = body(TACTICS_STRATEGY, init)
            self.assertIn("multipliers.push_back(new TrapHoldMultiplier(ai));", fn)
            self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));", fn)

    def test_a_move_into_a_fire_is_held(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<MovementAction*>(action)", fn)
        self.assertIn("CollectDamagingTrapHazards(ai, bot, hazards)", fn)
        self.assertIn("if (hazards.empty())", fn)
        # The stay, the follow (unit and formation spot) and any other move's
        # target, exactly like the cast hold.
        self.assertIn("dynamic_cast<ReturnToStayPositionAction*>(action)", fn)
        self.assertIn("HazardsHoldMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, hazards)", fn)
        self.assertIn("dynamic_cast<FollowAction*>(action)", fn)
        self.assertIn('AI_VALUE(Unit*, "follow target")', fn)
        self.assertIn('AI_VALUE(WorldPosition, "formation position")', fn)
        self.assertIn("HazardsHoldMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), hazards)", fn)
        self.assertIn("HazardsHoldMove(false, target == bot, true, target->GetPositionX(), target->GetPositionY(),",
                      fn)
        # The reads precede the hold; an empty map costs one cached value.
        self.assertLess(fn.index("CollectDamagingTrapHazards("), fn.index("HazardsHoldMove("))

    def test_the_step_out_itself_is_never_held(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<AvoidAoeAction*>(action)", fn)
        self.assertIn("dynamic_cast<BossTacticsAvoidCastAction*>(action)", fn)
        self.assertLess(fn.index("dynamic_cast<AvoidAoeAction*>(action)"), fn.index("CollectDamagingTrapHazards("))
        self.assertIn("class AvoidAoeAction : public MovementAction", (STRATEGY / "actions/MovementActions.h").read_text())


if __name__ == "__main__":
    unittest.main()
