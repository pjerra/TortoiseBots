#!/usr/bin/env python3
"""Strategy rows for Turtle-only spells stay out of the Forever build, and the Forever rows are wired.

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
    'ai/playerbot/strategy/druid/TankFeralDruidStrategy.cpp': ['savage bite'],
}

# Forever-only rows (a Turtle bot simply does not know the spell) and the
# action contexts that must register them, so a row can fire.
FOREVER_ROWS = {
    'ai/playerbot/strategy/druid/TankFeralDruidStrategy.cpp': ['primal bite', 'lacerate'],
    'ai/playerbot/strategy/druid/RestorationDruidStrategy.cpp': ['wild growth'],
    'ai/playerbot/strategy/shaman/RestorationShamanStrategy.cpp': ['riptide on party'],
}
FOREVER_ACTIONS = {
    'ai/playerbot/strategy/druid/DruidAiObjectContext.cpp': ['primal bite', 'lacerate', 'wild growth'],
    'ai/playerbot/strategy/shaman/ShamanAiObjectContext.cpp': ['riptide', 'riptide on party'],
}


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


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
            text = read(rel)
            for action in actions:
                self.assertTrue(guarded(text, action), '%s: "%s" row is not behind the Forever guard' % (rel, action))

    def test_guard_detects_an_unguarded_row(self):
        text = 'triggers.push_back(new TriggerNode("x", NextAction::array(0, new NextAction("envenom", 1), NULL)));'
        self.assertFalse(guarded(text, 'envenom'))

    def test_forever_rows_exist(self):
        for rel, actions in FOREVER_ROWS.items():
            text = read(rel)
            for action in actions:
                self.assertIn('NextAction("%s"' % action, text, '%s: no "%s" row' % (rel, action))

    def test_forever_rows_have_actions(self):
        for rel, actions in FOREVER_ACTIONS.items():
            text = read(rel)
            for action in actions:
                self.assertIn('creators["%s"]' % action, text, '%s: no action creator for "%s"' % (rel, action))

    def test_fire_nova_casts_the_forever_name(self):
        text = read('ai/playerbot/strategy/shaman/ShamanActions.h')
        self.assertIn('CastSpellAction(ai, "fire nova")', text)


if __name__ == '__main__':
    unittest.main()
