#!/usr/bin/env python3
"""Wiring pins for the S6 review fixes (R6). The core calls cannot run without a server, so these pin the order of the calls and the rules in the source;
each assertion fails on the code before the fix (the commit this file arrived with).

  1 a run's queued orders and pulls are cancelled by stop, by pause and while a stop waits, BEFORE the final stay
  2/3/7 one liquid rule (bits + level + sampled legs) for the facts, the stand search, the direct move and the come
  4 the run remembers its instance and ends when the lead stands in another one
  5 the run's leader never follows its commander (relax takes the follow off, the engine hook holds it)
  6 a stop pending in combat ends after kStopPendingMaxMs
  8 focus answers ok only for a live unit that merely is not seen yet
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src" / "host"


def text(rel):
    return (SRC / rel).read_text()


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


class Review(unittest.TestCase):
    def setUp(self):
        self.run = text("run/TbRun.cpp")
        self.orders = text("TbOrders.cpp")
        self.facts = text("run/TbRunFacts.cpp")
        self.stands = text("run/TbRunStands.cpp")
        self.runorders = text("run/TbRunOrders.cpp")
        self.liquid = text("run/TbRunLiquid.cpp")

    # 1
    def test_finish_cancels_before_the_stay(self):
        body = function_body(self.run, r"uint32 Finish\(RunRec& rec, uint32 nowMs, bool stopped, std::string const& why\)\s*\{")
        self.assertIn("CancelRunWork(rec, true)", body)
        self.assertLess(body.index("CancelRunWork(rec, true)"), body.index("StayParty(rec)"))

    def test_pause_and_pending_stop_cancel(self):
        pause = function_body(self.run, r"StateOutcome PauseRun\(RunRec& rec, uint32 nowMs\)\s*\{")
        self.assertIn("CancelRunWork(rec, true)", pause)
        observe = function_body(self.run, r"void Observe\(RunRec& rec, uint32 nowMs, DungeonRun::Facts const\* f\)\s*\{")
        self.assertRegex(observe, r"State::Paused\)\s*\{[^}]*CancelRunWork\(rec, true\)")
        stop = function_body(self.run, r"StopOutcome StopRun\(RunRec& rec, uint32 nowMs, std::string const& reason\)\s*\{")
        self.assertIn("CancelRunWork(rec, false)", stop)

    def test_cancel_in_the_order_layer(self):
        self.assertIn("CancelFromRun(rec->second.queue", self.orders)
        self.assertIn("fail:cancelled", self.orders)
        end_pull = function_body(self.orders, r"void EndRunPull\(Player\* bot\)\s*\{")
        self.assertIn('"end pull"', end_pull)
        self.assertIn("EndPullWindow", end_pull)
        helper = function_body(self.run, r"void CancelRunWork\(RunRec const& rec, bool endPulls\)\s*\{")
        self.assertIn("h3::CancelRunOrders(guid)", helper)
        self.assertIn("h3::EndRunPull(bot)", helper)

    # 2, 3, 7
    def test_one_liquid_rule(self):
        self.assertNotIn("== LIQUID_MAP_IN_WATER", self.facts)
        self.assertIn("liquid::PointRefusedOnMap", self.facts)
        self.assertNotIn("LIQUID_MAP_IN_WATER", self.stands)
        self.assertIn("liquid::PathPrefixRefused", self.stands)       # review fix 1: the move rule (a wet prefix from the lead's own spot)
        self.assertNotRegex(self.liquid, r"LiquidStatusCompat\([^)]*z \+ 0\.5f")
        self.assertIn("PointRefused(static_cast<std::uint32_t>(status), data.level, z)", self.liquid)

    def test_straight_moves_read_the_line(self):
        self.assertIn("liquid::SegmentPrefixRefusedOnMap(lead->GetMap()", self.runorders)
        check = function_body(self.runorders, r"MoveCheck CheckLeadMove\(Player\* mover, MovePoint point, bool fromRun, bool gather, bool wade\)\s*\{")
        self.assertIn("liquid::PathPrefixRefused", check)
        self.assertIn("liquidOnWay", check)
        # round 2: no blanket exemption for a wet origin, validation and execution use the same prefix rule
        self.assertNotIn("liquidOnWay = false", check)
        self.assertIn("run::liquid::SegmentPrefixRefusedOnMap", self.orders)
        self.assertNotIn("!run::liquid::PointRefusedOnMap(map, bot->GetPositionX()", self.orders)

    # 4
    def test_instance_is_remembered(self):
        start = function_body(self.run, r"StartOutcome StartRun\(Player\* leader, ai::DungeonPlan const\* plan, uint32 nowMs\)\s*\{")
        self.assertIn("rec->instanceId = leader->GetInstanceId()", start)
        tick = function_body(self.run, r"void TickRun\(RunRec& rec, uint32 nowMs\)\s*\{")
        self.assertIn("lead->GetInstanceId() != rec.instanceId", tick)

    # 5
    def test_leader_does_not_follow_its_commander(self):
        relax = function_body(self.orders, r"void RelaxTankForPull\(Player\* tank, PlayerbotAI\* ai, bool fromRun\)\s*\{")
        self.assertIn('"-follow"', relax)
        hook = function_body(self.orders, r"void BeforeEngineTick\(Player\* bot, PlayerbotAI\* ai\)\s*\{")
        self.assertIn("ShouldHoldLead(", hook)
        self.assertIn("HoldRunLead(leader->GetGUID(), true)", self.run)
        self.assertIn("HoldRunLead(rec.leader, false)", self.run)

    # 6
    def test_pending_stop_has_a_cap(self):
        header = text("run/TbRun.h")
        self.assertIn("constexpr uint32 kStopPendingMaxMs = 90000", header)
        tick = function_body(self.run, r"void TickRun\(RunRec& rec, uint32 nowMs\)\s*\{")
        self.assertIn("kStopPendingMaxMs", tick)

    # 8
    def test_focus_is_ok_only_for_a_live_unseen_unit(self):
        self.assertIn("FocusMarkWaits(", self.orders)
        self.assertNotRegex(self.orders, r"seen && !seen->Get\(\)\)\s*\n\s*return \"ok\"")


if __name__ == "__main__":
    unittest.main()
