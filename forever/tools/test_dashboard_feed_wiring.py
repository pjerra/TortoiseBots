"""Wiring pins for the dashboard feed gaps (TBF #140, the module sender side).

Proves, by reading the sources, that the ForeverCore roster sender emits the fields the dashboard's model reads
(lag percentiles, macro states, travel/trigger detail, real players, the activity stream, engine settings and the
two issue types), that the engine's legacy observability stubs forward the two hooks it owns, and that the sender
neither queries a database nor scans the world per tick.

Run: python3 -m unittest forever/tools/test_dashboard_feed_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
DASH = ROOT / "forever/src/TbDashboard.cpp"


def text(rel):
    return (ROOT / rel).read_text()


class DashboardFeedWiring(unittest.TestCase):
    def test_heartbeat_lag_and_state_window(self):
        cpp = text("forever/src/TbDashboard.cpp")
        hb = re.search(r"std::ostringstream hb;(.*?)Send\(hb\.str\(\)\);", cpp, re.S).group(1)
        for field in (r'\"diff_avg\"', r'\"diff_worst\"', r'\"lag_p50\"', r'\"lag_p95\"', r'\"window_secs\"'):
            self.assertIn(field, hb)
        self.assertIn("tickWindow.Compute()", hb)
        # All seven macro states, from the rolling histogram (not an instantaneous count).
        for state in ("Combat", "Moving", "Busy", "Stalled", "Resting", "Dead", "Idle"):
            self.assertIn("dashboard_state::" + state, hb)
        self.assertIn("AddStateTime(", cpp)
        self.assertIn("RefineIdle(", text("forever/src/dash/TbDashboardStatePure.h"))

    def test_bot_rows_carry_travel_trigger_and_state(self):
        cpp = text("forever/src/TbDashboard.cpp")
        for field in (r'\"last_action\"', r'\"last_trigger\"', r'\"travel_purpose\"', r'\"travel_to\"', r'\"travel_status\"', r'\"travel_dist\"'):
            self.assertIn(field, cpp)
        self.assertIn("ReadTelemetry(", cpp)
        self.assertIn("dashboard_state::Name", cpp)

    def test_real_players_ride_the_roster(self):
        cpp = text("forever/src/TbDashboard.cpp")
        self.assertIn("sWorld->GetAllSessions()", cpp)
        self.assertIn("session->IsBot()", cpp)
        self.assertIn('\\"player\\":true', cpp)
        self.assertIn('\\"kind\\":\\"player\\"', cpp)
        self.assertIn("players.size()", cpp)

    def test_activity_events_are_forwarded_and_enriched(self):
        stub = text("forever/src/host/TbHostTracing.cpp")
        self.assertRegex(stub, r"(?s)EmitBotActivity\(.*?\)\s*\{\s*Dashboard::OnBotActivity\(")
        cpp = text("forever/src/TbDashboard.cpp")
        for event in ('"SellAction"', '"BuyAction"', '"RepairAllAction"', '"TrainerAction"', '"AhAction"', '"MailAction"',
                      '"ReviveFromCorpseAction"', '"WorldPvpKill"'):
            self.assertIn(event, cpp)
        self.assertIn(r'\"item_id\"', cpp)
        self.assertIn(r'\"quality\"', cpp)
        # Events the core hooks already send are not forwarded a second time.
        covered = re.search(r"static std::set<std::string> const covered = \{(.*?)\};", cpp, re.S).group(1)
        for event in ('"Kill"', '"BotDeath"', '"StoreLootAction"', '"QuestRewarded"'):
            self.assertIn(event, covered)

    def test_engine_settings_are_live_getters(self):
        brain = text("forever/src/host/TbEngineBrain.cpp")
        self.assertIn("bool ConfigReady()", brain)
        self.assertIn("std::string ServerBotsJson()", brain)
        self.assertIn("std::string ServerDiagnosticsJson()", brain)
        self.assertIn("BotXpRate()", brain)
        cpp = text("forever/src/TbDashboard.cpp")
        self.assertIn("ServerBotsJson()", cpp)
        self.assertIn("ServerDiagnosticsJson()", cpp)
        self.assertIn(r'\"bot_xp_mult\"', cpp)
        self.assertIn(r'\"rep_gain\"', cpp)

    def test_issues_are_forwarded(self):
        stub = text("forever/src/host/TbHostTracing.cpp")
        self.assertRegex(stub, r"(?s)OnActionFailed\(.*?\)\s*\{\s*Dashboard::OnActionFailed\(")
        cpp = text("forever/src/TbDashboard.cpp")
        self.assertIn('"ACTION_LOOP"', cpp)
        self.assertIn(">= 5 times within 2 seconds", cpp)
        self.assertIn('"UNREACHABLE_TARGET"', cpp)
        self.assertIn("unreachableDurationMs >= 10000", cpp)
        self.assertIn("engineState.targetDistance > 45.0f", cpp)

    def test_sender_does_no_database_work_per_tick(self):
        cpp = text("forever/src/TbDashboard.cpp")
        upd = re.search(r"void Update\(uint32 diff\)\s*\{(.*?)\n\}", cpp, re.S).group(1)
        for word in ("Database", "Query(", "GetItemTemplateStore", "GetAllSessions"):
            self.assertNotIn(word, upd, "the per-tick Update names %s" % word)
        # The session walk and the roster cycle are on the 2 s snapshot, never the world tick.
        cycle = re.search(r"void SendCycle\(\)\s*\{(.*?)\n\}", cpp, re.S).group(1)
        self.assertIn("GetAllSessions()", cycle)


if __name__ == "__main__":
    unittest.main()
