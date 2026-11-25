"""Spearing Strike wiring (Forever's Arms talent strike, node 105949).

Spearing Strike (1310222) is an active Battle-Stance melee strike: 15 rage,
20 s cooldown, 75% weapon damage with a bonus against giants/dragonkin
(client Spell.db2 description, SpellShapeshift mask 65536, SpellPower 150).
The newer Arms preset takes it (78% of level-60 arms players). These pins
prove the row exists for the Arms kit, sits below Mortal Strike and above the
Heroic Strike swing dump, and only arms from Battle Stance. Turtle bots never
know the spell, so the row is skipped there (SpellIdValue answers 0).

Run: python3 -m unittest forever/tools/test_warrior_spearing_strike_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"


def class_body(text, start):
    i = text.index(start)
    return text[i:text.index("\n    };", i)]


class SpearingStrikeWiringTest(unittest.TestCase):
    def test_arms_row_sits_between_mortal_strike_and_heroic_strike(self):
        text = (WARRIOR / "ArmsWarriorStrategy.cpp").read_text()
        row = re.search(r'NextAction\("spearing strike", (ACTION_NORMAL \+ (\d+))\)', text)
        self.assertIsNotNone(row, 'no "spearing strike" row in the Arms kit')
        ms = re.search(r'NextAction\("mortal strike", (ACTION_NORMAL \+ (\d+))\)', text)
        hs = re.search(r'NextAction\("heroic strike", (ACTION_NORMAL \+ (\d+))\)', text)
        self.assertIsNotNone(ms)
        self.assertIsNotNone(hs)
        self.assertLess(int(row.group(2)), int(ms.group(2)), "Mortal Strike stays the pack's main strike")
        self.assertGreater(int(row.group(2)), int(hs.group(2)), "the 20 s cooldown goes before the swing dump")

    def test_trigger_only_arms_from_battle_stance(self):
        triggers = (WARRIOR / "WarriorTriggers.h").read_text()
        body = class_body(triggers, "class SpearingStrikeTrigger")
        self.assertIn('SpellCanBeCastedTrigger(ai, "spearing strike")', body)
        self.assertIn('HasAura("battle stance"', body)

    def test_trigger_and_action_are_registered(self):
        ctx = (WARRIOR / "WarriorAiObjectContext.cpp").read_text()
        self.assertIn('creators["spearing strike"] = [](PlayerbotAI* ai) { return new SpearingStrikeTrigger(ai); };', ctx)
        self.assertIn('creators["spearing strike"] = [](PlayerbotAI* ai) { return new CastSpearingStrikeAction(ai); };', ctx)
        self.assertIn('MELEE_ACTION(CastSpearingStrikeAction, "spearing strike");', (WARRIOR / "WarriorActions.h").read_text())


if __name__ == "__main__":
    unittest.main()
