#!/usr/bin/env python3
"""Pins `ai_playerbot_spec_enchant` (the level-60 common enchant per spec and slot) to its source.

The rows are derived from forever/data/aotc/enchants-60.csv (aotc.gg armory read 2026-11-24, `best_geared`
population): when at least 60% of those characters have no enchant in a slot, the slot is left empty (`spellid` 0);
otherwise the most used non-empty enchant is the spec's choice. The test recomputes that rule from the CSV and
checks the committed rows, the spec ids they name, and that every non-zero choice is an
`ai_playerbot_enchant_candidates` row for the same slot (the picker only returns pool rows).

    python3 -m unittest forever/tools/test_spec_enchant_data.py      # from the repo root
"""
import csv
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20261125180000_world.sql')
POOL_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20260925150000_world.sql')
WEIGHT_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20260916090001_world.sql')
TANK_WEIGHT_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20261020173406_world.sql')
AOTC_CSV = os.path.join(ROOT, 'forever', 'data', 'aotc', 'enchants-60.csv')

# Core EquipmentSlots values the armory page covers, by armory slot name.
SLOTS = {
    'Head': 0, 'Neck': 1, 'Back': 14, 'Chest': 4, 'Wrist': 8, 'Hands': 9,
    'Legs': 6, 'Feet': 7, 'Main Hand': 15, 'Off Hand': 16, 'Ranged': 17,
}

# Our weight-scale spec ids (ai_playerbot_weightscales.id) that carry an armory spec. A `feral` druid is one armory
# spec but two of ours (tank and dps); `shamantank` (id 16) has no armory page and therefore no row.
SPECS = {
    ('warrior', 'arms'): [1], ('warrior', 'fury'): [2], ('warrior', 'protection'): [3],
    ('paladin', 'holy'): [4], ('paladin', 'protection'): [5], ('paladin', 'retribution'): [6],
    ('hunter', 'beast-mastery'): [7], ('hunter', 'marksmanship'): [8], ('hunter', 'survival'): [9],
    ('rogue', 'assassination'): [10], ('rogue', 'combat'): [11], ('rogue', 'subtlety'): [12],
    ('priest', 'discipline'): [13], ('priest', 'holy'): [14], ('priest', 'shadow'): [15],
    ('shaman', 'elemental'): [20], ('shaman', 'enhancement'): [21], ('shaman', 'restoration'): [22],
    ('mage', 'arcane'): [23], ('mage', 'fire'): [24], ('mage', 'frost'): [25],
    ('warlock', 'affliction'): [26], ('warlock', 'demonology'): [27], ('warlock', 'destruction'): [28],
    ('druid', 'balance'): [29], ('druid', 'feral'): [30, 32], ('druid', 'restoration'): [31],
}
CLASS_IDS = {
    'warrior': 1, 'paladin': 2, 'hunter': 3, 'rogue': 4, 'priest': 5,
    'shaman': 7, 'mage': 8, 'warlock': 9, 'druid': 11,
}
EMPTY_SHARE = 60.0


