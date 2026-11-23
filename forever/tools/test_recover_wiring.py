#!/usr/bin/env python3
"""Wiring pins for proof-1 Part B (forever/docs/PROOF-1-NOTES.md). The run's self-recover after the policy stopped regrouping by itself at a step:
the glue answers that pause itself, once per step and at most kMaxRecoversPerRun per run, right after the gather block (after the policy's Tick,
before the executor and before Observe sees the pause), and the pause text it matches is still the policy's own.

Run: python3 -m unittest forever/tools/test_recover_wiring.py
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FOREVER = ROOT / "forever"


def text(rel):
    return (ROOT / rel).read_text()


class RecoverWiring(unittest.TestCase):
    def test_call_order(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("recover::Decide(rec.recover", run)
        self.assertIn("DungeonRun::Resume(run)", run)
        tick = run.index("DungeonRun::Tick(run, facts, dt)")
        gather = run.index("gather::Decide(rec.gatherExt")
        recover = run.index("recover::Decide(rec.recover")
        issue = run.index("Issue(rec, order, nowMs")
        observe = run.rindex("Observe(rec, nowMs, &facts)")
        self.assertLess(tick, gather)
        self.assertLess(gather, recover)          # after the gather block
        self.assertLess(recover, issue)           # before the executor
        self.assertLess(recover, observe)         # before Observe sees the pause

    def test_pause_text_is_the_policy_own(self):
        # pin the wording in the policy source and in the matcher: an edit to the policy's text fails here
        policy = text("runtime/DungeonRunPolicy.h")
        self.assertIn("regroups by itself at this step did not hold", policy)
        # both pause sites: the lead revived away and the wipe
        self.assertGreaterEqual(policy.count("regroups by itself at this step did not hold"), 2)
        pure = text("forever/src/host/run/TbRunRecoverPure.h")
        self.assertIn('" regroups by itself at this step did not hold"', pure)

    def test_recover_issues_the_revive_from_the_run(self):
        run = text("forever/src/host/run/TbRun.cpp")
        body = run[run.index("recover::Decide(rec.recover"):]
        body = body[:body.index("// The searches the policy asks the glue for")]
        self.assertIn('Issue(rec, "revive party", nowMs)', body)
        self.assertIn("BaselineOrders(rec);", body)
        self.assertIn("PLAYER_FLAGS_GHOST", body)

    def test_option_on_by_default(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn('sConfigMgr->GetBoolDefault("TortoiseBots.Run.SelfRecover", true)', run)
        conf = text("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.Run.SelfRecover = 1", conf)


if __name__ == "__main__":
    unittest.main()
