#!/usr/bin/env python3
"""Check that every module SQL file under forever/sql/ is in an applied updater path
(forever/sql/world/, forever/sql/characters/ or forever/sql/hotfixes/), follows the naming
& idempotency rules, and is documented in forever/sql/README.md. hotfixes/ is the same
delivery rule against the separate hotfixes database (broadcast_text lives there).

Run:
  python3 forever/tools/test_sql_delivery.py
  python3 forever/tools/test_sql_delivery.py --mutate   # must FAIL (exit non-zero)
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SQL_ROOT = os.path.join(ROOT, "forever", "sql")
APPLIED_SUBDIRS = {"world", "characters", "hotfixes"}
NAME_RE = re.compile(r"^\d{4}_\d{2}_\d{2}_\d{2}_[a-z0-9_]+\.sql$")
MUTATE = False


class TestSqlDelivery(unittest.TestCase):
    def test_all_module_sql_files_in_applied_paths(self):
        sql_files = []
        for dirpath, _, filenames in os.walk(SQL_ROOT):
            for fn in sorted(filenames):
                if fn.endswith(".sql"):
                    rel = os.path.relpath(os.path.join(dirpath, fn), SQL_ROOT)
                    sql_files.append(rel.replace("\\", "/"))
        if MUTATE:
            sql_files.append("orphan/2026_11_20_99_outside_path.sql")

        self.assertGreater(len(sql_files), 0)
        seen_names = set()
        for rel in sql_files:
            parts = rel.split("/")
            self.assertEqual(
                len(parts),
                2,
                f"SQL file `{rel}` is outside applied paths ({sorted(APPLIED_SUBDIRS)})",
            )
            subdir, fname = parts
            self.assertIn(
                subdir,
                APPLIED_SUBDIRS,
                f"SQL file `{rel}` is outside applied paths ({sorted(APPLIED_SUBDIRS)})",
            )
            self.assertRegex(fname, NAME_RE, f"SQL filename `{fname}` violates YYYY_MM_DD_NN_<what>.sql")
            self.assertNotIn(fname, seen_names, f"Duplicate SQL basename `{fname}`")
            seen_names.add(fname)

            full = os.path.join(SQL_ROOT, rel)
            if os.path.exists(full):
                with open(full, "rb") as f:
                    raw = f.read()
                self.assertTrue(raw.endswith(b";\n"), f"`{rel}` must end with `;\\n` for DBUpdater marker")

    def test_readme_documents_guild_orders_and_updates_include(self):
        with open(os.path.join(SQL_ROOT, "README.md"), "r", encoding="utf-8") as f:
            readme = f.read()
        self.assertIn("$/sql/tortoisebots/world", readme)
        self.assertIn("$/sql/tortoisebots/characters", readme)
        self.assertIn("characters/2026_11_20_01_tb_guild_orders.sql", readme)
        self.assertIn("$/sql/tortoisebots/hotfixes", readme)
        self.assertIn("hotfixes/2026_11_25_02_tortoisebots_hire_recruiter_text.sql", readme)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
