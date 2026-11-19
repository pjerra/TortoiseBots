#!/usr/bin/env python3
"""Wiring pins for package G8 (forever/docs/SLICE8-G-NOTES.md). The core calls cannot run without a server, so these pin the calls and rules in the source; each
assertion fails on the code before G8.

  1 run::Issue carries out `cc` and `cc clear` through h8::ApplyCcOrder / ClearCcOrder; the run's tick tells the policy the answer (CcOrderTaken) and clears the
    run's crowd control at its end; a bare `.tb cc <bot> clear` takes the icon of the bot's own mark
  1b the run facts know a mage's Polymorph, the aura on units, their time left and immunity
  2 `reach cc` is a registered action
  3 the party-buff search and the group-buff count skip rage/energy users for a mana buff
  4 the rogue's combat kit keeps `cc`
  5 the regroup gather's pause is extended by the glue (bounded), the policy header is untouched
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FOREVER = ROOT / "forever"


def text(rel):
    return (ROOT / rel).read_text()


class G8Wiring(unittest.TestCase):
    def test_issue_carries_cc_orders(self):
        orders = text("forever/src/host/run/TbRunOrders.cpp")
        self.assertIn("h8::ApplyCcOrder(", orders)
        self.assertIn("h8::ClearCcOrder(", orders)
        self.assertIn("case od::Verb::Cc:", orders)
        self.assertIn("case od::Verb::CcClear:", orders)
        pure = text("forever/src/host/run/TbRunOrdersPure.h")
        self.assertNotRegex(pure, r'for \(char const\* known : \{[^}]*"cc"')

    def test_run_tells_the_policy_and_clears_at_the_end(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("DungeonRun::CcOrderTaken(run, order, answer.accepted)", run)
        finish = run[run.index("uint32 Finish("):]
        finish = finish[:finish.index("\n}\n")]
        self.assertIn("ClearRunCc(rec);", finish)
        self.assertLess(finish.index("ClearRunCc(rec);"), finish.index("ForgetRun(rec.leader);"))   # the handles still resolve

    def test_bare_clear_takes_the_icon(self):
        cc = text("forever/src/host/TbCc.cpp")
        body = cc[cc.index("bool ClearCcOrder("):]
        body = body[:body.index("\nnamespace")]
        self.assertIn('ownCc->Get()', body)
        self.assertIn("ObjectGuid::Empty", body)
        self.assertIn("iconRemoved", body)

    def test_facts_know_the_crowd_control(self):
        facts = text("forever/src/host/run/TbRunFacts.cpp")
        self.assertIn("m.polymorph = cls == CLASS_MAGE && KnowsPolymorph(bot);", facts)
        self.assertIn("m.canSap = false;", facts)
        self.assertGreaterEqual(facts.count("RunCcHeld("), 4)       # creature, engaged, attacker + the definition
        pure = text("forever/src/host/run/TbRunFactsPure.h")
        self.assertIn("u.polymorphed = c.polymorphed;", pure)
        self.assertIn("u.polymorphed = e.polymorphed;", pure)

    def test_reach_cc_registered(self):
        ctx = text("forever/src/slice/SliceAiObjectContext.cpp")
        self.assertIn('creators["reach cc"]', ctx)
        self.assertIn("kActions = 54", ctx)

    def test_mana_filter(self):
        value = text("ai/playerbot/strategy/values/PartyMemberWithoutAuraValue.cpp")
        self.assertIn("ai::BuffTargetFits(", value)
        acts = text("ai/playerbot/strategy/actions/GenericSpellActions.cpp")
        self.assertIn("ai::BuffTargetFits(GetSpellName()", acts)

    def test_rogue_keeps_cc(self):
        fac = text("forever/src/slice/SliceAiFactory.cpp")
        self.assertRegex(fac, r'addStrategies\("combat", "dps assist", "close", "behind", "cc",')

    def test_gather_extension(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("gather::Decide(rec.gatherExt", run)
        self.assertIn("DungeonRun::Resume(run)", run)
        # the order of the calls: the extension sits right after the policy's tick, before the executor and before Observe sees the pause
        self.assertLess(run.index("DungeonRun::Tick(run, facts, dt)"), run.index("gather::Decide(rec.gatherExt"))
        self.assertLess(run.index("gather::Decide(rec.gatherExt"), run.index("Issue(rec, order, nowMs"))


if __name__ == "__main__":
    unittest.main()
