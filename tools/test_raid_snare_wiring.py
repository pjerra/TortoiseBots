"""Raid snare gate wiring (warrior-hunter-raid fix 2).

In a raid group Piercing Howl fires at ACTION_HIGH on every mob the snare
value returns; raid trash chasing the tank qualified, so the warrior spent
GCDs snaring mobs that are already controlled. In a raid only a mob
attacking a non-tank is a snare target.

Run: python3 -m unittest tools/test_raid_snare_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALUE = ROOT / "ai/playerbot/strategy/values/SnareTargetValue.cpp"
POLICY = ROOT / "ai/playerbot/policy/combat/RaidSnarePolicy.h"


class RaidSnareWiringTest(unittest.TestCase):
    def test_policy_rule_exists(self):
        self.assertIn("SnareAllowedInRaid", POLICY.read_text())

    def test_chase_branch_uses_policy(self):
        code = (VALUE).read_text()
        self.assertIn("SnareAllowedInRaid", code)
        self.assertIn("isRaidGroup", code)

    def test_chase_branch_still_skips_tank_victims(self):
        code = VALUE.read_text()
        self.assertIn("victimOnTank", code)
        self.assertIn("!victimOnTank", code)


if __name__ == "__main__":
    unittest.main()
