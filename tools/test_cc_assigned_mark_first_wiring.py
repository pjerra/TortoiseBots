"""Wiring pins for the cc order's path from the order to a cast (TBF #208).

A `cc <mark> <guid>` order is only values plus a raid icon; the cast has to be
asked for. TB starts it at the assignment itself (`RunCcOrder`): an immediate
probe of the class action, with the mature action queued behind its own
`reach cc` prerequisite when the target is out of reach (commands/Action.cpp,
commands/BotCommandContext.cpp FindCcAction). The Forever port left the cast
to the fight-start trigger alone, and the trigger read only the engaged-
attacker tournament: on a pack that had not opened yet the ordered mark was no
candidate at all, so three accepted orders cast nothing and the mark died with
the pack (notes/milestone-runb-recheck-2026-11-23.md row 2).

Two pins:
  * CcTargetValue picks the bot's own `rti cc target` first, at any range,
    through the same assigned-mark rules (CcChooserTakesAssignedMark).
  * ApplyCcOrder starts the cast once the order holds (StartCcCast): the class
    action found through the crowd-control action graph, then a queued copy
    for the reach chain.

Run:
  python3 tools/test_cc_assigned_mark_first_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

CC_TARGET = (ROOT / "ai/playerbot/strategy/values/CcTargetValue.cpp").read_text()
HOLD_POLICY = (ROOT / "ai/playerbot/strategy/values/CcMarkHoldPolicy.h").read_text()
TBC = (ROOT / "forever/src/host/TbCc.cpp").read_text()
TBC_PURE = (ROOT / "forever/src/host/TbCcPure.h").read_text()


def check_chooser_first(cc_target, policy):
    if "CcChooserTakesAssignedMark" not in policy:
        raise AssertionError("CcMarkHoldPolicy.h misses CcChooserTakesAssignedMark")
    if "return assignedByOrder && !onlyCrowdControlMarksLeft;" not in policy:
        raise AssertionError("CcChooserTakesAssignedMark lost its only-marks-left rule")
    calculate = cc_target[cc_target.index("Unit* CcTargetValue::Calculate()"):]
    if 'AI_VALUE(std::string, "run cc family")' not in calculate:
        raise AssertionError("CcTargetValue::Calculate does not scope the first pick to an ORDERED mark")
    if 'AI_VALUE(Unit*, "rti cc target")' not in calculate:
        raise AssertionError("CcTargetValue::Calculate does not read the assigned mark")
    if "CcChooserTakesAssignedMark(" not in calculate:
        raise AssertionError("CcTargetValue::Calculate does not apply CcChooserTakesAssignedMark")
    if '!family.empty() && family != "none" &&' not in calculate:
        raise AssertionError("a plain Moon mark without an ordered family must not take the fast path")
    if "CcOrderFamilyAllows(family, qualifier)" not in calculate:
        raise AssertionError("only the row the order named may take the fast path")
    if calculate.index('AI_VALUE(std::string, "run cc family")') > calculate.index("CcChooserTakesAssignedMark("):
        raise AssertionError("the ordered-mark test must run before the pick")
    if "strategy.CheckAttacker(assigned" not in calculate:
        raise AssertionError("CcTargetValue::Calculate does not run its own mark through the chooser rules")
    if calculate.index("CcChooserTakesAssignedMark(") > calculate.index("return FindTarget(&strategy);"):
        raise AssertionError("the assigned mark must be picked before the tournament")
    # The family's own cached pick must be dropped with the mark targets: a stale `cc target::<spell>` would hide the new mark.
    if 'HasMyAura(CcAuraName(qualifier), add)' not in calculate:
        raise AssertionError("the own-aura pre-pass must stay in front of the assigned-mark pick")


def check_order_starts_cast(tbc, pure):
    if "CcFamilySpellName" not in pure:
        raise AssertionError("TbCcPure.h misses CcFamilySpellName")
    if "bool CcFamilySpellName" in pure or "std::string CcFamilySpellName" in pure:
        raise AssertionError("CcFamilySpellName must stay a char const* table lookup")
    for token, spell in (('"polymorph"', '"polymorph"'), ('"shackleundead"', '"shackle undead"'),
                         ('"freezingtrap"', '"freezing trap"'), ('"scarebeast"', '"scare beast"')):
        if "if (token == %s) return %s;" % (token, spell) not in pure:
            raise AssertionError("CcFamilySpellName misses " + token)
    if "void StartCcCast(" not in tbc:
        raise AssertionError("TbCc.cpp misses StartCcCast")
    if "StartCcCast(leader, bot, ai, target, wantedFamily);" not in tbc:
        raise AssertionError("ApplyCcOrder does not start the cast after its read-back")
    apply_fn = tbc[tbc.index("bool ApplyCcOrder("):]
    apply_fn = apply_fn[:apply_fn.index("\n}\n")]
    if apply_fn.index("StartCcCast(leader, bot, ai, target, wantedFamily);") < apply_fn.index('"failed", "the mark, the bot'):
        raise AssertionError("the cast must start only after the order reads back")
    if "IsCrowdControlAction()" not in tbc or "GetCrowdControlSpellName()" not in tbc:
        raise AssertionError("StartCcCast must find the class action through the crowd-control graph")
    if "DoSpecificAction(action, event, true)" not in tbc:
        raise AssertionError("StartCcCast must probe the class action directly")
    if "QueueAction(action, ACTION_PASSTROUGH, event)" not in tbc:
        raise AssertionError("StartCcCast must queue the action for the reach chain")
    start_fn = tbc[tbc.index("void StartCcCast("):]
    start_fn = start_fn[:start_fn.index("\n}\n")]
    if start_fn.index("DoSpecificAction(") > start_fn.index("QueueAction("):
        raise AssertionError("the direct probe must run before the queued fallback")
    if "ResetTargets(ai);" not in start_fn or 'GetValue<Unit*>("cc target", spell)' not in start_fn:
        raise AssertionError("StartCcCast must drop the stale mark picks")
    if "SetSelectionGuid" not in start_fn or "SetFacingTo" not in start_fn:
        raise AssertionError("StartCcCast must select and face the target")


class CcOrderToCastTest(unittest.TestCase):
    def test_chooser_takes_the_assigned_mark_first(self):
        check_chooser_first(CC_TARGET, HOLD_POLICY)

    def test_order_starts_its_cast(self):
        check_order_starts_cast(TBC, TBC_PURE)

    def test_mutation_fails(self):
        # The pre-fix chooser: no assigned-mark block in Calculate.
        old = CC_TARGET.replace(
            'if (Unit* assigned = AI_VALUE(Unit*, "rti cc target"))',
            'if (false)')
        with self.assertRaises(AssertionError):
            check_chooser_first(old, HOLD_POLICY)
        # The pre-fix order: values and icon only, no probe.
        no_probe = TBC.replace("    StartCcCast(leader, bot, ai, target, wantedFamily);\n", "")
        with self.assertRaises(AssertionError):
            check_order_starts_cast(no_probe, TBC_PURE)
        # A value-first policy would read as "no rule".
        with self.assertRaises(AssertionError):
            check_chooser_first(CC_TARGET, HOLD_POLICY.replace("!onlyCrowdControlMarksLeft", "true"))
        # A plain Moon mark (no ordered family) must not take the fast path.
        with self.assertRaises(AssertionError):
            check_chooser_first(CC_TARGET.replace('!family.empty() && family != "none" &&', ""), HOLD_POLICY)
        with self.assertRaises(AssertionError):
            check_chooser_first(CC_TARGET.replace("CcOrderFamilyAllows(family, qualifier)", "true"), HOLD_POLICY)


if __name__ == "__main__":
    unittest.main()
