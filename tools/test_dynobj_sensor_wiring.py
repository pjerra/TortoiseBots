"""Wiring pins for the dynamic-object (ground cloud) sensor.

A persistent-area-aura dynamic object (Venoxis' Poison Cloud 23861, a mob's
Blizzard/Rain of Fire/Consecration) is a hostile ground circle the bot must step
out of, but the sensor behind "nearest dynamic objects" was a stub that returned
an empty list: a bot could not see any ground cloud in any dungeon or raid.

These pins only prove the live code is wired to the rules in
ai/playerbot/AvoidAoePolicy.h (their behaviour is tested in
tools/test_avoid_aoe_policy.cpp):

 - the dynobj scan visits the grid container only (dynamic objects live nowhere
   else), and the check filters TYPEID_DYNAMICOBJECT because the searcher calls
   it for every container type;
 - the "no los" value is registered with the hazard scan range (30 yd), not the
   60 yd sight distance;
 - step-out landing hazards come only from hostile, non-positive circles, so a
   friendly mage's Blizzard never refuses a landing, and the dynobj circles are
   merged into the landing list the strafe actually checks;
 - the tank drag uses the policy step and its landing test, and the caller skips
   the drag when the cap is below the landing test's need;
 - the proactive step-out sensor and the melee hold see creature-cast clouds
   only (open world / PvP against players is unchanged); the hold has no tank
   exemption, so a tank whose boss stays in the cloud does not walk back in;
 - the fallback flee of the "has area debuff" row is its own action ("flee from
   area"): only it is skipped while a hostile cloud covers the bot (the strafe
   owns that case and a plain flee ignores the landing filter) and for a tank
   that holds its boss; the shared "flee" action has no such clause, so panic,
   outnumbered, critical-health and range-control flees still work for a tank;
 - the "aoe threat nearby" trigger gets a proactive dynobj centre test, so the
   step fires before the cloud's first tick;
 - the reactive "has area debuff" -> "flee from area" row sits below the strafe,
   so the step-out wins the reaction pick.

Run: python3 -m unittest tools/test_dynobj_sensor_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
MOVEMENT_H = (STRATEGY / "actions/MovementActions.h").read_text()
ACTION_CONTEXT = (STRATEGY / "actions/ActionContext.h").read_text()
FLEE_STRATEGY = (STRATEGY / "generic/FleeStrategy.cpp").read_text()
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
COMBAT_STRATEGY = (STRATEGY / "generic/CombatStrategy.cpp").read_text()
TRIGGERS = (STRATEGY / "triggers/GenericTriggers.cpp").read_text()
VALUES = (STRATEGY / "values/NearestGameObjects.cpp").read_text()
VALUE_CONTEXT = (STRATEGY / "values/ValueContext.h").read_text()
TARGETS = (STRATEGY / "values/PossibleTargetsValue.cpp").read_text()
AOEVALUES = (STRATEGY / "values/AoeValues.cpp").read_text()
SLICE_CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
FOREVER_V = (ROOT / "forever/src/compat/TbCompat_V.h").read_text()
TURTLE_COMPAT = (ROOT / "ai/cmangos-compat-r4v.h").read_text()
FOREVER_V_CPP = (ROOT / "forever/src/compat/TbCompat_V.cpp").read_text()
REACH = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DynObjScanWiring(unittest.TestCase):
    def test_the_dynobj_scan_visits_the_grid(self):
        fn = body(VALUES, "std::list<ObjectGuid> NearestDynamicObjects::Calculate()")
        # The grid-only search is one both-core helper (R4V): a WorldObjectListSearcher and
        # Cell::VisitGridObjects on Turtle, the same on ForeverCore (see the helper pins below).
        self.assertIn("VisitGridObjectsCompat(bot, targets, u_check, range)", fn)
        # A dynamic object lives only in the grid container; the world
        # containers (players, creatures, corpses) never hold one.
        self.assertNotIn("VisitGameObjectsCompat(", fn)
        self.assertNotIn("VisitUnitsCompat(", fn)
        self.assertNotIn("Leave the list", fn)
        self.assertNotIn("Penqle has no", fn)
        # WorldObjectListSearcher calls the check for every container type
        # (player, creature, corpse, game object, dynamic object), so the check
        # takes a WorldObject* and filters by type id - the
        # MaNGOS::AllWorldObjectsInRange shape.
        check = body(VALUES, "class DynamicObjectsInRangeCheck", "\n};\n")
        self.assertIn("operator()(WorldObject* u)", check)
        self.assertIn("TYPEID_DYNAMICOBJECT", check)

    def test_the_scan_range_is_the_hazard_range(self):
        rows = [line for line in VALUE_CONTEXT.splitlines()
                if '"nearest dynamic objects no los"' in line]
        self.assertEqual(len(rows), 1)
        self.assertIn("kTrapScanRangeYd", rows[0])

    def test_the_far_point_dynobj_query_visits_the_grid(self):
        fn = body(VALUES, "void ai::CollectDamagingDynObjHazardsAt(")
        self.assertIn("VisitGridObjectsAtCompat(", fn)
        self.assertNotIn("VisitGameObjectsAtCompat(", fn)
        self.assertNotIn("VisitUnitsAtCompat(", fn)

    def test_the_grid_only_helpers_keep_the_search_the_code_always_made(self):
        # The helpers hold the searcher and the visit the call sites used to spell out.
        turtle = (ROOT / "ai/cmangos-compat-r4v.h").read_text()
        forever = (ROOT / "forever/src/compat/TbCompat_V.h").read_text()
        for text in (turtle, forever):
            grid = body(text, "inline void VisitGridObjectsCompat(", "\n}\n")
            self.assertIn("WorldObjectListSearcher<Check> searcher(", grid)
            self.assertIn("Cell::VisitGridObjects(center, searcher, range);", grid)
            self.assertNotIn("VisitAllObjects", grid)
            at_point = body(text, "inline void VisitGridObjectsAtCompat(", "\n}\n")
            self.assertIn("Cell::VisitGridObjects(x, y, map, searcher, range", at_point)
            self.assertNotIn("VisitAllObjects", at_point)
        # Turtle: a bounded query that never loads a grid (dont_load = true), as the call sites passed.
        self.assertIn("Cell::VisitGridObjects(x, y, map, searcher, range, true);", turtle)


class GroundCloudEmitterWiring(unittest.TestCase):
    """A periodic-trigger aura that drops a persistent area aura (effect 27)
    every tick - Sapphiron's Blizzard emitter 16474, Chill 28534 -> 28547 -
    was invisible to the trigger sensor, which knew only school damage and
    weapon spins. Failing case before the fix: neither call site named
    TriggerSpellMarksGroundHazard, so the emitter was never listed and the
    step-out never fired on it."""

    def test_the_trigger_list_accepts_a_ground_cloud_emitter(self):
        fn = body(TARGETS, "bool PossibleTriggersValue::AcceptUnit(")
        self.assertIn("TriggerSpellMarksGroundHazard(", fn)
        self.assertIn("triggerProto->IsPositiveEffect(SpellEffectIndex(i))", fn)

    def test_the_step_out_uses_the_cloud_radius(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::AvoidTriggerNpc(")
        self.assertIn("TriggerSpellMarksGroundHazard(", fn)
        self.assertIn("triggerProto->IsPositiveEffect(SpellEffectIndex(i))", fn)
        # The circle is the triggered (effect-27) spell's own radius, read
        # after the gate that now accepts the cloud.
        self.assertLess(fn.index("TriggerSpellMarksGroundHazard("),
                        fn.index("sSpellRadiusStore.LookupEntry(triggerProto->EffectRadiusIndex[i])"))


class DynObjHazardWiring(unittest.TestCase):
    def test_landing_hazards_skip_friendly_casters(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot", fn)
        # A circle is built only for a hostile, non-positive caster, and both
        # collectors (cached list, far-point query) share the same filter, so a
        # friendly mage's Blizzard never refuses a landing. The friendly
        # exemption is decided against a player-controlled caster.
        push = body(VALUES, "void PushDynObjHazard(")
        self.assertIn("IsFriendlyTo", push)
        self.assertIn("DynObjIsHostileHazard", push)
        self.assertIn("GetCharmerOrOwnerPlayerOrPlayerItself", push)
        for caller in ("void ai::CollectDamagingDynObjHazards(",
                       "void ai::CollectDamagingDynObjHazardsAt("):
            self.assertIn("PushDynObjHazard(", body(VALUES, caller))

    def test_the_landing_list_merges_the_dynobj_circles(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn(
            "damagingHazards.insert(damagingHazards.end(), dynObjHazards.begin(), dynObjHazards.end())", fn)
        check = "if (LandingInsideHazard(damagingHazards, cand.getX(), cand.getY(), cand.getZ()))"
        # Merged before any landing is checked against it.
        self.assertLess(fn.index("damagingHazards.insert("), fn.index(check))

    def test_the_tank_drag_uses_the_policy(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::StrafeToSafety(")
        self.assertIn("TankDragStep(", fn)
        self.assertIn("TankLandingDragsTargetOut(", fn)
        # The drag is skipped, not attempted with an unsatisfiable step, when
        # the cap is below what the landing test needs.
        self.assertIn("TankDragFitsCap(", fn)

    def test_the_proactive_step_is_creature_clouds_only(self):
        fn = body(MOVEMENT, "bool AvoidAoeAction::AvoidAuraWithDynamicObj(")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(circle, bot->GetPositionX()", fn)

    def test_the_area_flee_is_skipped_under_a_hostile_cloud(self):
        fn = body(MOVEMENT, "bool FleeFromAreaAction::isUseful()")
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(cloud, bot->GetPositionX()", fn)

    def test_the_area_flee_skips_an_active_tank(self):
        fn = body(MOVEMENT, "bool FleeFromAreaAction::isUseful()")
        self.assertIn("PlayerbotAI::IsTank(bot) && target && target->GetVictim() == bot", fn)

    def test_the_shared_flee_has_no_tank_or_cloud_clause(self):
        # The shared "flee" serves panic, outnumbered, critical health and range
        # control: the tank and cloud clauses stay in the area twin. Its one gate is the boss stand seat
        # (AiPlayerbot.BossStandWholeRaid): a bot on its seat does not flee, with the key off it is the plain flee.
        i = MOVEMENT_H.index("class FleeAction : public MovementAction")
        decl = MOVEMENT_H[i:MOVEMENT_H.index("};", i)]
        self.assertIn("virtual bool isUseful() override;", decl)
        fn = body(MOVEMENT, "bool FleeAction::isUseful()")
        self.assertNotIn("IsTank", fn)
        self.assertNotIn("CollectDamagingDynObjHazards", fn)
        self.assertIn("FleeYieldsToStand(true, FindBossStand(ai, bot).valid)", fn)

    def test_the_area_flee_is_a_flee_action_registered_live(self):
        i = MOVEMENT_H.index("class FleeFromAreaAction")
        decl = MOVEMENT_H[i:MOVEMENT_H.index("};", i)]
        self.assertIn("public FleeAction", decl)
        self.assertIn('"flee from area"', decl)
        self.assertIn("virtual bool isUseful() override;", decl)
        self.assertIn('creators["flee from area"] = [](PlayerbotAI* ai) { return new FleeFromAreaAction(ai); };', ACTION_CONTEXT)
        # The flee dampener keeps covering the area flee it used to dampen.
        self.assertIn('"flee from area"', FLEE_STRATEGY)

    def test_the_trigger_steps_out_proactively(self):
        fn = body(TRIGGERS, "bool AoeThreatNearbyTrigger::IsActive()")
        # The step fires as the cloud forms, on the centre test, not only once
        # the aura has landed ("has area debuff").
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, clouds, true)", fn)
        self.assertIn("InsideAoeHazard(cloud, bot->GetPositionX()", fn)


class DynObjHoldWiring(unittest.TestCase):
    def test_the_hold_sees_dynobjs(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        self.assertIn("CollectDamagingDynObjHazardsAt(", fn)

    def test_the_hold_has_no_tank_exemption_for_clouds(self):
        fn = body(ACTIONS, "float TrapHoldMultiplier::GetValue(")
        # A tank whose boss stays in the cloud must be held at the edge too: it
        # is the drag, not a walk back in, that gets the boss out.
        self.assertNotIn("PlayerbotAI::IsTank(bot) && target->GetVictim() == bot", fn)
        self.assertIn("CollectDamagingDynObjHazardsAt(ai, bot, endpoint.x, endpoint.y, endpoint.z, dynObjHazards, true)", fn)


class FleeRowWiring(unittest.TestCase):
    def test_the_area_row_names_the_area_flee(self):
        i = COMBAT_STRATEGY.rindex('"has area debuff"')
        row = COMBAT_STRATEGY[i:i + 200]
        self.assertIn('new NextAction("flee from area"', row)
        self.assertNotIn('new NextAction("flee",', row)

    def test_the_flee_row_sits_below_the_strafe(self):
        i = COMBAT_STRATEGY.rindex('"has area debuff"')
        row = COMBAT_STRATEGY[i:i + 200]
        self.assertIn("ACTION_EMERGENCY + 4", row)
        self.assertNotIn("ACTION_EMERGENCY + 5", row)

class AreaTriggerRingWiring(unittest.TestCase):
    """A CREATE_AREATRIGGER ground ring (Flamestrike 8422 on ForeverCore) leaves
    no DynamicObject, so the cloud sensor never saw it: the trigger never fired
    and the step-out never ran. The ring sensor mirrors the cloud sensor (grid
    scan, cached guid list, same hostility rules) and merges into the same
    collectors, so every consumer is fixed at once."""

    def test_the_ring_scan_visits_the_grid(self):
        fn = body(FOREVER_V_CPP, "void CollectAreaTriggerInfosCompat(WorldObject const* center, float range,")
        self.assertIn("VisitGridObjectsCompat(center, targets, check, range)", fn)
        self.assertNotIn("VisitGameObjectsCompat(", fn)
        self.assertNotIn("VisitUnitsCompat(", fn)
        # WorldObjectListSearcher calls the check for every container type, so
        # the check takes a WorldObject* and filters by type id.
        check = body(FOREVER_V_CPP, "class AreaTriggersInRangeCheck", "\n};\n")
        self.assertIn("operator()(WorldObject* u)", check)
        self.assertIn("TYPEID_AREATRIGGER", check)

    def test_the_ring_value_uses_the_hazard_range(self):
        fn = body(VALUES, "std::list<ObjectGuid> NearestAreaTriggers::Calculate()")
        self.assertIn("CollectAreaTriggerInfosCompat(bot, range, infos)", fn)
        rows = [line for line in VALUE_CONTEXT.splitlines()
                if '"nearest area triggers no los"' in line]
        self.assertEqual(len(rows), 1)
        self.assertIn("kTrapScanRangeYd", rows[0])
        self.assertIn('creators["nearest area triggers no los"]', SLICE_CTX)

    def test_the_collectors_merge_the_rings(self):
        fn = body(VALUES, "void ai::CollectDamagingDynObjHazards(")
        self.assertIn('"nearest area triggers no los"', fn)
        self.assertIn("PushAreaTriggerHazard(", fn)
        far = body(VALUES, "void ai::CollectDamagingDynObjHazardsAt(")
        self.assertIn("CollectAreaTriggerInfosAtCompat(", far)
        self.assertIn("PushAreaTriggerHazard(", far)

    def test_the_ring_push_uses_the_policy_gate(self):
        push = body(VALUES, "void PushAreaTriggerHazard(")
        self.assertIn("DamagingAreaTriggerEffect(", push)
        self.assertIn("DynObjIsHostileHazard(", push)
        self.assertIn("GetCharmerOrOwnerPlayerOrPlayerItself", push)
        self.assertIn("IsFriendlyTo", push)
        # The ring's damage search is reach-inclusive: the hazard circle is the
        # trigger bounds plus the caster's reach.
        self.assertIn("info.caster->GetCombatReach()", push)

    def test_the_debuff_value_sees_the_ring_mark(self):
        fn = body(AOEVALUES, "bool HasAreaDebuffValue::Calculate()")
        self.assertIn('"nearest area triggers no los"', fn)
        self.assertIn("DamagingAreaTriggerEffect(", fn)
        # Any caster: the ticking aura's caster guid need not match the ring's.
        self.assertIn("HasAura(info.spellId)", fn)
    def test_the_reach_stands_down_inside_a_ring(self):
        start = REACH.index("class ReachTargetAction : public MovementAction, public Qualified")
        segment = REACH[start:REACH.index("class CastReachTargetSpellAction", start)]
        # A reach (melee, spell, cc) that ran while the bot stands inside a
        # hostile ring would cancel the escape mid-step or walk back into the
        # fire. The walk resumes once the bot is clear, and a covered target
        # holds the reach too: walking in anyway is walking back in.
        self.assertIn("CollectDamagingDynObjHazards(ai, bot, rings, true)", segment)
        self.assertIn("InsideAoeHazard(ring, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ())", segment)
        self.assertIn("if (Unit* reachTarget = GetTarget())", segment)

    def test_both_cores_name_the_same_seam(self):
        for text in (TURTLE_COMPAT, FOREVER_V):
            self.assertIn("struct AreaTriggerHazardInfo", text)
            self.assertIn("CollectAreaTriggerInfosCompat(", text)
            self.assertIn("CollectAreaTriggerInfosAtCompat(", text)
            self.assertIn("bool GetAreaTriggerInfoCompat(", text)
        # Turtle 1.12 has no AreaTrigger entities: all three twins are inline
        # bodies (the resolver answers false).
        for prefix, tail in (("inline void CollectAreaTriggerInfosCompat(", "{}"),
                             ("inline void CollectAreaTriggerInfosAtCompat(", "{}"),
                             ("inline bool GetAreaTriggerInfoCompat(", "{ return false; }")):
            rows = [line for line in TURTLE_COMPAT.splitlines() if prefix in line]
            self.assertEqual(len(rows), 1)
            self.assertIn(tail, rows[0])


if __name__ == "__main__":
    unittest.main()
