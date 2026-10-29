"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_tactic.

The loader (ai/playerbot/BossTacticsMgr.cpp) skips a bad row with only a count in the
log, so a typo in a migration would silently turn a tactic off. This reads every
INSERT into ai_boss_tactic and checks what BossTacticTable::Add checks, plus the
primary key and the shape of each kind.
"""
import glob
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = {"interrupt", "kill_first", "behind", "air_target"}
ROW = re.compile(r"^\((\d+), (\d+), '(\w+)', (\d+), (\d+), (\d+), (-?[\d.]+), '(?:[^']|'')*'\)[,;]$")


def migration_rows():
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "INSERT INTO `ai_boss_tactic`" not in text:
            continue
        in_insert = False
        for line in text.splitlines():
            if line.startswith("INSERT INTO `ai_boss_tactic`"):
                in_insert = True
                continue
            if not in_insert or line.startswith("--") or not line.strip():
                continue
            match = ROW.match(line)
            if not match:
                raise AssertionError("unparsable ai_boss_tactic row in %s: %s" % (os.path.basename(path), line))
            rows.append((os.path.basename(path),) + match.groups())
            if line.endswith(";"):
                in_insert = False
    return rows


class BossTacticsSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_tactic rows found")

    def test_rows_are_loadable(self):
        for name, map_id, boss, kind, target, spell, priority, param in migration_rows():
            where = "%s boss %s kind %s" % (name, boss, kind)
            self.assertIn(kind, KINDS, where)
            self.assertGreater(int(map_id), 0, where)
            self.assertGreater(int(boss), 0, where)
            self.assertLess(int(priority), 256, where)
            if kind == "interrupt":
                self.assertGreater(int(spell), 0, where)
            if kind == "air_target":
                self.assertGreater(int(spell), 0, where)
            if kind == "behind":
                self.assertGreater(float(param), 0.0, where)

    def test_primary_keys_are_unique(self):
        seen = set()
        for name, map_id, boss, kind, target, spell, _priority, _param in migration_rows():
            key = (map_id, boss, kind, target, spell)
            self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, name))
            seen.add(key)

    def test_plan_interrupt_bosses_have_rows(self):
        # pjerra/turtle-agent#254: the turtle-agent plan JSON lists these
        # interrupts, and the module never read them. Fixed table, kept here
        # so the rows cannot silently disappear; do not read the plan repo.
        expected = [
            (43, 3673, 6778),
            (43, 3673, 700),
            (43, 3673, 9532),
            (43, 3669, 8040),
            (43, 3669, 9532),
            (43, 3669, 5187),
            (43, 3670, 5187),
            (43, 3670, 700),
            (43, 3670, 9532),
            (43, 3674, 6254),
            (43, 3654, 7967),
            (70, 4854, 8292),
            (70, 4854, 9532),
            (36, 647, 5208),
        ]
        have = set(
            (map_id, boss, kind, target, spell)
            for _, map_id, boss, kind, target, spell, _priority, _param in migration_rows()
        )
        for map_id, boss, spell in expected:
            self.assertIn(
                (str(map_id), str(boss), "interrupt", "0", str(spell)),
                have,
                "missing ai_boss_tactic interrupt row map %s boss %s spell %s" % (map_id, boss, spell),
            )

    def test_onyxia_air_target_row(self):
        # Onyxia's air phase is the Hover aura 17131 (core boss_onyxia.cpp);
        # the row is what keeps her a target, so it may not disappear.
        self.assertIn(
            ("249", "10184", "air_target", "0", "17131"),
            set((map_id, boss, kind, target, spell)
                for _, map_id, boss, kind, target, spell, _priority, _param in migration_rows()),
            "missing ai_boss_tactic air_target row map 249 boss 10184 spell 17131",
        )


if __name__ == "__main__":
    unittest.main()