def read(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def statement(text, table):
    """The INSERT body for `table`, from `INSERT INTO ... VALUES` up to the terminating semicolon."""
    match = re.search(r'INSERT (?:IGNORE )?INTO `%s`[^;]*?VALUES(.*?);' % re.escape(table), text, re.S)
    return match.group(1) if match else ''


def load_weight_scales():
    scales = {}
    for path in (WEIGHT_SQL, TANK_WEIGHT_SQL):
        for spec_id, name, clazz in re.findall(r"\((\d+), '(\w+)', (\d+)\)", statement(read(path), 'ai_playerbot_weightscales')):
            scales[int(spec_id)] = (name, int(clazz))
    return scales


def load_spec_rows():
    rows = {}
    for spec_id, slot_id, spell_id, item_id, none_pct, name in re.findall(
            r"\((\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.]+),\s*'([^']*)'\)",
            statement(read(SPEC_SQL), 'ai_playerbot_spec_enchant')):
        key = (int(spec_id), int(slot_id))
        assert key not in rows, 'duplicate spec/slot %s' % (key,)
        rows[key] = dict(spell_id=int(spell_id), item_id=int(item_id), none_pct=float(none_pct), name=name)
    return rows


def load_pool():
    """(spell id, slot id) pairs the candidate pool holds, plus the rows this migration adds."""
    pool = set()
    for text in (read(POOL_SQL), read(SPEC_SQL)):
        for spell_id, slot_id in re.findall(r"\((\d+),\s*(\d+),", statement(text, 'ai_playerbot_enchant_candidates')):
            pool.add((int(spell_id), int(slot_id)))
    return pool


def load_added_pool_rows():
    rows = []
    for spell_id, slot_id, min_level, tier, rep, premium in re.findall(
            r"\((\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),",
            statement(read(SPEC_SQL), 'ai_playerbot_enchant_candidates')):
        rows.append(tuple(int(value) for value in (spell_id, slot_id, min_level, tier, rep, premium)))
    return rows


def load_enchants():
    """(class, spec, slot) -> {'empty': pct, 'best': (item id, pct)} for the best-geared population."""
    entries = {}
    with open(AOTC_CSV, encoding='utf-8') as handle:
        for row in csv.DictReader(handle):
            if row['population'] != 'best_geared':
                continue
            key = (row['class'], row['spec'], row['slot'])
            entry = entries.setdefault(key, {'empty': 0.0, 'best': None})
            if row['name'] == 'No enchant':
                entry['empty'] = float(row['pct'])
            elif entry['best'] is None or float(row['pct']) > entry['best'][1]:
                entry['best'] = (int(row['wowhead_id'].split('=')[1]), float(row['pct']))
    return entries


class SpecEnchantDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.weights = load_weight_scales()
        cls.rows = load_spec_rows()
        cls.enchants = load_enchants()
        cls.pool = load_pool()

    def test_spec_ids_match_the_weight_scales(self):
        for (class_name, spec_name), spec_ids in SPECS.items():
            for spec_id in spec_ids:
                self.assertIn(spec_id, self.weights)
                self.assertEqual(self.weights[spec_id][1], CLASS_IDS[class_name],
                    'spec id %d is not a %s scale' % (spec_id, class_name))
        # Every weight-scale spec with an armory page must be covered, and nothing else may be in the table.
        covered = {spec_id for spec_ids in SPECS.values() for spec_id in spec_ids}
        for spec_id, (name, clazz) in self.weights.items():
            if spec_id in covered:
                continue
            self.assertNotIn(spec_id, {key[0] for key in self.rows},
                'spec id %d (%s) has no armory data and must fall back to the pool' % (spec_id, name))
        self.assertEqual(len(covered), 28)

    def test_every_spec_has_every_slot_once(self):
        expected = {(spec_id, slot) for spec_ids in SPECS.values() for spec_id in spec_ids for slot in SLOTS.values()}
        self.assertEqual(set(self.rows), expected)

    def test_rows_follow_the_best_geared_rule(self):
        for (class_name, spec_name), spec_ids in SPECS.items():
            for slot_name, slot_id in SLOTS.items():
                entry = self.enchants[(class_name, spec_name, slot_name)]
                empty = entry['empty']
                leave_empty = empty >= EMPTY_SHARE or entry['best'] is None
                for spec_id in spec_ids:
                    row = self.rows[(spec_id, slot_id)]
                    where = '%s/%s slot %d (%s)' % (class_name, spec_name, slot_id, slot_name)
                    self.assertAlmostEqual(row['none_pct'], empty, places=1, msg=where)
                    if leave_empty:
                        self.assertEqual(row['spell_id'], 0, '%s must stay empty' % where)
                        self.assertEqual(row['item_id'], 0, where)
                        self.assertEqual(row['name'], 'No enchant', where)
                    else:
                        self.assertNotEqual(row['spell_id'], 0, '%s must take the common enchant' % where)
                        self.assertEqual(row['item_id'], entry['best'][0], where)
                        self.assertNotEqual(row['name'], 'No enchant', where)

    def test_every_choice_is_a_pool_row_for_its_slot(self):
        for (spec_id, slot_id), row in self.rows.items():
            if row['spell_id']:
                self.assertIn((row['spell_id'], slot_id), self.pool,
                    'spec %d slot %d: spell %d is not in ai_playerbot_enchant_candidates for that slot'
                    % (spec_id, slot_id, row['spell_id']))

    def test_added_pool_rows_are_level_60_and_used(self):
        used = {(row['spell_id'], slot_id) for (_, slot_id), row in self.rows.items() if row['spell_id']}
        added = load_added_pool_rows()
        self.assertTrue(added)
        for spell_id, slot_id, min_level, tier, rep, premium in added:
            self.assertIn((spell_id, slot_id), used, 'added pool row %d/%d is not used by the table' % (spell_id, slot_id))
            self.assertEqual((min_level, tier, rep, premium), (60, 0, 0, 0),
                'added pool row %d/%d must be level 60 and base-tier' % (spell_id, slot_id))


if __name__ == '__main__':
    unittest.main()
