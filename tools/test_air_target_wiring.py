"""Wiring pins for the air_target boss tactic (Onyxia kill plan, code item C1).

The rule lives in ai/playerbot/BossTacticsPolicy.h (AirTargetKeep, pure, tested by
tools/test_boss_tactics_policy.cpp) and its game-state translation in
ai/playerbot/BossTacticsAirTarget.h. These pins prove the live code calls it:

 - a ranged bot keeps the airborne boss as its target: the core flags a flying
   boss IsEvadeBecauseTargetNotReachable (no floor path reaches her), and the
   target values used to drop her on exactly that flag;
 - "current target" keeps her out to reactDistance instead of sightDistance;
 - the caster's reach walks the floor under her instead of chasing her flight
   height;
 - the Onyxia fight strategy no longer hands "shoot" to every bot while she is
   airborne: the casters keep their own rotation on the target, and the wand row
   is gated to kits without a ranged rotation (PlayerbotAI::IsRanged, the
   module's own kit test: paladin/warrior/rogue are melee, every other class is
   ranged - mage, warlock, priest, druid, hunter and shaman included).

Run: python3 -m unittest tools/test_air_target_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
AIR_TARGET = (ROOT / "ai/playerbot/BossTacticsAirTarget.h").read_text()
INVALID_TARGET = (STRATEGY / "values/InvalidTargetValue.cpp").read_text()
CURRENT_TARGET = (STRATEGY / "values/CurrentTargetValue.cpp").read_text()
REACH = (STRATEGY / "actions/ReachTargetActions.h").read_text()
ONYXIA_STRATEGY = (STRATEGY / "generic/OnyxiasLairDungeonStrategies.cpp").read_text()
ONYXIA_TRIGGERS = (STRATEGY / "triggers/OnyxiasLairDungeonTriggers.h").read_text()
TRIGGER_CONTEXT = (STRATEGY / "triggers/TriggerContext.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def node(text, trigger_name):
    """The NextAction array of one TriggerNode, by its trigger name."""
    match = re.search(r'"%s",\s*NextAction::array\(0,(.*?)\)\)\);' % re.escape(trigger_name), text, re.S)
    if not match:
        raise AssertionError("no TriggerNode for %s" % trigger_name)
    return match.group(1)


class AirTargetWiring(unittest.TestCase):
    def test_policy_keeps_the_air_target_without_a_floor_path(self):
        fn = body(POLICY, "inline bool AirTargetKeep(", end="\n    }\n")
        self.assertIn("rowMatches && hasAura && !evading", fn)
        self.assertIn("(void)evadeUnreachable;", fn)

    def test_glue_asks_for_the_bots_tactics_and_the_aura(self):
        fn = body(AIR_TARGET, "inline bool IsAirTargetKept(", end="\n    }\n")
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)
        self.assertIn("GetBossTactics().Rows(bot->GetMapId(), unit->GetEntry())", fn)
        self.assertIn("BossTacticKind::AirTarget", fn)
        self.assertIn("AirTargetKeep(", fn)
        self.assertIn("IsInEvadeMode()", fn)
        self.assertIn("IsEvadeBecauseTargetNotReachable()", fn)
        self.assertIn("unit->HasAura(row.spellId)", fn)

    def test_invalid_target_ignores_unreachable_for_the_air_target(self):
        fn = body(INVALID_TARGET, "bool InvalidTargetValue::Calculate()")
        evade = fn.index("creature->IsInEvadeMode()")
        unreachable = fn.index("IsEvadeBecauseTargetNotReachable()")
        self.assertLess(evade, unreachable)  # an evade mode still drops her
        start = fn.index("if (creature && creature->IsEvadeBecauseTargetNotReachable()")
        condition = fn[start:fn.index("\n    {", start)]
        self.assertIn("!IsAirTargetKept(ai, bot, creature)", condition)

    def test_invalid_target_checks_a_kept_air_target_at_react_distance(self):
        fn = body(INVALID_TARGET, "bool InvalidTargetValue::Calculate()")
        self.assertIn("AirTargetKept", fn)
        self.assertIn("? sPlayerbotAIConfig.reactDistance : sPlayerbotAIConfig.sightDistance", fn)

    def test_current_target_keeps_it_to_react_distance(self):
        fn = body(CURRENT_TARGET, "Unit* CurrentTargetValue::Get()")
        self.assertIn("IsAirTargetKept(ai, bot, unit)", fn)
        self.assertIn("maxDistance = sPlayerbotAIConfig.reactDistance;", fn)
        # The sight ceiling stays the default for every other target.
        self.assertIn("float maxDistance = sPlayerbotAIConfig.sightDistance;", fn)

    def test_airborne_row_spreads_only(self):
        airborne = node(ONYXIA_STRATEGY, "onyxia airborne")
        self.assertIn("raid spread", airborne)
        self.assertNotIn("shoot", airborne)

    def test_wand_row_is_melee_kits_only(self):
        wand = node(ONYXIA_STRATEGY, "onyxia airborne wand")
        self.assertIn("shoot", wand)
        self.assertIn(
            'creators["onyxia airborne wand"] = [](PlayerbotAI* ai) { return new OnyxiaAirborneWandTrigger(ai); };',
            TRIGGER_CONTEXT,
        )
        fn = body(ONYXIA_TRIGGERS, "class OnyxiaAirborneWandTrigger", end="\n    };\n")
        self.assertIn("!ai->IsRanged(bot)", fn)
        self.assertIn("OnyxiaAirborneTrigger::IsActive()", fn)

    def test_reach_walks_the_floor_under_the_air_target(self):
        fn = body(REACH, "AirTargetReach MoveUnderAirborneTarget(Unit* target, float distance)", end="\n        }\n")
        self.assertIn("IsAirTargetKept(ai, bot, target)", fn)
        self.assertIn("AirTargetReachPlan(", fn)
        self.assertIn("AirTargetReach::NoGroundPoint", fn)
        self.assertIn("UpdateGroundPositionZ", fn)
        self.assertIn("MoveTo(bot->GetMapId(), x, y, z)", fn)
        self.assertNotIn("ChaseTo", fn)
        # Never the target's own (flight) height.
        self.assertNotIn("MoveTo(bot->GetMapId(), target->GetPositionX()", fn)

    def test_lost_sight_is_not_a_stay(self):
        # Codex round 2: a lost line of sight used to answer NoGroundPoint, so a
        # bot behind a pillar never moved again. Sight now goes into the plan,
        # and NeedLosPoint walks to a floor point that sees the target.
        fn = body(REACH, "AirTargetReach MoveUnderAirborneTarget(Unit* target, float distance)", end="\n        }\n")
        self.assertNotIn("!bot->IsWithinLOSInMap(target, true) ||", fn)
        self.assertIn("bool const inLos = bot->IsWithinLOSInMap(target, true);", fn)
        self.assertIn("AirTargetReachPlan(true, inLos, distance, flightHeight,", fn)
        self.assertIn("if (plan == AirTargetReach::NeedLosPoint)\n                return MoveToAirTargetLosPoint(map, target, distance, flightHeight);", fn)
        # The budget is per target, and only sustained sight refills it: a
        # plain in-sight tick never resets it (Codex round 3).
        self.assertIn("if (airLosTarget != target->getObjectGuid())", fn)
        self.assertEqual(fn.count("AirTargetLosBudgetReset(airLosBudget);"), 1)
        reset = fn.index("AirTargetLosBudgetReset(airLosBudget);")
        self.assertLess(reset, fn.index("bool const inLos"))
        self.assertIn("if (inLos)\n                AirTargetLosSeen(airLosBudget, WorldTimer::getMSTime());", fn)

    def test_flight_height_is_measured_from_the_bots_floor(self):
        fn = body(REACH, "AirTargetReach MoveUnderAirborneTarget(Unit* target, float distance)", end="\n        }\n")
        self.assertIn("float groundZ = botZ;\n            bot->UpdateGroundPositionZ(targetX, targetY, groundZ);", fn)
        self.assertNotIn("float groundZ = flightZ;", fn)

    def test_los_recovery_walks_a_bounded_floor_point(self):
        fn = body(REACH, "AirTargetReach MoveToAirTargetLosPoint(Map* map, Unit* target, float distance, float flightHeight)",
                  end="\n        }\n")
        self.assertIn("AirTargetLosTryAttempt(airLosBudget, WorldTimer::getMSTime())", fn)
        self.assertIn("AirTargetLosCandidates(distance, flightHeight, target->GetPositionX(), target->GetPositionY())", fn)
        self.assertIn("SelectAirTargetLosPoint(candidates, botPosition.getX(), botPosition.getY(),\n"
                      "                                                     kAirTargetLosPathProbes, usable, reachable)", fn)
        # Floor, sight from the floor point, hazard, navmesh path.
        self.assertIn("bot->UpdateGroundPositionZ(point.x, point.y, z);", fn)
        self.assertIn("AirTargetLosPointFits(distance, botZ, point.x, point.y, z, goalX, goalY, goalZ)", fn)
        fits = fn.index("AirTargetLosPointFits(")
        self.assertLess(fn.index("bot->UpdateGroundPositionZ(point.x, point.y, z);"), fits)
        self.assertLess(fits, fn.index("map->isInLineOfSight("))
        self.assertLess(fits, fn.index("isPathTo("))
        self.assertIn("map->isInLineOfSight(point.x, point.y, z + botHeight, goalX, goalY, goalZ + targetHeight, true)", fn)
        self.assertIn("IsHazardNearPosition(", fn)
        self.assertIn("destination.isPathTo(destination.getPathStepFrom(botPosition, bot), 2.0f)", fn)
        # The move goes to the floor point's ground z, never the flight height.
        self.assertIn("MoveTo(bot->GetMapId(), point.x, point.y, groundZ[pick])", fn)
        self.assertNotIn("ChaseTo", fn)
        self.assertNotIn("MoveTo(bot->GetMapId(), target->", fn)
        self.assertNotIn("goalZ)", fn.split("SelectAirTargetLosPoint(")[1])

    def test_the_caster_reach_tries_the_floor_walk_first(self):
        fn = body(REACH, "class ReachSpellAction", end="\n    };\n")
        self.assertIn("bool MoveIntoRange(Unit* target, float distance) override", fn)
        self.assertIn("AirTargetReach const reach = MoveUnderAirborneTarget(target, distance)", fn)
        self.assertIn("AirTargetGenericReachApplies(reach)", fn)
        walk = fn.index("MoveUnderAirborneTarget(target, distance)")
        fallback = fn.index("ReachTargetAction::MoveIntoRange(target, distance)")
        self.assertLess(walk, fallback)


if __name__ == "__main__":
    unittest.main()
