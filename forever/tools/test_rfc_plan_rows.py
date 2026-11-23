#!/usr/bin/env python3
"""The Ragefire Chasm rows the module ships plan step 14 as one pull, the trogg pair and the five-pack.

  python3 forever/tools/test_rfc_plan_rows.py        (no server, no core)

Run A (level 22, 11 deaths at step 14) and the 11-21 test3 run (level 28, 8 deaths at step 14) and the level-18 S6 walk
were all wiped at step 14, the plan's leg 2 `trogg_cave`: the trogg pair at -238 -36 stands 16.9 yd from the five-pack's
Shaman, inside a same-level mob's 20 yd aggro radius from any spot the tank meets the pair at. So the plan's two-unit
pull woke the seven-unit five-pack unprepared (two shamans healing a fight the run sized for two), and the stand before
the five-pack (-221.2 -34.7 -54.7) was 17.5 yd from the pair: standing there woke the pair too.

The rows now plan one pull of all seven, from the pair's stand. The SQL is the exporter's output (forever-agent
`plan-export --forever`); this test reads it and checks the merge, the stand and the plan's counts. It fails against the
rows before the merge.
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MIGRATION = os.path.join(ROOT, 'forever', 'sql', 'world', '2026_11_20_02_tortoisebots_plan_ragefire_chasm.sql')

LEG_TROGG_CAVE = 2


def split_values(row):
    """One INSERT row's values as ('text'|'number', value): quoted strings ('' escapes) and bare numbers."""
    out = []
    i, n = 0, len(row)
    while i < n:
        while i < n and row[i] in ' ,':
            i += 1
        if i >= n:
            break
        if row[i] == "'":
            i += 1
            buf = []
            while i < n:
                if row[i] == "'":
                    if i + 1 < n and row[i + 1] == "'":
                        buf.append("'")
                        i += 2
                        continue
                    i += 1
                    break
                buf.append(row[i])
                i += 1
            out.append(('text', ''.join(buf)))
        else:
            j = i
            while j < n and row[j] != ',':
                j += 1
            out.append(('number', row[i:j].strip()))
            i = j
    return out


def insert_rows(text, table):
    """Every value tuple of `INSERT INTO <table> ...` as a list of split_values lists."""
    m = re.search(r'INSERT INTO `%s` \([^)]*\) VALUES\n(.*?);\n' % table, text, re.S)
    if m is None:
        raise AssertionError('the migration has no INSERT INTO `%s`' % table)
    body = m.group(1)
    rows = []
    for line in body.split('\n'):
        line = line.rstrip()
        if not line.startswith('('):
            continue
        if line.endswith(','):
            line = line[:-1]
        if not line.endswith(')'):
            raise AssertionError('a row is not closed: %r' % line)
        rows.append(split_values(line[1:-1]))
    return rows


def num(values, idx):
    return float(values[idx][1])


def txt(values, idx):
    return values[idx][1]


