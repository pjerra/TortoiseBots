#!/usr/bin/env python3
"""No module query may bypass the world-database schema guard (ForeverCore port, slice 5 review fix N5, item 1).

On ForeverCore a query that names a missing table or column ABORTS THE SERVER after about ten seconds (MySQLConnection.cpp: ER_NO_SUCH_TABLE /
ER_BAD_FIELD_ERROR). Every module query therefore goes through `WorldQueryCompat` (forever/src/compat/TbCompat_S5.h), which asks information_schema first.
This test greps forever/src and the S5 loader files for a direct Query / PQuery / Execute call on a database object and fails on any outside the guard's own
implementation (and the one documented legacy exception below).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SCANNED = list((ROOT / "forever" / "src").rglob("*.cpp")) + list((ROOT / "forever" / "src").rglob("*.h")) + [
    ROOT / "ai" / "playerbot" / "DungeonPlanMgr.cpp",
    ROOT / "ai" / "playerbot" / "BossTacticsMgr.cpp",
]

# A direct call on a core database pool: WorldDatabase.Query(...), CharacterDatabase.PQuery(...), LoginDatabase.Execute(...) ...
DIRECT = re.compile(r"\b(?:World|Character|Login|Hotfix)Database\s*(?:\.|->)\s*"
                    r"(?:Query|PQuery|AsyncQuery|AsyncPQuery|Execute|PExecute|DirectExecute|DirectPExecute|GetPreparedStatement)\b")

# The guard's own implementation: the schema probe and the guarded query itself (two calls, nothing else).
GUARD_FILE = "forever/src/compat/TbCompat_S5.h"
GUARD_CALLS = 2
# The other place: the character-database shims of the unported-feature layer, `CharacterPQueryCompat` (a read) and `SaveSummonedPetNumberCompat` (the
# `characters.summonedPetNumber` write the pet host makes). Both serve the legacy engine's character database (core tables that always exist), not a plan
# or module table, and are outside slice 5; the count is named here so it cannot grow unnoticed.
LEGACY_EXCEPTION = {"forever/src/compat/TbCompat_Unported.h": 2}
# The characters-database guard of the commander binding store (package P2, forever/docs/CMD-P2-NOTES.md). ForeverCore's abort rule covers every pool,
# so the store asks information_schema the same way on the characters database; its three raw calls are the probe, one guarded SELECT and one guarded
# synchronous statement. The store itself makes none, and forever/tools/test_cmd_p2_wiring.py pins where every one of them stands.
CHAR_GUARD_FILE = "forever/src/compat/TbCompat_CharStore.h"
CHAR_GUARD_CALLS = 3


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def direct_calls(text: str):
    return [text.count("\n", 0, m.start()) + 1 for m in DIRECT.finditer(strip_comments(text))]


class NoDirectQueries(unittest.TestCase):
    def test_scanner_finds_what_it_should(self):
        # the scanner itself is not vacuous
        self.assertEqual(direct_calls("auto r = WorldDatabase.Query(sql);"), [1])
        self.assertEqual(direct_calls("x;\nCharacterDatabase . PQuery(\"a\");"), [2])
        self.assertEqual(direct_calls("LoginDatabase->Execute(stmt);"), [1])
        self.assertEqual(direct_calls("// WorldDatabase.Query(sql)\n/* CharacterDatabase.Execute(x) */"), [])
        self.assertEqual(direct_calls("WorldDatabase.EscapeString(plan); auto t = CharacterDatabase.BeginTransaction();"), [])

    def test_scanned_files_exist(self):
        names = {str(p.relative_to(ROOT)) for p in SCANNED}
        for must in ("forever/src/host/run/TbPlanUnits.cpp", "forever/src/host/run/TbPlanCommands.cpp", GUARD_FILE, CHAR_GUARD_FILE,
                     "ai/playerbot/DungeonPlanMgr.cpp", "ai/playerbot/BossTacticsMgr.cpp"):
            self.assertIn(must, names)

    def test_no_query_bypasses_the_guard(self):
        found = {}
        for path in SCANNED:
            lines = direct_calls(path.read_text(encoding="utf-8", errors="replace"))
            if lines:
                found[str(path.relative_to(ROOT))] = lines
        allowed = dict(LEGACY_EXCEPTION)
        allowed[GUARD_FILE] = GUARD_CALLS
        allowed[CHAR_GUARD_FILE] = CHAR_GUARD_CALLS
        for name, lines in sorted(found.items()):
            self.assertIn(name, allowed, f"{name}:{lines}: a direct database query: route it through WorldQueryCompat (the schema guard)")
            self.assertEqual(len(lines), allowed[name], f"{name}: {len(lines)} direct calls at lines {lines}, expected {allowed[name]}")
        for name, count in allowed.items():
            self.assertEqual(len(found.get(name, [])), count, f"{name}: the allowed direct calls changed (expected {count})")

    def test_the_plan_readers_use_the_guard(self):
        for name in ("forever/src/host/run/TbPlanUnits.cpp", "forever/src/host/run/TbPlanCommands.cpp", "ai/playerbot/DungeonPlanMgr.cpp",
                     "ai/playerbot/BossTacticsMgr.cpp"):
            text = strip_comments((ROOT / name).read_text())
            self.assertIn("WorldQueryCompat(", text, name)
            self.assertNotRegex(text, r"\bWorldDatabase\s*\.\s*(?:Query|PQuery)", name)

    def test_the_guard_validates_names_in_the_where_clause_too(self):
        text = (ROOT / GUARD_FILE).read_text()
        self.assertIn("WorldQueryNamesCompat(", text)
        self.assertIn("bool* schemaMissing", text)


if __name__ == "__main__":
    unittest.main()
