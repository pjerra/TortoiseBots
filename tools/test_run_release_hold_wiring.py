"""A dead run bot waits at its body: the playerbot gate (SM-REVIVE-PATH-2026-10-23 fix 3).

The pure verdict (RunReleaseHold::Decide, HeldByRun with and without a query)
is tested in tools/test_run_release_hold_policy.cpp; this pins where the
auto release (ai/playerbot/strategy/actions/ReleaseSpiritAction.h) obeys it,
since that file only builds against the core:
- AutoReleaseSpiritAction::isUseful asks Decide with the bot's lease, the
  registered run query and the crew's rezzer and combat reads, after the
  ghost check and before every stock rule (group, master, distance);
- Wait returns false, Release returns true, Stock falls through to the stock
  rules, so a bot whose run is over releases as before;
- the seam is header-only and does nothing until a run leader registers its
  query, so the module without the run leader keeps the old gate.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = (ROOT / "ai/playerbot/strategy/actions/ReleaseSpiritAction.h").read_text()
SEAM = (ROOT / "runtime/RunReleaseHold.h").read_text()


def body(text, start, end):
    i = text.index(start)
    j = text.index(end, i + len(start))
    return text[i:j]


class RunReleaseHoldWiringTest(unittest.TestCase):
    def setUp(self):
        auto = body(ACTION, "class AutoReleaseSpiritAction", "class CorpseRunAction")
        self.useful = body(auto, "virtual bool isUseful() override", "\n        }\n")

    def test_the_gate_asks_the_verdict_before_the_stock_rules(self):
        u = self.useful
        self.assertIn('#include "runtime/RunReleaseHold.h"', ACTION)
        gate = u.index("TortoiseBots::RunReleaseHold::Decide(")
        # slice 4 (P4): the ghost-flag read goes through the both-core helper (Turtle: the same HasFlag call); the gate still comes after it
        self.assertLess(u.index("if (HasPlayerFlagCompat(bot, PLAYER_FLAGS_GHOST))"), gate)
        for stock in ("if (!bot->GetGroup())", "if (!ai->GetGroupMaster())", "if (!ai->HasActivePlayerMaster())",
                      'AI_VALUE2(float, "distance", "master target")'):
            self.assertLess(gate, u.index(stock), stock)
        call = body(u, "TortoiseBots::RunReleaseHold::Decide(", ";")
        self.assertIn("BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow())", call)
        self.assertIn("TortoiseBots::RunReleaseHold::HeldByRun(bot)", call)
        self.assertIn("DungeonCrewHasLivingResurrecter()", call)
        self.assertIn("DungeonCrewInCombat()", call)

    def test_the_verdicts_map_to_wait_release_and_fall_through(self):
        u = self.useful
        self.assertIn("if (verdict == TortoiseBots::RunReleaseHold::AutoRelease::Wait)\n                return false;", u)
        self.assertIn("if (verdict == TortoiseBots::RunReleaseHold::AutoRelease::Release)\n                return true;", u)
        # The old Dungeon-lease line is gone (Decide holds it now), so there is one gate only.
        self.assertNotIn("== TortoiseBots::BotActivity::Dungeon)\n                return !DungeonCrewHasLivingResurrecter()", u)

    def test_the_seam_does_nothing_until_registered(self):
        self.assertIn("static Query query = nullptr;", SEAM)
        self.assertIn("return bot && query && query(bot);", body(SEAM, "inline bool HeldByRun(", "}"))
        decide = body(SEAM, "inline AutoRelease Decide(", "\n}\n")
        self.assertIn("lease == BotActivity::PlayerMaster && heldByRun", decide)


if __name__ == "__main__":
    unittest.main()
