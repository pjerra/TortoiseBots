"""Priest heal cooldown fixes (combat-t3): shield gate on rage tanks (P1),
Prayer of Healing on the priest's own subgroup only (P2), Ascendance held
for real damage (P3).

Run: python3 -m unittest tools/test_priest_heal_cooldown_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIEST = ROOT / "ai/playerbot/strategy/priest"


def rows(path):
    """(trigger, action, relevance expression) for every single-action row."""
    text = (PRIEST / path).read_text()
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(text)]


def multi_rows(path):
    """(trigger, [actions]) for every row, including multi-action arrays."""
    text = (PRIEST / path).read_text()
    out = []
    for m in re.finditer(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, (.*?)NULL\)\)\)', text, re.S):
        actions = re.findall(r'new NextAction\("([^"]+)"', m.group(2))
        out.append((m.group(1), actions))
    return out


class ShieldGateWiringTest(unittest.TestCase):
    def test_shield_action_gated(self):
        # P1: the party shield action consults the target policy and the
        # rage power type; no row changes (the critical rows keep it).
        actions = (PRIEST / "PriestActions.h").read_text()
        body = actions[actions.index("class CastPowerWordShieldOnPartyAction"):]
        body = body[:body.index("\n    };")]
        self.assertIn("ShouldShieldTarget", body)
        self.assertIn("POWER_RAGE", body)


class PrayerOfHealingWiringTest(unittest.TestCase):
    def test_poh_party_trigger_rows(self):
        # P2: both AoE strategies queue PoH on the subgroup trigger above
        # the medium/low band singles and below every critical row.
        for path in ("HolyPriestStrategy.cpp", "DisciplinePriestStrategy.cpp"):
            self.assertIn(
                ("prayer of healing party", "prayer of healing", "ACTION_MEDIUM_HEAL + 5"),
                rows(path), path)
            names = [r[0] for r in rows(path) if r[1] == "prayer of healing"]
            self.assertNotIn("medium aoe heal", names, path)

    def test_group_setting_rows_drop_poh(self):
        # P2: the raid-wide group-setting rows no longer contain PoH (the
        # shield stays; it is gated by P1).
        for path in ("HolyPriestStrategy.cpp", "DisciplinePriestStrategy.cpp"):
            setting = [a for t, a in multi_rows(path) if t == "medium group heal setting"]
            self.assertTrue(setting, path)
            for actions in setting:
                self.assertNotIn("prayer of healing", actions, path)
                self.assertIn("power word: shield on party", actions, path)

    def test_trigger_counts_own_subgroup(self):
        # P2: the trigger counts the priest's own subgroup, in 30 yd 2D at
        # or below medium health, through the pure policy.
        trig = (PRIEST / "PriestTriggers.cpp").read_text()
        fn = trig[trig.index("PrayerOfHealingPartyTrigger::IsActive"):]
        fn = fn[:fn.index("\n}\n")]
        self.assertIn("SameSubGroup", fn)
        self.assertIn("PrayerOfHealingWorth", fn)
        ctx = (PRIEST / "PriestAiObjectContext.cpp").read_text()
        self.assertIn('creators["prayer of healing party"]', ctx)


class AscendanceWiringTest(unittest.TestCase):
    def test_ascendance_gated_on_damage(self):
        # P3: the capstone arms only when someone actually needs healing.
        trig = (PRIEST / "PriestTriggers.cpp").read_text()
        fn = trig[trig.index("bool AscendanceTrigger::IsActive()"):]
        fn = fn[:fn.index("\n}\n")]
        self.assertIn("HealBoostReady", fn)
        self.assertIn('"party member critical health"', fn)
        self.assertIn('"party member low health"', fn)
        self.assertIn('"critical health"', fn)
        self.assertIn('"low health"', fn)
        self.assertIn('"medium group heal setting"', fn)


if __name__ == "__main__":
    unittest.main()
