#!/usr/bin/env python3
"""Pins for the recruiter SQL of issue #298 (package P3).

The recruiter rows are hand-maintained SQL (one file per database: world rows in
forever/sql/world, greeting broadcast_text in forever/sql/hotfixes). Nothing else
checks that the rows still belong together, so this file does:

  * every spawn's entry has a template, a display and a level row;
  * every template's greeting npc_text (entry and entry+100) exists and its
    broadcast_text row is in the hotfixes file;
  * every recruiter has the gossip_menu + creature_template_gossip rows that keep
    its GOSSIP|QUESTGIVER npcflags visible to a player (menu id = entry, text =
    the default greeting npc_text);
  * every id stays in the recruiter range (1000000-1000069 for spawnable
    recruiters; 1000070-1000099 are the credit-only creatures of P4/P5), every
    spawn guid in 4000000+, and every column list matches its rows;
  * the deletes are idempotent and cover exactly what is inserted;
  * the ScriptName in the SQL is the one the code registers
    (forever/src/host/hire/TbHireRecruiter.cpp), and the six capital recruiters
    keep the quest-giver flag the unlock quests need.

Run:
  python3 forever/tools/test_hire_recruiter_sql.py
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORLD_SQL = ROOT / "forever/sql/world/2026_11_25_01_tortoisebots_hire_recruiters.sql"
HOTFIX_SQL = ROOT / "forever/sql/hotfixes/2026_11_25_02_tortoisebots_hire_recruiter_text.sql"
RECRUITER_CPP = ROOT / "forever/src/host/hire/TbHireRecruiter.cpp"

RECRUITER_FIRST, RECRUITER_LAST = 1000000, 1000099
WORLD_BUFF_CREDITS = {1000070, 1000071}   # this file owns the two world-buff kill credits
CONTRACT_CREDIT_FIRST = 1000080           # the hire-contract credits belong to the tier package
SPAWN_GUID_FIRST = 4000000
CAPITALS = {1000006, 1000012, 1000017, 1000018, 1000019, 1000025}


def parse_inserts(text):
    """{table: (columns, [row values, ...])} for every INSERT INTO `table` (...) VALUES (...);"""
    out = {}
    for m in re.finditer(r"INSERT INTO `(\w+)` \(([^;]*?)\) VALUES\n(.*?);\n", text, re.S):
        table = m.group(1)
        columns = [c.strip().strip("`") for c in m.group(2).split(",")]
        rows = []
        for line in m.group(3).splitlines():
            line = line.strip()
            if not line.startswith("("):
                continue
            rows.append(split_row(line))
        out[table] = (columns, rows)
    return out


def split_row(line):
    values, cur, quoted = [], "", False
    body = line.rstrip(",").rstrip(";")
    assert body[0] == "(" and body[-1] == ")", body
    for ch in body[1:-1]:
        if ch == "'":
            quoted = not quoted
            cur += ch
        elif ch == "," and not quoted:
            values.append(cur.strip())
            cur = ""
        else:
            cur += ch
    values.append(cur.strip())
    return values


def to_int(value):
    return int(value.strip("'"))


def read(path):
    if not path.exists():
        raise AssertionError("missing SQL file: %s" % path.relative_to(ROOT))
    return path.read_text(encoding="utf-8")


class RecruiterSql(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.world = read(WORLD_SQL)
        cls.hotfix = read(HOTFIX_SQL)
        cls.w = parse_inserts(cls.world)
        cls.h = parse_inserts(cls.hotfix)
        cls.templates = cls.w["creature_template"]
        cls.models = cls.w["creature_template_model"]
        cls.levels = cls.w["creature_classic_level"]
        cls.spawns = cls.w["creature"]
        cls.npc_text = cls.w["npc_text"]
        cls.gossip_menu = cls.w["gossip_menu"]
        cls.template_gossip = cls.w["creature_template_gossip"]
        cls.broadcast = cls.h["broadcast_text"]

    def test_every_insert_row_matches_its_column_list(self):
        for inserts in (self.w, self.h):
            for table, (columns, rows) in inserts.items():
                for row in rows:
                    self.assertEqual(len(row), len(columns), "%s row has %d values for %d columns: %s"
                                     % (table, len(row), len(columns), row[:3]))

    def test_counts(self):
        self.assertEqual(len(self.templates[1]), 57, "world: 55 recruiter templates + 2 world-buff credits")
        self.assertEqual(len(self.models[1]), 57)
        self.assertEqual(len(self.levels[1]), 55)
        self.assertEqual(len(self.spawns[1]), 55, "world: 55 spawns (one per Forever inn)")
        self.assertEqual(len(self.npc_text[1]), 110, "world: greeting + HireAllowPvP variant per recruiter")
        self.assertEqual(len(self.gossip_menu[1]), 55, "world: one greeting menu per recruiter")
        self.assertEqual(len(self.template_gossip[1]), 55, "world: one assigned menu per recruiter")
        self.assertEqual(len(self.broadcast[1]), 110, "hotfixes: greeting + variant per recruiter")

    def test_ids_are_in_range_and_unique(self):
        entries = [to_int(r[0]) for r in self.templates[1]]
        self.assertEqual(len(entries), len(set(entries)), "duplicate recruiter entry")
        for entry in entries:
            self.assertGreaterEqual(entry, RECRUITER_FIRST)
            self.assertLessEqual(entry, max(WORLD_BUFF_CREDITS),
                                 "entry %d is past this file's ids" % entry)
            if entry > max(WORLD_BUFF_CREDITS):
                self.fail("entry %d would collide with the contract credits" % entry)
        guids = [to_int(r[0]) for r in self.spawns[1]]
        self.assertEqual(len(guids), len(set(guids)), "duplicate spawn guid")
        for guid in guids:
            self.assertGreaterEqual(guid, SPAWN_GUID_FIRST)
            self.assertLess(guid, SPAWN_GUID_FIRST + 1000)

    def spawnable_entries(self):
        return {to_int(r[0]) for r in self.templates[1]} - WORLD_BUFF_CREDITS

    def test_every_spawn_has_a_template_and_vice_versa(self):
        entries = {to_int(r[0]) for r in self.templates[1]}
        spawn_entries = [to_int(r[1]) for r in self.spawns[1]]
        self.assertTrue(set(spawn_entries) <= entries, "a spawn has no template")
        self.assertFalse(set(spawn_entries) & WORLD_BUFF_CREDITS, "a credit-only creature is spawned")
        self.assertEqual(set(spawn_entries), self.spawnable_entries(),
                         "a recruiter template has no spawn")
        for row in self.spawns[1]:
            self.assertNotEqual(to_int(row[3]), 0, "spawn %s has zoneId 0" % row[0])
            self.assertNotEqual(to_int(row[4]), 0, "spawn %s has areaId 0" % row[0])

    def test_every_template_has_model_level_and_text(self):
        entries = {to_int(r[0]) for r in self.templates[1]}
        spawnable = self.spawnable_entries()
        model_ids = [to_int(r[0]) for r in self.models[1]]
        self.assertEqual(model_ids, list(dict.fromkeys(model_ids)), "a template has two model rows")
        self.assertEqual(set(model_ids), entries, "model rows do not match the templates")
        level_ids = {to_int(r[0]) for r in self.levels[1]}
        self.assertEqual(level_ids, spawnable, "level rows do not match the spawned recruiters")
        text_ids = {to_int(r[0]) for r in self.npc_text[1]}
        self.assertEqual(text_ids, spawnable | {entry + 100 for entry in spawnable},
                         "greeting npc_text rows do not match the spawned recruiters")

    def test_every_text_row_has_its_broadcast_text(self):
        texts = {to_int(r[0]): to_int(r[2]) for r in self.npc_text[1]}
        broadcast_ids = {to_int(r[0]) for r in self.broadcast[1]}
        for npc_id, broadcast_id in texts.items():
            self.assertEqual(broadcast_id, npc_id, "npc_text %d points at broadcast_text %d" % (npc_id, broadcast_id))
            self.assertIn(broadcast_id, broadcast_ids, "npc_text %d has no broadcast_text row" % npc_id)
        self.assertEqual(set(texts), broadcast_ids, "broadcast_text rows without an npc_text")
        for row in self.broadcast[1]:
            self.assertEqual(row[1], row[2], "broadcast_text %s: male and female text differ" % row[0])
            self.assertEqual(to_int(row[3]), 0, "broadcast_text %s is not English" % row[0])
            self.assertEqual(to_int(row[4]), 0, "broadcast_text %s is not a custom (VerifiedBuild 0) row" % row[0])

    def test_every_recruiter_has_the_gossip_menu_that_keeps_its_flag(self):
        spawnable = self.spawnable_entries()
        texts = {to_int(r[0]) for r in self.npc_text[1]}
        menu_ids = {}
        for row in self.gossip_menu[1]:
            menu_id, text_id = to_int(row[0]), to_int(row[1])
            self.assertNotIn(menu_id, menu_ids, "duplicate gossip_menu id %d" % menu_id)
            menu_ids[menu_id] = text_id
        self.assertEqual(set(menu_ids), spawnable, "gossip_menu rows do not match the spawned recruiters")
        for menu_id, text_id in menu_ids.items():
            self.assertEqual(text_id, menu_id, "gossip_menu %d shows text %d, not its greeting" % (menu_id, text_id))
            self.assertIn(text_id, texts, "gossip_menu %d has no npc_text" % menu_id)
        assigned = [to_int(r[0]) for r in self.template_gossip[1]]
        self.assertEqual(len(assigned), len(set(assigned)), "a recruiter has two gossip menus")
        self.assertEqual(set(assigned), spawnable,
                         "creature_template_gossip rows do not match the spawned recruiters")
        for row in self.template_gossip[1]:
            self.assertIn(to_int(row[1]), menu_ids,
                          "recruiter %s points at a menu that is not in gossip_menu" % row[0])

    def test_deletes_cover_every_inserted_id(self):
        # Each insert's ids must fall inside a delete range of the same table in the same file. The parsed
        # ranges keep the two files independent: the hotfix file only deletes broadcast_text.
        def deleted_ranges(text):
            ranges = {}
            for m in re.finditer(r"DELETE FROM `(\w+)` WHERE ([^;]*);", text):
                table, clause = m.group(1), m.group(2)
                spans = []
                for clause_part in clause.split(" OR "):
                    cm = re.search(r"`\w+` BETWEEN (\d+) AND (\d+)", clause_part)
                    if cm:
                        spans.append((int(cm.group(1)), int(cm.group(2))))
                ranges.setdefault(table, []).extend(spans)
            return ranges

        def insert_ids(inserts, table, index):
            columns, rows = inserts[table]
            return [to_int(r[index]) for r in rows]

        world_deletes = deleted_ranges(self.world)
        hotfix_deletes = deleted_ranges(self.hotfix)
        for table, index in (("creature_template", 0), ("creature_template_model", 0),
                             ("creature_classic_level", 0), ("creature", 0),
                             ("gossip_menu", 0), ("creature_template_gossip", 0)):
            ids = insert_ids(self.w, table, index)
            spans = world_deletes.get(table, [])
            for value in ids:
                self.assertTrue(any(lo <= value <= hi for lo, hi in spans),
                                "%s id %d is inserted but not deleted (ranges %s)" % (table, value, spans))
        for table, index in (("npc_text", 0), ("broadcast_text", 0)):
            inserts = self.w if table == "npc_text" else self.h
            deletes = world_deletes if table == "npc_text" else hotfix_deletes
            spans = deletes.get(table, [])
            for value in insert_ids(inserts, table, index):
                self.assertTrue(any(lo <= value <= hi for lo, hi in spans),
                                "%s id %d is inserted but not deleted (ranges %s)" % (table, value, spans))
        # A delete that reaches into the credit-only ids would wipe another package's rows.
        for table in ("creature_template", "creature_template_model", "creature_classic_level", "creature",
                      "gossip_menu", "creature_template_gossip"):
            for lo, hi in world_deletes.get(table, []):
                if lo >= SPAWN_GUID_FIRST:
                    continue    # the spawn guid range, not an entry range
                self.assertLess(hi, CONTRACT_CREDIT_FIRST,
                                "%s delete reaches the contract credits" % table)
                self.assertLessEqual(hi, max(WORLD_BUFF_CREDITS),
                                     "%s delete ends past this file's ids" % table)
        for entry in WORLD_BUFF_CREDITS:
            self.assertTrue(any(lo <= entry <= hi
                                for lo, hi in world_deletes.get("creature_template", [])),
                            "world-buff credit %d is inserted but not deleted" % entry)

    def test_script_name_is_the_one_the_code_registers(self):
        by_entry = {to_int(r[0]): r for r in self.templates[1]}
        names = {by_entry[e][39].strip("'") for e in by_entry if e not in WORLD_BUFF_CREDITS}
        self.assertEqual(names, {"tortoisebots_hire_recruiter"})
        for entry in WORLD_BUFF_CREDITS:
            self.assertEqual(by_entry[entry][39].strip("'"), "",
                             "credit creature %d must not carry the recruiter AI" % entry)
        code = RECRUITER_CPP.read_text(encoding="utf-8")
        self.assertIn('"tortoisebots_hire_recruiter"', code,
                      "the SQL ScriptName has no matching GenericCreatureScript name in TbHireRecruiter.cpp")

    def test_capital_recruiters_are_quest_givers(self):
        by_entry = {to_int(r[0]): r for r in self.templates[1]}
        for entry, row in by_entry.items():
            want = 3 if entry in CAPITALS else (0 if entry in WORLD_BUFF_CREDITS else 1)
            self.assertEqual(to_int(row[11]), want, "npcflag of %d" % entry)
        for entry in CAPITALS:
            self.assertIn(entry, by_entry, "capital recruiter %d is missing" % entry)

    def test_no_schema_prefix(self):
        for text, name in ((self.world, "world"), (self.hotfix, "hotfixes")):
            self.assertIsNone(re.search(r"`?\b(world|hotfixes|characters)\b`?\s*\.", text),
                              "%s SQL hard-codes a database name" % name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
