"""Feral bear tank wiring (T2: B1, B2, W3 bear half).

Run: python3 -m unittest tools/test_bear_tank_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DRUID = ROOT / "ai/playerbot/strategy/druid"


def rows(path):
    """(trigger, action, relevance expression) for every single-action row."""
    text = (DRUID / path).read_text()
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(text)]


class BearTankWiringTest(unittest.TestCase):
    def test_maul_row_replaces_dead_mangle_row(self):
        combat_rows = rows("TankFeralDruidStrategy.cpp")
        self.assertIn(("maul rage available", "maul", "ACTION_NORMAL + 1"),
                      [(t, a, r) for (t, a, r) in combat_rows if a == "maul"])
        self.assertNotIn(("medium rage available", "mangle (bear)", "ACTION_NORMAL + 1"),
                         [(t, a, r) for (t, a, r) in combat_rows])
        combat = (DRUID / "TankFeralDruidStrategy.cpp").read_text()
        self.assertNotIn('"mangle (bear)"', combat)

    def test_maul_action_uses_policy(self):
        actions = (DRUID / "DruidActions.h").read_text()
        maul_start = actions.index("class CastMaulAction")
        maul_body = actions[maul_start:maul_start + 600]
        self.assertIn("ShouldQueueMaul", maul_body)
        self.assertNotIn(">= 45", maul_body)

    def test_maul_rage_trigger_registered(self):
        ctx = (DRUID / "DruidAiObjectContext.cpp").read_text()
        self.assertIn('creators["maul rage available"]', ctx)

    def test_savage_bite_row_and_creators(self):
        combat_rows = rows("TankFeralDruidStrategy.cpp")
        self.assertIn(("savage bite", "savage bite", "ACTION_HIGH + 1"),
                      [(t, a, r) for (t, a, r) in combat_rows])
        ctx = (DRUID / "DruidAiObjectContext.cpp").read_text()
        self.assertIn('creators["savage bite"]', ctx)
        self.assertEqual(ctx.count('creators["savage bite"]'), 2)

    def test_loose_adds_row_replaces_pack_roar(self):
        text = (DRUID / "TankFeralDruidStrategy.cpp").read_text()
        aoe = text[text.index("TankFeralDruidAoeStrategy::InitCombatTriggers"):]
        aoe = aoe[:aoe.index("InitNonCombatTriggers")]
        pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
        roar_rows = [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(aoe) if m.group(2) == "challenging roar"]
        self.assertIn(("loose adds near", "challenging roar", "ACTION_INTERRUPT + 2"), roar_rows)
        self.assertNotIn(("melee medium aoe", "challenging roar", "ACTION_HIGH + 3"), roar_rows)


if __name__ == "__main__":
    unittest.main()
