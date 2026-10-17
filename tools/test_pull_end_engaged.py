"""A pull ends as soon as the pulled mob is on the puller in melee.

Corner pulls: the tank walks to a mob it cannot see, the mob meets it on the
way, and the pull action never completes. While the pull keeps its target,
PullMultiplier mutes every action below ACTION_INTERRUPT, so the tank stood
with its back to the mob (no swing, no ability, no turn) until the 15 s pull
cap (Uldaman hire run, 2026-10-14: 8 s facing away, trace-score facing FAIL).
Pins the "pull end" clause that ends such a pull, and its order: after the
pullback return (a landed pullback still walks back) and the dead-target and
time-cap exits. A pullback meet completes the pull action and takes the
return leg home instead of ending at the meeting point (#973).

Run: python3 -m unittest tools/test_pull_end_engaged.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/PullTriggers.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PullEndEngagedTest(unittest.TestCase):
    def setUp(self):
        self.fn = body(TRIGGERS, "bool PullEndTrigger::IsActive()", "\n}\n")

    def test_engaged_in_melee_ends_pull(self):
        self.assertIn("!strategy->HasPullActionCompleted() && target->GetVictim() == bot &&", self.fn)
        self.assertIn("bot->CanReachWithMeleeAutoAttack(target)", self.fn)

    def test_pullback_meet_takes_return_leg(self):
        self.assertIn("if (pullback)", self.fn)
        self.assertIn("OnPullActionCompleted()", self.fn)

    def test_clause_order(self):
        pullback = self.fn.index("if (pullback && strategy->HasPullActionCompleted())")
        cap = self.fn.index("secondsSincePullStarted >= strategy->GetMaxPullTime()")
        engaged = self.fn.index("target->GetVictim() == bot")
        final = self.fn.index("return strategy->HasPullActionCompleted();")
        self.assertLess(pullback, engaged)
        self.assertLess(cap, engaged)
        self.assertLess(engaged, final)


ACTIONS = (ROOT / "ai/playerbot/strategy/actions/PullActions.cpp").read_text()
TRIGGER_SRC = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()


class ChatPullbackOrderTest(unittest.TestCase):
    def setUp(self):
        self.fn = body(ACTIONS, "bool PullbackMyTargetAction::Execute(Event& event)", "\n}\n")

    def test_mode_before_request(self):
        # F1: RequestPull snapshots intent and DecideOpening reads the command
        # state, so BeginCommand must run before the request action.
        begin = self.fn.index("BeginCommand(true")
        request = self.fn.index("PullRequestAction::Execute(event)")
        self.assertLess(begin, request)

    def test_real_prior_state_with_rollback(self):
        # F2: the tank's real prior "pull back" state is recorded and put back
        # when the request refuses (no hardcoded hadPullBack=true leak).
        self.assertIn('HasStrategy("pull back"', self.fn)
        self.assertIn("RestoreCommand(commandBefore)", self.fn)

    def test_wand_explicit_order_escape(self):
        # F3: a direct attack order still lets the healer wand, even mid-hold.
        self.assertIn("gate.explicitOrder", TRIGGER_SRC)
        self.assertIn("gate.pullHold = ai->HasPullWindow() && !gate.explicitOrder;", TRIGGER_SRC)


if __name__ == "__main__":
    unittest.main()
