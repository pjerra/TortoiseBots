#!/usr/bin/env python3
"""Wiring pin for the `reload all` hook (TbScripts.cpp OnAfterReload): no dungeon plan reload while a run holds a plan.

A run keeps the plan it started on and matches its units by (leg, seq). The hook used to install new unit rows on any
`reload all` even while a run read the old plan, so the run could resolve another step's spawns. The guard reads the live
run table (PlanRunLeaders, the same runs PlansInUse counts), so there is no pure seam to unit-test: this is a wiring check.
It fails on the commit before the fix (no guard, no PlanRunLeaders).
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = (ROOT / "src" / "TbScripts.cpp").read_text()
HOOKS_H = (ROOT / "src" / "host" / "run" / "TbRunHooks.h").read_text()
RUN_CPP = (ROOT / "src" / "host" / "run" / "TbRun.cpp").read_text()


def hook_body():
    start = SCRIPTS.index("void OnAfterReload(std::string_view command) override")
    return SCRIPTS[start:SCRIPTS.index("void OnWorldInitialized() override", start)]


class ReloadHookRunGuard(unittest.TestCase):
    def test_guard_names_the_runs_and_skips_the_reload(self):
        body = hook_body()
        guard = body.index("TortoiseBots::run::PlanRunLeaders()")
        self.assertIn("if (!runs.empty())", body)
        after_guard = body[body.index("if (!runs.empty())"):]
        self.assertIn("return;", after_guard[:after_guard.index("ai::PlanReloadHost host;")])
        self.assertIn("plans not reloaded: run(s) in progress", body)
        self.assertIn("use .tb plan reload after they end", body)
        # the guard comes before the unit rows are read and before anything is installed
        self.assertLess(guard, body.index("TortoiseBots::run::FillUnitReloadHost(host)"))
        self.assertLess(guard, body.index("ai::ReloadDungeonPlans("))

    def test_reload_still_hands_every_runs_plan_to_the_commit(self):
        body = hook_body()
        self.assertIn("ai::ReloadDungeonPlans(TortoiseBots::run::PlansInUse(), &host)", body)

    def test_leader_list_uses_the_same_runs_as_plans_in_use(self):
        self.assertIn("std::vector<std::string> PlanRunLeaders();", HOOKS_H)
        start = RUN_CPP.index("std::vector<std::string> PlanRunLeaders()")
        fn = RUN_CPP[start:RUN_CPP.index("\n}\n", start)]
        self.assertIn("for (RunRec const* rec : All())", fn)
        self.assertIn("if (rec->run.plan)", fn)
        self.assertIn("out.push_back(rec->leaderName);", fn)


if __name__ == "__main__":
    unittest.main()
