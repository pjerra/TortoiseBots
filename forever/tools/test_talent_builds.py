#!/usr/bin/env python3
"""Every bot talent build obeys the class trees the server validates (talent_builds.py), and the checker catches each rule.

    python3 forever/tools/test_talent_builds.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import talent_builds as tb  # noqa: E402


class TalentBuildsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.trees = tb.load_trees()
        cls.builds = tb.builds_by_spec(tb.read_csv(tb.BUILDS))

    def node(self, cls, name):
        hits = [n for n in self.trees[cls].values() if n['name'] == name]
        self.assertEqual(len(hits), 1, name)
        return hits[0]

    def step_id(self, node):
        return node['talent_id'] or node['node']

    def test_every_build_passes(self):
        self.assertEqual(tb.check_all(), [])

    def test_every_spec_has_a_build(self):
        for cls, specs in tb.SPEC_TABS.items():
            for spec in specs:
                self.assertIn((cls, spec), self.builds)

    def test_header_matches_csv(self):
        self.assertEqual(tb.header_steps(open(tb.HEADER).read()), tb.table_steps(tb.read_csv(tb.BUILDS)))

    def test_rank_over_max_is_caught(self):
        steps = list(self.builds[('warrior', 'arms')])
        itm = self.step_id(self.node('warrior', 'Improved Tactical Mastery'))  # max 3 on Forever, 5 in Talent.db2
        steps = [(i, r + 2 if i == itm else r) for i, r in steps]
        errors, _ = tb.check_build(self.trees['warrior'], steps)
        self.assertTrue(any('over the max 3' in e for e in errors), errors)

    def test_gate_is_caught(self):
        # Mortal Strike (row 6) as the first point.
        ms = self.step_id(self.node('warrior', 'Mortal Strike'))
        steps = [(ms, 1)] + [s for s in self.builds[('warrior', 'arms')] if s[0] != ms]
        errors, _ = tb.check_build(self.trees['warrior'], steps)
        self.assertTrue(any('Mortal Strike' in e and 'row needs 30' in e for e in errors), errors)

    def test_parent_is_caught(self):
        # Impale needs Deep Wounds full.
        dw = self.step_id(self.node('warrior', 'Deep Wounds'))
        steps = [(i, 2 if i == dw else r) for i, r in self.builds[('warrior', 'arms')]] + [(self.step_id(self.node('warrior', 'Improved Hamstring')), 1)]
        errors, _ = tb.check_build(self.trees['warrior'], steps)
        self.assertTrue(any('Impale' in e and 'parent not filled' in e for e in errors), errors)

    def test_short_build_is_caught(self):
        steps = list(self.builds[('mage', 'frost')])
        steps[-1] = (steps[-1][0], steps[-1][1] - 1)
        errors, _ = tb.check_build(self.trees['mage'], steps)
        self.assertIn('spends 50 points, not 51', errors)

    def test_unknown_id_is_caught(self):
        errors, _ = tb.check_build(self.trees['rogue'], [(132, 1)])  # Talent.db2 Weaponmaster: no rogue node
        self.assertTrue(any('not a node of this class tree' in e for e in errors), errors)

    def test_forever_only_talents_are_named_by_node(self):
        wm = self.node('warrior', 'Weaponmaster')
        self.assertEqual(wm['talent_id'], 0)
        self.assertGreaterEqual(wm['node'], tb.TRAIT_NODE_ID_MIN)
        self.assertIn((wm['node'], 5), self.builds[('warrior', 'arms')])


if __name__ == '__main__':
    unittest.main()
