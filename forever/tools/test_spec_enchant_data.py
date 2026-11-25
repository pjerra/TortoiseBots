#!/usr/bin/env python3
"""Pins `ai_playerbot_spec_enchant` (the level-60 common enchant per class, spec and slot) to its source.

The rows live in forever/sql/world/2026_11_25_00_tortoisebots_spec_enchant.sql and are derived from
forever/data/aotc/enchants-60.csv (aotc.gg armory read 2026-11-24, `best_geared` population): when at least 60%
of those characters have no enchant in a slot, the slot is left empty (`spellid` 0); otherwise the most used
non-empty enchant is the spec's choice. The test recomputes that rule from the CSV and checks the committed
rows, the class and spec-name keys they name (they must be real weight scales of that class), and each choice's
level requirement (`min_level`: its Turtle candidate-pool row where one exists, 60 for the level-60-only Forever
enchantments the pool lacks).

    python3 -m unittest forever/tools/test_spec_enchant_data.py      # from the repo root
"""
import csv
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FOREVER_SPEC_SQL = os.path.join(ROOT, 'forever', 'sql', 'world', '2026_11_25_00_tortoisebots_spec_enchant.sql')
TURTLE_POOL_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20260925150000_world.sql')
WEIGHT_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20260916090001_world.sql')
TANK_WEIGHT_SQL = os.path.join(ROOT, 'data', 'sql', 'world', '20261020173406_world.sql')
AOTC_CSV = os.path.join(ROOT, 'forever', 'data', 'aotc', 'enchants-60.csv')

# Core EquipmentSlots values the armory page covers, by armory slot name.
SLOTS = {
    'Head': 0, 'Neck': 1, 'Back': 14, 'Chest': 4, 'Wrist': 8, 'Hands': 9,
    'Legs': 6, 'Feet': 7, 'Main Hand': 15, 'Off Hand': 16, 'Ranged': 17,
}

# The armory's class and spec name -> our core class id and weight-scale spec names. A `feral` druid is one
# armory spec but two of our scales (tank and dps); `shamantank` has no armory page and therefore no row.
SPECS = {
    ('warrior', 'arms'): (1, ['arms']),
    ('warrior', 'fury'): (1, ['fury']),
    ('warrior', 'protection'): (1, ['prot']),
    ('paladin', 'holy'): (2, ['holy']),
    ('paladin', 'protection'): (2, ['prot']),
    ('paladin', 'retribution'): (2, ['retrib']),
    ('hunter', 'beast-mastery'): (3, ['beast']),
    ('hunter', 'marksmanship'): (3, ['marks']),
    ('hunter', 'survival'): (3, ['surv']),
    ('rogue', 'assassination'): (4, ['assas']),
    ('rogue', 'combat'): (4, ['combat']),
    ('rogue', 'subtlety'): (4, ['subtle']),
    ('priest', 'discipline'): (5, ['disc']),
    ('priest', 'holy'): (5, ['holy']),
    ('priest', 'shadow'): (5, ['shadow']),
    ('shaman', 'elemental'): (7, ['elem']),
    ('shaman', 'enhancement'): (7, ['enhance']),
    ('shaman', 'restoration'): (7, ['resto']),
    ('mage', 'arcane'): (8, ['arcane']),
    ('mage', 'fire'): (8, ['fire']),
    ('mage', 'frost'): (8, ['frost']),
    ('warlock', 'affliction'): (9, ['afflic']),
    ('warlock', 'demonology'): (9, ['demo']),
    ('warlock', 'destruction'): (9, ['destro']),
    ('druid', 'balance'): (11, ['balance']),
    ('druid', 'feral'): (11, ['feraltank', 'feraldps']),
    ('druid', 'restoration'): (11, ['resto']),
}
EMPTY_SHARE = 60.0

# The five common Forever enchantments the Turtle candidate pool lacks; their rows carry min_level 60 by
# construction (the armory data only says level-60 characters use them).
FOREVER_ONLY_SPELLS = {1248757, 1248805, 1254767, 1254770, 1328948}


