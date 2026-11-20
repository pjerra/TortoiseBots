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
  6 a clear is atomic (ClearCcState writes, reads back and restores) and the run's end resets every bot a run `cc` order marked, not only Run::ccHolds
  7 a teardown clear that could not be taken (bot away, no engine, a refused read-back) is kept pending and retried; the run's event ring and the server
    log say so
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
        self.assertIn("ClearRunCc(rec, nowMs);", finish)
        self.assertLess(finish.index("ClearRunCc(rec, nowMs);"), finish.index("ForgetRun(rec.leader);"))   # the handles still resolve

    def test_bare_clear_takes_the_icon(self):
        cc = text("forever/src/host/TbCc.cpp")
        body = cc[cc.index("bool ClearCcOrder("):]
        body = body[:body.index("\nnamespace")]
        self.assertIn('ownCc->Get()', body)
        self.assertIn("target.own", body)
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
        # The registry file is generated: the action count must be the number of action creators its own Action contexts
        # register (54 on the slice-8 tree; the integration branch adds the slice-10 and slice-11 keys, so the number itself
        # is not pinned here).
        registered = 0
        for m in re.finditer(r"class Slice\w+Context : public NamedObjectContext<(Action)>\n    \{\n(.*?)\n    \};", ctx, re.S):
            registered += m.group(2).count('creators["')
        self.assertEqual(registered, int(re.search(r"kActions = (\d+)", ctx).group(1)))

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

    # Review fix round: a refused clear changes nothing, and the run's end resets every bot a run `cc` order marked, not only Run::ccHolds.
    def test_clear_is_atomic(self):
        cc = text("forever/src/host/TbCc.cpp")
        body = cc[cc.index("bool ClearCcOrder("):]
        body = body[:body.index("\nvoid CcOrderValues(")]
        self.assertIn("ClearCcState(values, icons, plan)", body)
        self.assertIn("CcClearTargetOf(", body)
        self.assertIn("CcClearFailure::Icon", body)
        self.assertIn("CcClearFailure::Values", body)
        self.assertNotIn("ownCc->Set(", body)        # both values are written and undone by ClearCcState
        self.assertNotIn("group->SetTargetIcon(", body)
        # the icon comes down only after the target is read, and the no-party refusal is for a named unit alone
        self.assertLess(body.index("CcClearTargetOf("), body.index("ClearCcState(values, icons, plan)"))
        self.assertIn("if (target.needsGroup && !group)", body)

    def test_run_clears_every_bot_it_marked(self):
        run = text("forever/src/host/run/TbRun.cpp")
        clear = run[run.index("void ClearRunCc("):]
        clear = clear[:clear.index("\n}\n")]
        self.assertIn("for (auto const& owned : rec.ccOwned)", clear)
        self.assertNotIn("rec.run.ccHolds", clear)
        self.assertIn("h8::CcOrderValues(", clear)
        self.assertIn("h8::CcMarkOwned(", clear)
        self.assertIn("h8::ClearCcOrder(", clear)
        self.assertIn("ObjectAccessor::FindPlayerByName(", clear)    # a bot that left the group is still found
        orders = text("forever/src/host/run/TbRunOrders.cpp")
        self.assertIn("h8::CcOwnedRecord(rec.ccOwned", orders)
        self.assertIn("h8::CcOwnedForget(rec.ccOwned", orders)

    # Review round 2: an unavailable bot and a refused teardown clear are kept pending, retried, and visible; a newer assignment takes the bot over.
    def test_cc_teardown_is_retried(self):
        run = text("forever/src/host/run/TbRun.cpp")
        clear = run[run.index("void ClearRunCc("):]
        clear = clear[:clear.index("\n}\n")]
        self.assertNotIn("if (!bot || !bot->IsInWorld())", clear)   # was a silent skip
        self.assertIn("bool const inWorld = bot && bot->IsInWorld();", clear)
        self.assertIn("cleared = h8::ClearCcOrder(", clear)         # the answer is read, not discarded
        self.assertIn("h8::CcTeardownSettles(inWorld, ownedMark, cleared)", clear)
        self.assertIn("RetainPendingCc(rec, owned.first, owned.second);", clear)
        self.assertIn('Note(rec, nowMs, "cc-clear"', clear)         # the run's event ring names the failure
        retry = run[run.index("void RetryPendingCc()"):]
        retry = retry[:retry.index("\n}\n")]
        self.assertIn("h8::CcTeardownSettles(inWorld, ownedMark, cleared)", retry)
        self.assertIn("h8::ClearCcOrder(nullptr, bot", retry)
        self.assertIn("sLog.outString(", retry)                      # the wait is observable in the server log
        update = run[run.index("void Update(uint32 diffMs)"):]
        self.assertLess(update.index("RetryPendingCc();"), update.index("if (Table().empty())"))
        orders = text("forever/src/host/run/TbRunOrders.cpp")
        self.assertEqual(orders.count("DropPendingCc(bot->GetName());"), 2)   # accepted `cc` and `cc clear`


if __name__ == "__main__":
    unittest.main()
