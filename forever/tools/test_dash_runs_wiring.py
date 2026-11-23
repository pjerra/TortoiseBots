"""Wiring pins for dashboard round 1 (forever/src/dash, FOREVER-DASH-RUNS-SPEC.md): the Commander and Dungeon runs feeds.

Proves, by reading the sources, that (1) the feed makes no database call (SLICE4-COMMON: every DB read goes through WorldQueryCompat, and this
code reads none), (2) it is off without the engine build, with Dashboard.Enable = 0 and with Dashboard.Runs = 0, (3) it is hooked once into the
world tick (after the run tick, inside the engine build) and into both config loads, (4) it does not touch the roster sender's files, (5) the two
config keys are in the conf.dist.

Run: python3 -m unittest forever/tools/test_dash_runs_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
DASH = ROOT / "forever/src/dash"


def text(rel):
    return (ROOT / rel).read_text()


class DashRunsWiring(unittest.TestCase):
    def test_no_database_call(self):
        files = sorted(DASH.glob("*"))
        self.assertGreaterEqual(len(files), 4)
        for f in files:
            body = f.read_text()
            for word in ("WorldDatabase", "CharacterDatabase", "LoginDatabase", "HotfixDatabase", "WorldQueryCompat", "PreparedStatement", "->Query(", ".Query("):
                self.assertNotIn(word, body, "%s names %s" % (f.name, word))

    def test_off_without_the_engine_build(self):
        cpp = text("forever/src/dash/TbDashRuns.cpp")
        # the real body only in the SLICE1 build, the stubs otherwise
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", cpp)
        self.assertRegex(cpp, r"#else\s+namespace TortoiseBots\s*\{\s*namespace DashRuns\s*\{\s*void LoadConfig\(\) \{\}\s*void Update\(uint32\) \{\}\s*bool Active\(\) \{ return false; \}")
        link = text("forever/src/dash/TbDashLink.cpp")
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", link)

    def test_off_with_the_config_keys(self):
        cpp = text("forever/src/dash/TbDashRuns.cpp")
        self.assertIn('GetBoolDefault("TortoiseBots.Dashboard.Runs", true)', cpp)
        active = re.search(r"bool Active\(\)\s*\{(.*?)\n\}", cpp, re.S).group(1)
        self.assertIn("Get().enabled", active)          # Dashboard.Runs
        self.assertIn("link::Active()", active)         # Dashboard.Enable and an open socket
        self.assertIn("BotMgr::Instance().Enabled()", active)
        upd = re.search(r"void Update\(uint32 diff\)\s*\{(.*?)\n\}", cpp, re.S).group(1)
        self.assertRegex(upd, r"if \(!Active\(\)\)\s*return;")
        link = text("forever/src/dash/TbDashLink.cpp")
        self.assertIn('GetBoolDefault("TortoiseBots.Dashboard.Enable", true)', link)

    def test_hooked_once(self):
        scripts = text("forever/src/TbScripts.cpp")
        self.assertEqual(scripts.count("TortoiseBots::DashRuns::Update(diff);"), 1)
        self.assertEqual(scripts.count("TortoiseBots::DashRuns::LoadConfig();"), 2)
        i = scripts.index("TortoiseBots::run::Update(diff);")
        j = scripts.index("TortoiseBots::DashRuns::Update(diff);")
        k = scripts.index("#endif", i)
        self.assertTrue(i < j < k, "the feed is ticked after the run tick, inside the engine build")

    def test_roster_sender_is_not_ours(self):
        for rel in ("forever/src/TbDashboard.cpp", "forever/src/TbDashboard.h"):
            self.assertNotIn("DashRuns", text(rel))

    def test_conf_keys(self):
        conf = text("forever/conf/tortoisebots.conf.dist")
        self.assertRegex(conf, r"(?m)^TortoiseBots\.Dashboard\.Runs = 1$")
        self.assertRegex(conf, r'(?m)^TortoiseBots\.Dashboard\.RunSummaryDir = ""$')

    def test_dashboard_host_accepts_a_host_name(self):
        # both senders (the roster socket and this shim) go through the one resolver, so a dashboard container name works
        for rel in ("forever/src/TbDashboard.cpp", "forever/src/dash/TbDashLink.cpp"):
            body = text(rel)
            self.assertIn("ResolveEndpoint(host, uint16(port), s.target, resolveError)", body)
            self.assertNotIn("make_address(", body)
        conf = text("forever/conf/tortoisebots.conf.dist")
        self.assertIn("host name", conf)

    def test_summary_is_written_once_per_run(self):
        cpp = text("forever/src/dash/TbDashRuns.cpp")
        # the job is queued before the file lands; only the atomic rename marks it done, and a failure leaves it for the next cycle
        self.assertIn("s.summaries.Add(std::move(job))", cpp)
        self.assertIn("s.summaries.Succeeded(leaderGuid, startedMs)", cpp)
        self.assertIn("s.summaries.Failed(leaderGuid, startedMs)", cpp)
        self.assertNotIn("MarkIfNew", cpp)

    def test_the_run_end_is_captured_before_a_new_start_removes_it(self):
        run = text("forever/src/host/run/TbRun.cpp")
        finish = re.search(r"uint32 Finish\(RunRec& rec.*?\n\}", run, re.S).group(0)
        self.assertEqual(finish.count("EndHook(rec)"), 1)       # at the run's end, not only at the next 2 s poll
        self.assertIn("void SetEndListener(EndListener listener)", run)
        cpp = text("forever/src/dash/TbDashRuns.cpp")
        self.assertIn("run::SetEndListener(&OnRunEnded)", cpp)
        self.assertIn("void OnRunEnded(run::RunRec const& rec)", cpp)

    def test_empty_list_goes_through_the_gate(self):
        cpp = text("forever/src/dash/TbDashRuns.cpp")
        self.assertIn("s.gate.Next(bindings.size())", cpp)


if __name__ == "__main__":
    unittest.main()
