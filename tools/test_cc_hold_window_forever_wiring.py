"""Wiring pins for the cc mark's fight-start hold on ForeverCore (TBF #208).

Live recheck 2 (bot logs of the mage): the ordered Polymorph DID go out, thirteen
casts on the marked unit, and none held. The party fought the mark: the tank's
Taunt and Thunder Clap, the priest's Shadow Word: Pain 0.9 s after the order's own
sheep. Two gaps on the port:

  * CombatTimeCompat (forever/src/compat/TbCompat_Unit.h) answered a constant -1,
    so CcMarkHeld's fight-start window (a ready cc bot's mark is left alone for
    CcMarkHoldSeconds) never opened on this core. The module now measures the
    engage time itself (TbCombatClockPure.h CombatClock).
  * FindNonCcTargetStrategy::OnlyCcMarksLeft read a fresh sheep that pulled its
    mark alone as "only cc marks left", and every bot's last-mark pick took it.
    A crowd-controlled mark inside its window is an opening hold now
    (CcOpeningHold), not the end of the fight.

Run:
  python3 tools/test_cc_hold_window_forever_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

COMPAT_UNIT = (ROOT / "forever/src/compat/TbCompat_Unit.h").read_text()
TARGET_VALUE = (ROOT / "ai/playerbot/strategy/values/TargetValue.cpp").read_text()
HOLD_POLICY = (ROOT / "ai/playerbot/strategy/values/CcMarkHoldPolicy.h").read_text()
CLOCK = ROOT / "forever/src/compat/TbCombatClockPure.h"


def function_body(text, signature):
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced body of " + signature)


def check_combat_time(compat, clock_exists):
    if re.search(r"CombatTimeCompat\(Creature const\*\s*/\*creature\*/\)\s*\{\s*return time_t\(-1\);\s*\}", compat):
        raise AssertionError("CombatTimeCompat still answers a constant -1: the cc mark hold window never opens on ForeverCore")
    if not clock_exists:
        raise AssertionError("forever/src/compat/TbCombatClockPure.h is missing")
    body = function_body(compat, "inline time_t CombatTimeCompat(Creature const* creature)")
    for needle in ("CombatClock", "IsInCombat()", "GuidKeyCompat(", "std::lock_guard<std::mutex>", ".Observe("):
        if needle not in body:
            raise AssertionError("CombatTimeCompat misses " + needle)


def check_only_marks_left(target_value, policy):
    if "inline bool CcOpeningHold(bool crowdControlled, long combatSeconds)" not in policy:
        raise AssertionError("CcMarkHoldPolicy.h misses CcOpeningHold")
    if "return liveEnemiesSeen > 0 && !unmarkedEnemySeen && !openingHoldSeen;" not in policy:
        raise AssertionError("CcOnlyMarksLeft does not let an opening hold veto 'only marks left'")
    body = function_body(target_value, "bool FindNonCcTargetStrategy::OnlyCcMarksLeft(Unit* except)")
    if "CcOpeningHold(" not in body or "CombatTimeCompat(" not in body or "IsCrowdControlled(ai, unit)" not in body:
        raise AssertionError("OnlyCcMarksLeft does not look for a crowd-controlled mark in its fight-start window")
    if "return CcOnlyMarksLeft(seen, false, openingHold);" not in body:
        raise AssertionError("OnlyCcMarksLeft does not pass the opening hold to CcOnlyMarksLeft")
    if "return CcOnlyMarksLeft(seen, true);" not in body:
        raise AssertionError("an unmarked enemy must still end the scan as 'not only marks'")
    protected = function_body(target_value, "bool FindNonCcTargetStrategy::IsCcProtected(Unit* unit)")
    if "CombatTimeCompat(creature)" not in protected:
        raise AssertionError("IsCcProtected no longer reads the creature's combat time")


class Wiring(unittest.TestCase):
    def test_combat_time_is_measured(self):
        check_combat_time(COMPAT_UNIT, CLOCK.exists())

    def test_old_constant_is_caught(self):
        old = "inline time_t CombatTimeCompat(Creature const* /*creature*/) { return time_t(-1); }"
        with self.assertRaises(AssertionError):
            check_combat_time(old, True)

    def test_only_marks_left_respects_opening_hold(self):
        check_only_marks_left(TARGET_VALUE, HOLD_POLICY)

    def test_old_only_marks_left_is_caught(self):
        old = TARGET_VALUE.replace("return CcOnlyMarksLeft(seen, false, openingHold);", "return CcOnlyMarksLeft(seen, false);")
        with self.assertRaises(AssertionError):
            check_only_marks_left(old, HOLD_POLICY)


if __name__ == "__main__":
    unittest.main()
