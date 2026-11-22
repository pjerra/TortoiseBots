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

if __name__ == '__main__': unittest.main()
