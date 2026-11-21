#!/usr/bin/env python3
"""Pins for the weight-scale migration (forever/sql/world/2026_11_20_20_tortoisebots_weightscales.sql).

The updater applies the file against whatever world database it runs on, so the
file must carry no hard-coded schema qualifier, and the delete+inserts that
replace the module's own scale rows must run in one transaction. The deletes
stay scoped to the module's own scale ids: scales a server added under other
ids are left alone.

Run:
  python3 forever/tools/test_weightscales_sql.py                 # the worktree file must PASS
  python3 forever/tools/test_weightscales_sql.py <old-file>      # the pre-fix file must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SQL_FILE = ROOT / "forever/sql/world/2026_11_20_20_tortoisebots_weightscales.sql"

OWN_IDS = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,20,21,22,23,24,25,26,27,28,29,30,31,32"


def check_no_schema_prefix(text):
    if re.search(r"`?\bworld\b`?\s*\.", text):
        raise AssertionError("migration hard-codes the `world` schema; the updater picks the database")


def check_replace_in_transaction(text):
    begin = text.find("START TRANSACTION")
    end = text.find("COMMIT")
    if begin < 0 or end < 0 or end < begin:
        raise AssertionError("delete+inserts are not wrapped in START TRANSACTION ... COMMIT")
    first_delete = text.find("DELETE FROM")
    last_insert = text.rfind("INSERT INTO")
    if first_delete < 0 or last_insert < 0:
        raise AssertionError("migration has no DELETE/INSERT pair to transact")
    if not (begin < first_delete < last_insert < end):
        raise AssertionError("transaction does not cover the delete+inserts")


def check_only_own_ids_replaced(text):
    for table in ("ai_playerbot_weightscale_data", "ai_playerbot_weightscales"):
        want = "DELETE FROM `%s` WHERE `id` IN (%s);" % (table, OWN_IDS)
        if want not in text:
            raise AssertionError("migration no longer replaces only its own scale ids in " + table)
    bare = [line for line in text.splitlines()
            if line.startswith("DELETE FROM") and "WHERE" not in line]
    if bare:
        raise AssertionError("unscoped DELETE: " + bare[0])


def check_inserts_intact(text):
    if text.count("INSERT INTO `ai_playerbot_weightscales`") != 1:
        raise AssertionError("weightscales INSERT missing or duplicated")
    if text.count("INSERT INTO `ai_playerbot_weightscale_data`") != 1:
        raise AssertionError("weightscale_data INSERT missing or duplicated")
    for probe in ("(1, 'arms', 1),", "(16, 'shamantank', 7),", "(32, 'feraldps', 11)"):
        if probe not in text:
            raise AssertionError("weightscales INSERT lost row " + probe)


def check_file(path):
    text = pathlib.Path(path).read_text()
    check_no_schema_prefix(text)
    check_replace_in_transaction(text)
    check_only_own_ids_replaced(text)
    check_inserts_intact(text)


class WeightscalesSql(unittest.TestCase):
    def test_migration_shape(self):
        check_file(SQL_FILE)

    def test_prefixed_untransacted_file_fails(self):
        old = SQL_FILE.read_text()
        mutated = old.replace("START TRANSACTION;\n", "").replace("\nCOMMIT;", "")
        mutated = mutated.replace("DELETE FROM `ai_playerbot_weightscale_data`",
                                  "DELETE FROM `world`.`ai_playerbot_weightscale_data`")
        with self.assertRaises(AssertionError):
            check_no_schema_prefix(mutated)
        with self.assertRaises(AssertionError):
            check_replace_in_transaction(mutated)


if __name__ == "__main__":
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        check_file(sys.argv[1])
        print("PASS " + sys.argv[1])
    else:
        unittest.main()
