#!/usr/bin/env python3
"""Strategy rows for Turtle-only spells stay out of the Forever build (no Forever class can learn them on 70959),
and the Forever rows stay in it (a row hidden behind the Turtle-only guard is dead on Forever).

    python3 forever/tools/test_forever_spell_rows.py
"""
import os
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GUARD = '#if !defined(TORTOISEBOTS_CORE_FOREVER) || !TORTOISEBOTS_CORE_FOREVER'
ROWS = {
    'ai/playerbot/strategy/warrior/ArmsWarriorStrategy.cpp': ['master strike'],
    'ai/playerbot/strategy/warrior/FuryWarriorStrategy.cpp': ['master strike'],
    'ai/playerbot/strategy/mage/ArcaneMageStrategy.cpp': ['arcane rupture'],
    'ai/playerbot/strategy/mage/FrostMageStrategy.cpp': ['icicles'],
    'ai/playerbot/strategy/rogue/AssassinationRogueStrategy.cpp': ['envenom'],
    'ai/playerbot/strategy/rogue/SubtletyRogueStrategy.cpp': ['shadow of death', 'mark for death', 'smoke bomb'],
}
FOREVER_ROWS = {
    'ai/playerbot/strategy/mage/ArcaneMageStrategy.cpp': ['arcane blast', 'arcane wrath', 'missile barrage'],
    'ai/playerbot/strategy/mage/FrostMageStrategy.cpp': ['ice lance'],
    'ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp': ['incinerate'],
    'ai/playerbot/strategy/warlock/AfflictionWarlockStrategy.cpp': ['wrack'],
}


def guarded(text, action):
    """True when every NextAction("<action>") sits between the Forever guard and its #endif."""
    depth_forever = False
    found = False
    for line in text.splitlines():
        s = line.strip()
        if s == GUARD:
            depth_forever = True
        elif s == '#endif' and depth_forever:
            depth_forever = False
        elif 'NextAction("%s"' % action in line:
            found = True
            if not depth_forever:
                return False
    return found


class ForeverSpellRowsTest(unittest.TestCase):
    def test_turtle_only_rows_are_guarded(self):
        for rel, actions in ROWS.items():
            text = open(os.path.join(ROOT, rel)).read()
            for action in actions:
                self.assertTrue(guarded(text, action), '%s: "%s" row is not behind the Forever guard' % (rel, action))

    def test_forever_rows_are_not_guarded(self):
        for rel, actions in FOREVER_ROWS.items():
            text = open(os.path.join(ROOT, rel)).read()
            for action in actions:
                self.assertFalse(guarded(text, action), '%s: "%s" row is hidden from the Forever build' % (rel, action))

    def test_guard_detects_an_unguarded_row(self):
        text = 'triggers.push_back(new TriggerNode("x", NextAction::array(0, new NextAction("envenom", 1), NULL)));'
        self.assertFalse(guarded(text, 'envenom'))


if __name__ == '__main__':
    unittest.main()
