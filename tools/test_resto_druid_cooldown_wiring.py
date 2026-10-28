"""Resto druid cooldown rows (combat spec T4: D1, D2).

D1: Tranquility is a 30-min channel for group damage, not for two scratches:
no ("medium aoe heal", "tranquility") row in the base combat triggers; the
group-sized row in the AoE strategy ("medium group heal setting") keeps it.

D2: Nature's Swiftness is held for the critical row, not popped on the first
quiet tick: no ("nature's swiftness", ACTION_HIGH) row in the boost strategy;
the critical pop row and the instant-Healing-Touch spend row stay.

Run: python3 -m unittest tools/test_resto_druid_cooldown_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy/druid/RestorationDruidStrategy.cpp"

ROW = re.compile(
    r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)'
)


def method_body(cls, method):
    """Body of `void <cls>::<method>(...)` (brace-matched)."""
    text = STRATEGY.read_text()
    start = text.index("void %s::%s" % (cls, method))
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced braces in %s::%s" % (cls, method))


def rows(body):
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in ROW.finditer(body)]


class RestoDruidCooldownWiringTest(unittest.TestCase):
    def test_tranquility_not_on_two_scratches(self):
        base = rows(method_body("RestorationDruidStrategy", "InitCombatTriggers"))
        self.assertNotIn(
            ("medium aoe heal", "tranquility", "ACTION_MEDIUM_HEAL + 2"), base)
        for trigger, action, _ in base:
            self.assertFalse(
                trigger == "medium aoe heal" and action == "tranquility",
                "tranquility still queued on the 2-member aoe-heal trigger")
        aoe = rows(method_body("RestorationDruidAoeStrategy", "InitCombatTriggers"))
        self.assertIn(
            ("medium group heal setting", "tranquility", "ACTION_MEDIUM_HEAL + 2"), aoe)

    def test_natures_swiftness_held_for_critical(self):
        boost = rows(method_body("RestorationDruidBoostStrategy", "InitCombatTriggers"))
        for trigger, action, relevance in boost:
            self.assertFalse(
                action == "nature's swiftness" and relevance == "ACTION_HIGH",
                "nature's swiftness still popped on the bare boost trigger")
        base = rows(method_body("RestorationDruidStrategy", "InitCombatTriggers"))
        self.assertIn(
            ("party member critical health", "nature's swiftness",
             "ACTION_CRITICAL_HEAL + 4"), base)
        self.assertIn(
            ("nature's swiftness active", "healing touch on party",
             "ACTION_CRITICAL_HEAL + 3"), base)


if __name__ == "__main__":
    unittest.main()
