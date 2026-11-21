#!/usr/bin/env python3
"""Wiring pins for the review fixes of fix/run-review-1 (Gemini review of the merged run glue, findings 1, 2, 4, 5, 6, 7a).
The core calls cannot run without a server, so these pin the rules in the source; each assertion fails on the code before the fix.

  1 the stand searches judge a path with the move rule (a wet prefix from the lead's own spot is allowed), not "no wet sample at all"
  2 pausing a run forgets the executor's pending list (resume must not settle orders the pause cancelled)
  4 move-direct reads FindMap, and fails on a teleporting bot or another map
  5 the rez roster compares the Map object, not the map id
  6 CheckLeadMove answers a too-far point before the PathFinder and the liquid samples
  3b `.tb where` (TbScripts.cpp, not the dashboard) reads FindMap
  7 BindBotMaster replaces the engine's master pointer also with an offline (null) master
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


class RevFix1(unittest.TestCase):
    def test_1_stand_path_uses_the_move_rule(self):
        body = function_body(text("run/TbRunStands.cpp"), r"bool PathInLiquid\(Map\* map, Points const& points\)\s*\{")
        self.assertIn("PathPrefixRefused", body)
        self.assertNotIn("liquid::PathRefused", body)

    def test_2_cancel_run_work_forgets_the_pending_orders(self):
        body = function_body(text("run/TbRun.cpp"), r"void CancelRunWork\(RunRec const& rec, bool endPulls\)\s*\{")
        self.assertIn("ForgetRunOrders(rec.leader)", body)

    def test_2_pause_paths_reach_cancel_run_work(self):
        run = text("run/TbRun.cpp")
        pause = function_body(run, r"StateOutcome PauseRun\(RunRec& rec, uint32 nowMs\)\s*\{")
        self.assertIn("CancelRunWork(rec, true)", pause)
        observe = function_body(run, r"void Observe\(RunRec& rec, uint32 nowMs, DungeonRun::Facts const\* f\)\s*\{")
        self.assertRegex(observe, r"State::Paused\)\s*\{[^}]*CancelRunWork\(rec, true\)")

    def test_4_move_direct_checks_the_map(self):
        orders = text("TbOrders.cpp")
        start = orders.index('o.verb == "move-direct"')
        end = orders.index('o.verb == "pullback"')
        body = orders[start:end]
        self.assertNotIn("bot->GetMap()", body)
        self.assertIn("bot->FindMap()", body)
        self.assertIn("IsBeingTeleported()", body)
        self.assertIn("PointOnBotMap(o.map, o.instance, bot->GetMapId(), bot->GetInstanceId())", body)
        self.assertIn('"fail:wrong-map"', body)
        # the check comes before the liquid read and before the walk
        self.assertLess(body.index('"fail:wrong-map"'), body.index("SegmentPrefixRefusedOnMap"))
        self.assertLess(body.index('"fail:wrong-map"'), body.index("MovePoint("))

    def test_5_rez_roster_compares_map_objects(self):
        body = function_body(text("run/TbRunFacts.cpp"), r"void ReadRezRoster\(Ctx& c, Facts& f\)\s*\{")
        self.assertNotRegex(body, r"GetMapId\(\)\s*==")
        self.assertIn("SameMapObject(body, lead)", body)
        self.assertIn("SameMapObject(memberBots[i], body)", body)
        helper = function_body(text("run/TbRunFacts.cpp"), r"bool SameMapObject\(WorldObject const\* a, WorldObject const\* b\)\s*\{")
        self.assertIn("FindMap()", helper)

    def test_6_too_far_before_path_and_liquid(self):
        body = function_body(text("run/TbRunOrders.cpp"), r"MoveCheck CheckLeadMove\(Player\* mover, MovePoint point, bool fromRun, bool gather, bool wade\)\s*\{")
        far = body.index("MoveVerdict::TooFar")
        self.assertLess(far, body.index("PathFinder path(mover)"))
        self.assertLess(far, body.index("PathPrefixRefused"))
        self.assertLess(far, body.index("SegmentPrefixRefused"))
        self.assertLess(far, body.index("PointRefusedOnMap"))

    def test_7_bind_bot_master_always_sets_the_pointer(self):
        body = function_body(text("TbHostBotManager.cpp"), r"bool BotManager::BindBotMaster\(ObjectGuid botGuid, ObjectGuid masterGuid\)\s*\{")
        self.assertNotRegex(body, r"if \(bot && master\)")
        self.assertIn("ai->SetMaster(master)", body)

    def test_3b_tb_where_reads_find_map(self):
        scripts = (ROOT / "forever" / "src" / "TbScripts.cpp").read_text()
        body = function_body(scripts, r"static bool HandleWhere\(ChatHandler\* handler, std::string name\)\s*\{")
        self.assertNotIn("bot->GetMap()", body)
        self.assertIn("bot->FindMap()", body)


if __name__ == "__main__":
    unittest.main()