def read(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def statement(text, table):
    """The INSERT body for `table`, from `INSERT INTO ... VALUES` up to the terminating semicolon."""
    match = re.search(r'INSERT (?:IGNORE )?INTO `%s`[^;]*?VALUES(.*?);' % re.escape(table), text, re.S)
    return match.group(1) if match else ''


def load_weight_scales():
    """(class id, scale name) -> (scale name, class id), the pairs the table is keyed by."""
    scales = {}
    for path in (WEIGHT_SQL, TANK_WEIGHT_SQL):
        for spec_id, name, clazz in re.findall(r"\((\d+), '(\w+)', (\d+)\)", statement(read(path), 'ai_playerbot_weightscales')):
            scales[int(spec_id)] = (name, int(clazz))
    return scales


def load_spec_rows():
    """(class id, scale name, slot) -> {spell_id, min_level, item_id, none_pct, name}."""
    rows = {}
    for clazz, spec, slot, spell, min_level, item_id, none_pct, name in re.findall(
            r"\((\d+),\s*'(\w+)',\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.]+),\s*'([^']*)'\)",
            statement(read(FOREVER_SPEC_SQL), 'ai_playerbot_spec_enchant')):
        key = (int(clazz), spec, int(slot))
        assert key not in rows, 'duplicate class/spec/slot %s' % (key,)
        rows[key] = dict(spell_id=int(spell), min_level=int(min_level), item_id=int(item_id),
                         none_pct=float(none_pct), name=name)
    return rows


def load_pool():
    """(spell id, slot id) -> min_level, the Turtle candidate pool."""
    pool = {}
    for spell, slot, min_level in re.findall(
            r"\((\d+), (\d+), (\d+), \d+, \d+, \d+, '[^']*'\)", statement(read(TURTLE_POOL_SQL), 'ai_playerbot_enchant_candidates')):
        pool[(int(spell), int(slot))] = int(min_level)
    return pool


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

    def test_file_is_idempotent_and_self_contained(self):
        text = read(FOREVER_SPEC_SQL)
        self.assertIn('CREATE TABLE IF NOT EXISTS `ai_playerbot_spec_enchant`', text)
        self.assertIn('DELETE FROM `ai_playerbot_spec_enchant`', text)
        self.assertIn('START TRANSACTION;', text)
        self.assertTrue(text.endswith(';\n'))

    def test_keys_are_real_weight_scales_of_their_class(self):
        by_class = {}
        for _spec_id, (name, clazz) in self.weights.items():
            by_class.setdefault(clazz, set()).add(name)
        covered = set()
        for (class_name, spec_name), (class_id, scale_names) in SPECS.items():
            for scale in scale_names:
                covered.add((class_id, scale))
                self.assertIn(scale, by_class.get(class_id, set()),
                    '%s/%s: (%d, %s) is not a weight scale of that class' % (class_name, spec_name, class_id, scale))
        # Every weight-scale spec with an armory page is covered, and nothing else is in the table.
        self.assertEqual(len(covered), 28)
        for (class_id, scale) in {(clazz, name) for name, clazz in self.weights.values()}:
            if (class_id, scale) in covered:
                continue
            self.assertNotIn((class_id, scale), {(key[0], key[1]) for key in self.rows},
                '(%d, %s) has no armory data and must fall back to the ordinary picker' % (class_id, scale))

    def test_every_spec_has_every_slot_once(self):
        expected = {(class_id, scale, slot)
                    for (_, _), (class_id, scale_names) in SPECS.items()
                    for scale in scale_names for slot in SLOTS.values()}
        self.assertEqual(set(self.rows), expected)

    def test_rows_follow_the_best_geared_rule(self):
        for (class_name, spec_name), (class_id, scale_names) in SPECS.items():
            for slot_name, slot_id in SLOTS.items():
                entry = self.enchants[(class_name, spec_name, slot_name)]
                leave_empty = entry['empty'] >= EMPTY_SHARE or entry['best'] is None
                for scale in scale_names:
                    row = self.rows[(class_id, scale, slot_id)]
                    where = '%s/%s (%d, %s) slot %d (%s)' % (class_name, spec_name, class_id, scale, slot_id, slot_name)
                    self.assertAlmostEqual(row['none_pct'], entry['empty'], places=1, msg=where)
                    if leave_empty:
                        self.assertEqual(row['spell_id'], 0, '%s must stay empty' % where)
                        self.assertEqual(row['item_id'], 0, where)
                        self.assertEqual(row['name'], 'No enchant', where)
                    else:
                        self.assertNotEqual(row['spell_id'], 0, '%s must take the common enchant' % where)
                        self.assertEqual(row['item_id'], entry['best'][0], where)
                        self.assertNotEqual(row['name'], 'No enchant', where)

    def test_level_requirement_matches_the_candidate_pool(self):
        for (class_id, scale, slot), row in self.rows.items():
            where = '(%d, %s) slot %d' % (class_id, scale, slot)
            if not row['spell_id']:
                self.assertEqual(row['min_level'], 0, '%s: an empty slot has no requirement' % where)
                continue
            pool_min = self.pool.get((row['spell_id'], slot))
            if pool_min is not None:
                self.assertEqual(row['min_level'], pool_min, '%s: min_level must be the pool row' % where)
            else:
                self.assertIn(row['spell_id'], FOREVER_ONLY_SPELLS,
                    '%s: spell %d is neither in the pool nor a known level-60 Forever enchantment'
                    % (where, row['spell_id']))
                self.assertEqual(row['min_level'], 60, '%s: a pool-less choice is level 60 only' % where)

    def test_every_forever_only_spell_is_used(self):
        used = {row['spell_id'] for row in self.rows.values()}
        self.assertEqual(FOREVER_ONLY_SPELLS - used, set(),
            'a Forever-only enchant is in the table list but no row uses it')


if __name__ == '__main__':
    unittest.main()
