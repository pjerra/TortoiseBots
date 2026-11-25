"""Check RFC rows against the exported Forever facts and navigation plan."""
import csv
import io
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SQL = ROOT / 'forever/sql/world/2026_11_21_30_tortoisebots_boss_tactics_rfc.sql'

class RfcRows(unittest.TestCase):
    def test_rules_have_verified_bosses_and_targets(self):
        facts = list(csv.DictReader(io.StringIO((ROOT / 'forever/data/waterys/rfc/rfc-bosses.csv').read_text())))
        mobs = {int(r['entry']) for r in csv.DictReader(io.StringIO((ROOT / 'forever/data/waterys/rfc/rfc-mobs.csv').read_text()))}
        bosses = {int(r['boss_entry']) for r in facts}
        spells = {(int(r['boss_entry']), int(r['ability_spell_id'])) for r in facts}
        rows = re.findall(r"\(389,(\d+),'(behind|kill_first|interrupt)',(\d+),(\d+),", SQL.read_text())
        self.assertEqual(len(rows), 6)
        for boss, kind, target, spell in rows:
            self.assertIn(int(boss), bosses)
            if kind == 'interrupt': self.assertIn((int(boss), int(spell)), spells)
            if kind == 'kill_first': self.assertIn(int(target), mobs)
        self.assertNotIn('11321', SQL.read_text())  # Molten-pack trash tactics are issue #23.

    def test_tank_point_matches_shipped_plan(self):
        plan = (ROOT / 'forever/sql/world/2026_11_20_02_tortoisebots_plan_ragefire_chasm.sql').read_text()
        self.assertIn("'boss', 11517, -147.550, 38.710, -38.800", plan)
        self.assertIn("'tank',-147.550,38.710,-38.800,1", SQL.read_text())
        self.assertIn('INSERT IGNORE INTO `ai_boss_stand`', SQL.read_text())
        self.assertIn('CREATE TABLE IF NOT EXISTS `ai_boss_stand`', SQL.read_text())

WC_SQL = ROOT / 'forever/sql/world/2026_11_22_30_tortoisebots_boss_tactics_wc.sql'


class WcRows(unittest.TestCase):
    """Every Wailing Caverns row is backed by forever/data/waterys/wc/wc-casts.csv (SmartAI casts at core b441bf74, DB2 70959 cast times)."""

    def facts(self):
        return list(csv.DictReader(io.StringIO((ROOT / 'forever/data/waterys/wc/wc-casts.csv').read_text())))

    def test_interrupts_are_listed_casts_with_a_cast_time(self):
        casts = {(int(r['entry']), int(r['spell_id'])): r for r in self.facts()}
        rows = re.findall(r"\(43,(\d+),'interrupt',0,(\d+),", WC_SQL.read_text())
        self.assertEqual(len(rows), 17)
        for boss, spell in rows:
            fact = casts.get((int(boss), int(spell)))
            self.assertIsNotNone(fact, (boss, spell))
            self.assertTrue(fact['cast_ms'] and int(fact['cast_ms']) > 0, (boss, spell))

    def test_kill_first_targets_spawn_in_wc_and_kinds_stay_five_man(self):
        spawned = {int(r['entry']) for r in self.facts() if int(r['spawns_map43']) > 0}
        sql = WC_SQL.read_text()
        for boss, target in re.findall(r"\(43,(\d+),'kill_first',(\d+),", sql):
            self.assertIn(int(boss), spawned)
            self.assertIn(int(target), spawned)
        kinds = set(re.findall(r"\(43,\d+,'(\w+)',", sql))
        self.assertEqual(kinds, {'interrupt', 'kill_first'})
        self.assertNotIn('ai_boss_stand', sql)   # no unverified WC stand point
        self.assertIn('INSERT IGNORE INTO `ai_boss_tactic`', sql)

if __name__ == '__main__': unittest.main()
