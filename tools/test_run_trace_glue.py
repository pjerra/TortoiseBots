#!/usr/bin/env python3
"""Structural checks for the run trace's glue (PR #927 review findings).

The record layer is tested standalone (tools/test_run_trace_records.cpp); the
glue in runtime/RunTrace.cpp and the outcome sites in Engine::DoNextAction need
the game headers, so these checks pin their shape instead:

- Sample() replaces the hook scope before it looks at an empty frame, so a
  requester who logs out stops the action records at the next frame, not at
  the scope_lost timeout.
- ResolveBots() picks its source with PickScopeSource(): a party run whose
  group is gone has no bots; it does not shrink to the anchor bot.
- Every way an action leaves the walk early reports an outcome, or re-queues
  the action (it is reported when it is popped again). The "vetoed earlier
  this tick" branch only zeroes the relevance and falls through to the
  ordinary result sites (Vetoed at the IMPOSSIBLE site), so it reports
  nothing itself; a report there would count each veto twice.

Run: python3 tools/test_run_trace_glue.py
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN_TRACE = ROOT / "runtime/RunTrace.cpp"
ENGINE = ROOT / "ai/playerbot/strategy/Engine.cpp"


def function_body(src, signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[brace:i + 1]
    raise AssertionError("unbalanced braces in " + signature)


def block_after(src, marker):
    """The brace block that starts at the first '{' after marker."""
    at = src.index(marker)
    return function_body(src, src[at:at + len(marker)])


class SampleScopeTest(unittest.TestCase):
    def setUp(self):
        self.body = function_body(RUN_TRACE.read_text(), "void RunTraceService::Sample()")

    def test_scope_replaced_before_empty_check(self):
        replace = self.body.find("scopeGuids_.Replace(")
        empty = self.body.find("if (bots.empty())")
        self.assertNotEqual(replace, -1, "Sample() must replace the hook scope every frame")
        self.assertNotEqual(empty, -1)
        self.assertLess(replace, empty, "an empty frame must empty the hook scope before returning")

    def test_scope_not_filled_only_on_the_populated_path(self):
        self.assertNotIn("scopeGuids_.insert(", self.body)
        self.assertEqual(self.body.count("scopeGuids_."), 1)


class ResolveBotsTest(unittest.TestCase):
    def setUp(self):
        self.body = function_body(RUN_TRACE.read_text(), "void RunTraceService::ResolveBots(")

    def test_source_comes_from_the_pure_policy(self):
        self.assertIn("RunTrace::PickScopeSource(wholeParty_,", self.body)
        self.assertIn("source == RunTrace::ScopeSource::Group", self.body)
        self.assertIn("source == RunTrace::ScopeSource::Anchor", self.body)

    def test_no_anchor_fallback_outside_the_policy(self):
        self.assertIsNone(re.search(r"else\s+if\s*\(\s*BotManager::Instance\(\)\.IsControllableBot\(anchor\)", self.body))


class EngineOutcomeSitesTest(unittest.TestCase):
    def setUp(self):
        self.walk = function_body(ENGINE.read_text(), "bool Engine::DoNextAction(")

    def test_vetoed_earlier_branch_falls_through(self):
        branch = block_after(self.walk, "if (std::find(vetoedActions.begin(), vetoedActions.end(), action) != vetoedActions.end())")
        self.assertIn("relevance = 0.0f;", branch)
        for word in ("continue;", "break;", "delete actionNode", "return"):
            self.assertNotIn(word, branch, "a vetoed-earlier instance must reach a result site")
        self.assertNotIn("actionExecutionListeners.Outcome(", branch,
                         "the result site reports it; a report here would count it twice")

    def test_impossible_site_reports_vetoed_for_a_zeroed_possible_action(self):
        self.assertIn("isPossible ? ActionOutcome::Vetoed : ActionOutcome::Impossible", self.walk)

    def test_every_early_exit_reports_or_requeues(self):
        lines = self.walk.splitlines()
        exits = [i for i, line in enumerate(lines) if line.strip() == "continue;"]
        self.assertGreaterEqual(len(exits), 3)
        for i in exits:
            window = "\n".join(lines[max(0, i - 6):i])
            self.assertTrue("actionExecutionListeners.Outcome(" in window or "PushAgain(" in window,
                            "continue without an outcome or a re-queue near line %d of the walk" % i)

    def test_each_result_site_reports_once(self):
        for outcome in ("Backoff", "Prereq", "Ok", "Failed", "Useless"):
            self.assertEqual(self.walk.count("ActionOutcome::%s," % outcome), 1, outcome)
        self.assertEqual(self.walk.count("ActionOutcome::Vetoed"), 1)


if __name__ == "__main__":
    unittest.main()
