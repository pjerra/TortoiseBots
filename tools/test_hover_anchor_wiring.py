"""The anchor-aimed hover reach glue (AiPlayerbot.HoverReachAimsAtAnchor).

The hover reach parks a caster at the edge of range on the boss's side of the
room, so her next hop takes it out of range again (walk 2e: one reach per caster
per 27 s, her hop rate). With a live `anchor` row the walk follows the navmesh
path to the anchor's centre and stops as soon as the target is in reach. The
stop arithmetic is tested in tools/test_ranged_approach_policy.cpp; this pins
the glue in ai/playerbot/strategy/actions/ReachTargetActions.h:
- MoveIntoRangeUnderHover asks the anchor walk only with the key on, after the
  ranged-DPS gate and before it paths to the floor under the target;
- the anchor walk reads the live anchor, walks to the anchor centre, stops by
  HoverAnchorStopDistance, refuses a hazard and is gated by the same sight test;
- the BossReachHover row says which aim it walked (aim=anchor / aim=target);
and the key's read and shipped default.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/ReachTargetActions.h").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class HoverAnchorWiringTest(unittest.TestCase):
    def test_hover_walk_asks_the_anchor_walk_after_the_role_gate(self):
        fn = body(ACTIONS, "bool MoveIntoRangeUnderHover(Unit* target, float distance)", "\n        }")
        call = "if (sPlayerbotAIConfig.hoverReachAimsAtAnchor && MoveTowardAnchorUnderHover(target, distance, groundZ))"
        self.assertIn(call, fn)
        # Only a walk that would otherwise run pays it: after the ranged-DPS gate,
        # and before the walk under her body picks its own ground path.
        self.assertGreater(fn.index(call), fn.index("if (!HoverReachTakerIsRangedDps("))
        self.assertLess(fn.index(call), fn.index("WorldPosition const ground(bot->GetMapId(), targetX, targetY, groundZ);"))
        self.assertLess(fn.index(call), fn.index("ground.getPathStepFrom(WorldPosition(bot), bot)"))

    def test_anchor_walk_paths_to_the_anchor_centre(self):
        fn = body(ACTIONS, "bool MoveTowardAnchorUnderHover(Unit* target, float distance, float groundZ)", "\n        }")
        self.assertIn("BossAnchorCircle circle;", fn)
        self.assertIn("if (!LiveBossAnchor(ai, bot, circle))", fn)
        self.assertIn("WorldPosition const centre(bot->GetMapId(), circle.x, circle.y, circle.z);", fn)
        self.assertIn("centre.getPathStepFrom(WorldPosition(bot), bot)", fn)
        self.assertNotIn("ground.getPathStepFrom", fn)
        self.assertIn("if (path.size() < 2)", fn)
        self.assertIn("HoverAnchorStopDistance(reachDist, triggerDist,", fn)
        self.assertIn("ai->GetRange(\"spell\") - sPlayerbotAIConfig.contactDistance", fn)
        self.assertIn("SelectRangedApproachPoint(points, goal, stop, seesTarget)", fn)
        self.assertIn("if (IsHazardNearPosition(destination))", fn)
        self.assertIn("HoverReachResult::Moved", fn)
        self.assertIn("HoverReachResult::MoveFailed", fn)

    def test_row_names_the_aim(self):
        fn = body(ACTIONS, "void HoverReachRow(HoverReachResult result", "\n        }")
        self.assertIn("aim=%s", fn)
        self.assertIn('anchorAim ? "anchor" : "target"', fn)
        self.assertIn("anchorAim ? stopDist", fn)

    def test_key_is_read_in_load_dungeon_run_keys(self):
        keys = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()",
                    "void PlayerbotAIConfig::LoadObservabilityRaids()")
        self.assertIn(
            'hoverReachAimsAtAnchor = config.GetBoolDefault("AiPlayerbot.HoverReachAimsAtAnchor", false);', keys)

    def test_key_is_read_and_ships_zero(self):
        self.assertIn("bool hoverReachAimsAtAnchor = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.HoverReachAimsAtAnchor = 0", CONF)


if __name__ == "__main__":
    unittest.main()