class RfcPlanRows(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(MIGRATION, encoding='utf-8') as f:
            cls.text = f.read()
        cls.steps = []
        for v in insert_rows(cls.text, 'ai_dungeon_step'):
            cls.steps.append({
                'leg': int(num(v, 1)),
                'seq': int(num(v, 2)),
                'kind': txt(v, 3),
                'x': num(v, 5), 'y': num(v, 6), 'z': num(v, 7),
                'radius': num(v, 8),
            })
        cls.units = []
        for v in insert_rows(cls.text, 'ai_dungeon_step_unit'):
            cls.units.append({
                'leg': int(num(v, 1)),
                'seq': int(num(v, 2)),
                'idx': int(num(v, 3)),
                'entry': int(num(v, 4)),
                'x': num(v, 5), 'y': num(v, 6), 'z': num(v, 7),
                'role': txt(v, 8),
            })

    def step(self, leg, seq):
        found = [s for s in self.steps if s['leg'] == leg and s['seq'] == seq]
        self.assertEqual(len(found), 1, 'leg %d seq %d is not exactly one step row' % (leg, seq))
        return found[0]

    def units_of(self, leg, seq):
        return sorted([u for u in self.units if u['leg'] == leg and u['seq'] == seq], key=lambda u: u['idx'])

    def leg_moves(self, leg):
        return [s for s in self.steps if s['leg'] == leg and s['kind'] == 'move']

    # Step 14 (leg 2 seq 4) is one pull of all seven: the pair's two 11318 first, then the five-pack's five.
    def test_trogg_cave_pair_and_fivepack_are_one_pull(self):
        s = self.step(LEG_TROGG_CAVE, 4)
        self.assertEqual(s['kind'], 'pull')
        self.assertEqual(s['radius'], 20.0)
        units = self.units_of(LEG_TROGG_CAVE, 4)
        self.assertEqual(len(units), 7, 'the merged pull is seven units: %r' % units)
        self.assertEqual([u['idx'] for u in units], list(range(7)))
        self.assertEqual([u['entry'] for u in units],
                         [11318, 11318, 11319, 11318, 11318, 11319, 11318],
                         'the pair first, then the five-pack, its order kept')
        # The positions are the plan's own: the pair, then the five-pack.
        self.assertEqual([(u['x'], u['y'], u['z']) for u in units],
                         [(-238.66, -31.56, -56.62), (-237.99, -40.35, -56.27),
                          (-253.89, -38.88, -60.46), (-262.22, -29.89, -60.61),
                          (-270.13, -31.88, -60.61), (-266.73, -28.03, -60.61),
                          (-266.61, -44.4, -60.85)])
        # Both shamans are in the row's entries: the pre-pull Polymorph and the Shaman-first order cover both.
        self.assertIn(11319, [u['entry'] for u in units])
        # The five-pack's own pull row (seq 6 before the merge) is gone: leg 2 has no second row at its centroid.
        for other in self.steps:
            if other['leg'] != LEG_TROGG_CAVE or other['kind'] != 'pull':
                continue
            if other['seq'] == 4:
                continue
            self.assertFalse(abs(other['x'] + 263.92) < 0.01 and abs(other['y'] + 34.62) < 0.01,
                             'the five-pack still has a pull row of its own: %r' % other)

    # The stand before the merged pull is the pair's own: the ramp point a manual walk reached.
    def test_stand_before_it_is_the_pairs(self):
        s = self.step(LEG_TROGG_CAVE, 3)
        self.assertEqual(s['kind'], 'move')
        self.assertAlmostEqual(s['x'], -205.0, places=2)
        self.assertAlmostEqual(s['y'], -34.0, places=2)
        self.assertAlmostEqual(s['z'], -49.9, places=2)
        # Nothing but the stand walks the party near the pack before the pull: seq 4 is the pull itself.
        self.assertEqual(self.step(LEG_TROGG_CAVE, 4)['kind'], 'pull')

    # No stand of the leg lies inside a same-level mob's 20 yd aggro radius of the merged pull, but the pair's stand.
    def test_no_stand_inside_the_pairs_aggro(self):
        units = self.units_of(LEG_TROGG_CAVE, 4)
        stand = self.step(LEG_TROGG_CAVE, 3)
        for s in self.leg_moves(LEG_TROGG_CAVE):
            if s['seq'] == 3:
                continue
            near = min(((s['x'] - u['x']) ** 2 + (s['y'] - u['y']) ** 2 + (s['z'] - u['z']) ** 2) ** 0.5
                       for u in units)
            self.assertGreater(near, 20.0,
                               'leg 2 seq %d (a move) stands %.1f yd from the merged pull' % (s['seq'], near))
        # The stand itself clears the wake reach: 33.5 yd from the nearest member (2-D), 49.1 yd from the five-pack's
        # Shaman at -253.89 (the far Trogg of the group stands 65.2 yd out).
        near2 = min(((stand['x'] - u['x']) ** 2 + (stand['y'] - u['y']) ** 2) ** 0.5 for u in units)
        self.assertAlmostEqual(near2, 33.5, delta=0.6)
        shaman = [u for u in units if u['entry'] == 11319][0]
        far = ((stand['x'] - shaman['x']) ** 2 + (stand['y'] - shaman['y']) ** 2) ** 0.5
        self.assertAlmostEqual(far, 49.1, delta=0.6)

    # The plan's shape after the merge: one pull fewer, one approach move fewer, the unit rows unchanged.
    def test_plan_counts(self):
        self.assertEqual(len(self.steps), 78)
        self.assertEqual(sum(1 for s in self.steps if s['kind'] == 'pull'), 50)
        self.assertEqual(len(self.units), 103)
        header = re.search(r'-- ragefire_chasm: map 389, (\d+) legs, (\d+) steps, (\d+) pull groups, (\d+) unit rows',
                           self.text)
        self.assertIsNotNone(header, 'the migration header is missing')
        self.assertEqual([int(g) for g in header.groups()], [9, 78, 50, 103])


if __name__ == '__main__':
    unittest.main()
