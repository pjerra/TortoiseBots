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

# Forever actives the class line teaches (no talent needed): trigger -> action.
FOREVER_ROWS = {
    'ai/playerbot/strategy/rogue/AssassinationRogueStrategy.cpp': [('mutilate', 'mutilate'), ('death mark', 'death mark')],
    'ai/playerbot/strategy/rogue/CombatRogueStrategy.cpp': [('death mark', 'death mark')],
    'ai/playerbot/strategy/rogue/SubtletyRogueStrategy.cpp': [('death mark', 'death mark')],
    'ai/playerbot/strategy/priest/DisciplinePriestStrategy.cpp': [
        ('party member low health', 'penance on party'),
        ('prayer of mending', 'prayer of mending on tank'),
        ('tank needs renew', 'renew on tank'),
    ],
    'ai/playerbot/strategy/priest/HolyPriestStrategy.cpp': [
        ('prayer of mending', 'prayer of mending on tank'),
        ('tank needs renew', 'renew on tank'),
    ],
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


def has_row(text, trigger, action):
    """A TriggerNode for trigger whose NextAction array contains the action."""
    pattern = r'"%s"\s*,\s*NextAction::array\([^;]*?"%s"' % (re.escape(trigger), re.escape(action))
    return re.search(pattern, text, re.S) is not None


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

    def test_forever_actives_have_rows(self):
        for rel, rows in FOREVER_ROWS.items():
            text = open(os.path.join(ROOT, rel)).read()
            for trigger, action in rows:
                self.assertTrue(has_row(text, trigger, action),
                    '%s: no "%s" -> "%s" row' % (rel, trigger, action))

    def test_row_matcher_requires_the_pair(self):
        text = 'new TriggerNode("tank needs renew", NextAction::array(0, new NextAction("renew on party", 1), NULL))'
        self.assertFalse(has_row(text, 'tank needs renew', 'renew on tank'))


if __name__ == '__main__':
    unittest.main()
