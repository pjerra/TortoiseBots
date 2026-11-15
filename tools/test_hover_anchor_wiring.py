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
- a refused anchor aim writes no row: the fallback walk's one row carries its
  real result plus anchor_reject=no_anchor|boss_mismatch;
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
        call = "if (sPlayerbotAIConfig.hoverReachAimsAtAnchor && MoveTowardAnchorUnderHover(target, distance, groundZ, anchorReject))"
        self.assertIn("const char* anchorReject = nullptr;", fn)
        self.assertIn(call, fn)
        # Only a walk that would otherwise run pays it: after the ranged-DPS gate,
        # and before the walk under her body picks its own ground path.
        self.assertGreater(fn.index(call), fn.index("if (!HoverReachTakerIsRangedDps("))
        self.assertLess(fn.index(call), fn.index("WorldPosition const ground(bot->GetMapId(), targetX, targetY, groundZ);"))
        self.assertLess(fn.index(call), fn.index("ground.getPathStepFrom(WorldPosition(bot), bot)"))
        # The refusal is declared before the anchor call that fills it.
        self.assertLess(fn.index("const char* anchorReject = nullptr;"), fn.index(call))

    def test_anchor_walk_aims_at_the_in_range_point(self):
        fn = body(ACTIONS, "bool MoveTowardAnchorUnderHover(Unit* target, float distance, float groundZ, const char*& anchorReject)", "\n        }")
        self.assertIn("BossAnchorCircle circle;", fn)
        self.assertIn("if (!LiveBossAnchor(ai, bot, circle))", fn)
        self.assertIn("HoverAnchorAimPoint(centre, goal, HoverAnchorReachLimit(reachDist, triggerDist), aim)", fn)
        self.assertIn("WorldPosition const aimPoint(bot->GetMapId(), aim.x, aim.y, aim.z);", fn)
        self.assertIn("aimPoint.getPathStepFrom(WorldPosition(bot), bot)", fn)
        self.assertNotIn("centre.getPathStepFrom", fn)
        self.assertIn("if (path.size() < 2)", fn)
        self.assertIn("HoverAnchorStopDistance(reachDist, triggerDist,", fn)
        self.assertIn("float const triggerDist = range - sPlayerbotAIConfig.contactDistance +", fn)
        self.assertNotIn("GetRange(\"spell\")", fn)
        self.assertIn("SelectRangedApproachPoint(points, goal, stop, seesTarget)", fn)
        self.assertIn("if (IsHazardNearPosition(destination))", fn)
        self.assertIn("HoverReachResult::Moved", fn)
        self.assertIn("HoverReachResult::MoveFailed", fn)

    def test_anchor_walk_needs_the_anchors_own_boss(self):
        fn = body(ACTIONS, "bool MoveTowardAnchorUnderHover(Unit* target, float distance, float groundZ, const char*& anchorReject)", "\n        }")
        # An anchor row of another boss in the same fight names another room's centre.
        self.assertIn("const BossTacticRow* const anchorRow = LiveBossAnchorRow(ai, bot);", fn)
        self.assertIn("if (!anchorRow)", fn)
        self.assertIn("if (anchorRow->bossEntry != target->GetEntry())", fn)
        self.assertLess(fn.index("if (!anchorRow)"), fn.index("LiveBossAnchor(ai, bot, circle)"))

    def test_anchor_walk_refusal_writes_no_row(self):
        fn = body(ACTIONS, "bool MoveTowardAnchorUnderHover(Unit* target, float distance, float groundZ, const char*& anchorReject)", "\n        }")
        # The target hovers (HoverReachApplies already said so), so a refusal row could only say
        # result=not_hover and take the 5 s slot from the walk that follows. It names itself for that
        # walk's row instead, and no not_hover verdict exists at all.
        self.assertNotIn("HoverReachRow(", fn[:fn.index("BossAnchorCircle circle;")])
        self.assertNotIn("HoverReachResult::NotHover", ACTIONS)
        self.assertIn('anchorReject = "no_anchor";', fn)
        self.assertIn('anchorReject = "boss_mismatch";', fn)
        self.assertLess(fn.index('anchorReject = "no_anchor";'), fn.index('anchorReject = "boss_mismatch";'))
        self.assertLess(fn.index('anchorReject = "no_anchor";'), fn.index("BossAnchorCircle circle;"))
        self.assertLess(fn.index('anchorReject = "boss_mismatch";'), fn.index("BossAnchorCircle circle;"))

    def test_fallback_row_carries_the_anchor_reject(self):
        fn = body(ACTIONS, "bool MoveIntoRangeUnderHover(Unit* target, float distance)", "\n        }")
        # One row for the plain walk, with its real result and the refusal appended by HoverReachRow.
        for row in ('HoverReachResult::NoPath, target, distance, groundZ, path.size(), false, 0.0f, nullptr, anchorReject',
                    'HoverReachResult::NoPoint, target, distance, groundZ, path.size(), false, 0.0f, nullptr, anchorReject',
                    'HoverReachResult::Hazard, target, distance, groundZ, path.size(), false, 0.0f, nullptr, anchorReject'):
            self.assertIn(row, fn)
        self.assertIn(': HoverReachResult::MoveFailed,\n                target, distance, groundZ, path.size(), false, 0.0f, nullptr, anchorReject);', fn)
        row = body(ACTIONS, "void HoverReachRow(HoverReachResult result", "\n        }")
        self.assertIn('",anchor_reject=%s"', row)
        self.assertIn('snprintf(info2 + written, sizeof(info2) - written, ",anchor_reject=%s", anchorReject)', row)

    def test_anchor_exits_name_their_fallback(self):
        fn = body(ACTIONS, "bool MoveTowardAnchorUnderHover(Unit* target, float distance, float groundZ, const char*& anchorReject)", "\n        }")
        # A fallback says why it gave the aim up, and stays aim=target (anchorAim false).
        self.assertIn('HoverReachResult::NoPath, target, distance, groundZ, path.size(), false, 0.0f, "no_path"', fn)
        self.assertIn('HoverReachResult::NoPoint, target, distance, groundZ, path.size(), false, 0.0f, "no_point"', fn)
        self.assertIn('HoverReachResult::NoPoint, target, distance, groundZ, 0, false, 0.0f, "no_point"', fn)
        self.assertIn('HoverReachResult::Hazard, target, distance, groundZ, path.size(), false, 0.0f, "hazard"', fn)
        row = body(ACTIONS, "void HoverReachRow(HoverReachResult result", "\n        }")
        self.assertIn('",reason=%s"', row)
        self.assertIn("snprintf(info2 + written", row)

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
