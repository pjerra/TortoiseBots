#!/usr/bin/env python3
"""Wiring pins for piece c-store (TBF #133): the persisted "manual saved string" AI value.

What it proves (each is a bug the build alone does not catch):
  * the value is registered through the generator (slice_gen_context.py STORE_KEYS), the generated
    SliceAiObjectContext.cpp is up to date (--check), and its creator line is the full build's own
    (slice_registry_check.py has no complaint);
  * the real PlayerbotDbStore::Load/Save live in forever/src/slice/SliceDbStore.cpp (VALUES only),
    the SliceLinkStubs no-ops are gone, and Reset/InvalidateStrategySnapshots stay undefined (#135's);
  * the store makes no direct database call (tools/test_forever_no_direct_queries.py gate): every
    statement runs after the schema probe, through compat/TbCompat_CharStore.h;
  * the characters migration ships the table TB's schema demands, idempotently, and the module's own
    CREATE names the same columns (the two cannot drift);
  * the brain saves values on teardown by its stored guidLow (no Player touched);
  * the strategy auto-save gate stays off (co/nc/de edits never reach Save; #135 flips it).

Run:  python3 -m unittest forever/tools/test_aistore_wiring.py
"""
import pathlib
import re
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
GEN = ROOT / "forever/tools/slice_gen_context.py"
CTX = ROOT / "forever/src/slice/SliceAiObjectContext.cpp"
STUBS = ROOT / "forever/src/slice/SliceLinkStubs.cpp"
STORE_CPP = ROOT / "forever/src/slice/SliceDbStore.cpp"
STORE_H = ROOT / "forever/src/slice/SliceDbStorePure.h"
ENGINE = ROOT / "forever/src/host/TbEngineBrain.cpp"
SQL = ROOT / "forever/sql/characters/2026_11_22_00_tb_aistore.sql"
README = ROOT / "forever/sql/README.md"
FULL_VALUE_CTX = ROOT / "ai/playerbot/strategy/values/ValueContext.h"

CREATE_COLUMNS = ["id", "guid", "preset", "key", "value"]


def code_only(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def create_of(text):
    m = re.search(r"CREATE TABLE IF NOT EXISTS\s+`ai_playerbot_db_store`\s*\((.*?)\)\s*ENGINE", text, re.S)
    assert m, "no CREATE TABLE IF NOT EXISTS `ai_playerbot_db_store`"
    return m.group(1)


def columns_of(create_body):
    return re.findall(r"`(\w+)` (?:bigint|varchar|text|tinyint|int)", create_body)


class Registration(unittest.TestCase):
    def test_value_registered_through_the_generator(self):
        gen = GEN.read_text()
        self.assertIn('"manual saved string"', gen)
        self.assertIn("STORE_KEYS", gen)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, str(GEN), "--check"], capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_registry_check_is_clean(self):
        proc = subprocess.run(
            [sys.executable, str(ROOT / "forever/tools/slice_registry_check.py")],
            capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr)

    def test_creator_is_the_full_builds_own(self):
        full = FULL_VALUE_CTX.read_text()
        self.assertIn('creators["manual saved string"]', full)
        generated = CTX.read_text()
        self.assertIn('creators["manual saved string"] = [](PlayerbotAI* ai) { return new StringManualSetSavedValue(ai); };', generated)
        # The "following party" value (quest travel gates answer for a free bot's own party, merged in #256)
        # raised the registered value count from 269 to 270; BG2's "enemy flag carrier" and "team flag carrier" (row 4) took it to 272;
        # the pool gear top-up and the emote and mount animation rows took it to 274.
        counts = re.findall(r"ValueContext \((\d+) of its keys\)", generated)
        self.assertEqual(len(counts), 1, "the generated value-context comment is missing")
        k_values = re.search(r"kValues = (\d+)", generated)
        self.assertIsNotNone(k_values, "the generated value count is missing")
        self.assertEqual(counts[0], k_values.group(1), "the value-context comment and kValues disagree")


class StoreWiring(unittest.TestCase):
    def test_load_save_reset_invalidate_defined_in_slice_store(self):
        store = code_only(STORE_CPP.read_text())
        self.assertIn("void PlayerbotDbStore::Load(PlayerbotAI* ai, std::string preset)", store)
        self.assertIn("void PlayerbotDbStore::Save(PlayerbotAI* ai, std::string preset)", store)
        self.assertIn("void PlayerbotDbStore::Reset(PlayerbotAI* ai, std::string preset)", store)
        self.assertIn("void PlayerbotDbStore::InvalidateStrategySnapshots(PlayerbotAI* ai)", store)
        stubs = code_only(STUBS.read_text())
        self.assertNotIn("PlayerbotDbStore::Load", stubs)
        self.assertNotIn("PlayerbotDbStore::Save", stubs)
        self.assertNotIn("PlayerbotDbStore::Reset", stubs)
        self.assertNotIn("PlayerbotDbStore::InvalidateStrategySnapshots", stubs)

    def test_strategy_lists_saved_and_loaded(self):
        pure = code_only(STORE_H.read_text())
        self.assertIn("BuildSaveAllStatements", pure)
        self.assertIn("BuildInvalidateStrategySnapshotsSql", pure)
        self.assertIn("FormatStrategies", pure)
        store = code_only(STORE_CPP.read_text())
        for key in ('"co"', '"nc"', '"dead"', '"react"'):
            self.assertIn(key, store)
        self.assertIn("ClearStrategies", store)
        self.assertIn("ReleasePullHold", store)

    def test_no_direct_database_call_in_the_store(self):
        # neither the store TU nor the pure header it builds its statements from touches a database pool
        for path in (STORE_CPP, STORE_H):
            text = code_only(path.read_text())
            direct = re.findall(
                r"\b(?:World|Character|Login|Hotfix)Database\s*(?:\.|->)\s*"
                r"(?:Query|PQuery|AsyncQuery|AsyncPQuery|Execute|PExecute|DirectExecute|DirectPExecute|GetPreparedStatement)\b",
                text)
            self.assertEqual(direct, [], path.name)
        store = code_only(STORE_CPP.read_text())
        # every statement runs after the schema probe, through the characters-DB guard
        for func in ("LoadValues", "SaveValues", "LoadAll", "SaveAll", "ResetAll", "InvalidateStrategySnapshots",
                     "QueryValuesOnly", "DeleteAllValues", "QueryDistinctPresets"):
            m = re.search(r"(?:void|QueryResult) %s\(.*?\)\n\{(.*?)\n\}\n" % func, store, re.S)
            self.assertIsNotNone(m, func)
            body = m.group(1)
            self.assertIn("SchemaReady()", body, func)
            self.assertLess(body.index("SchemaReady()"), body.index("GuardedCompat("), func)
        self.assertIn("CharacterTransactGuardedCompat(", store)

    def test_teardown_saves_by_stored_guid_and_create_preserves_loaded_movement(self):
        engine = code_only(ENGINE.read_text())
        dispose = engine[engine.index("void Dispose(Brain* brain)"):engine.index("delete brain;", engine.index("void Dispose(Brain* brain)"))]
        self.assertIn("aistore::SaveValues(brain->ai, brain->guidLow, std::string())", dispose)
        self.assertLess(dispose.index("aistore::SaveValues"), dispose.index("RemoveAI"))
        self.assertIn("if (!pai->HasActiveMovementStrategy())", engine)

    def test_strategy_autosave_and_invalidation_wired(self):
        compat = (ROOT / "forever/src/compat/TbCompat_Unported.h").read_text()
        m = re.search(r"inline bool DbStoreBeginCompat\(.*?\{(.*?)\}", code_only(compat), re.S)
        self.assertIsNotNone(m)
        self.assertIn("return true", m.group(1))
        tbcc = code_only((ROOT / "forever/src/host/TbCc.cpp").read_text())
        self.assertIn("sPlayerbotDbStore.Save(ai)", tbcc)
        orders = code_only((ROOT / "forever/src/host/TbOrders.cpp").read_text())
        self.assertIn("sPlayerbotDbStore.InvalidateStrategySnapshots(ai)", orders)
        self.assertIn("ai::slice::aistore::InvalidateStrategySnapshots", orders)

    def test_reset_actions_compiled_and_registered(self):
        slice_list = (ROOT / "forever/SLICE-W4.txt").read_text()
        self.assertIn("ai/playerbot/strategy/actions/ResetAiAction.cpp", slice_list)
        ctx = CTX.read_text()
        for key in ('"reset ai"', '"reset strats"', '"reset values"', '"reset ai soft"', '"de"', '"react"', '"all"'):
            self.assertIn(key, ctx)


class SqlDelivery(unittest.TestCase):
    def test_migration_shape(self):
        name = SQL.name
        self.assertRegex(name, r"^\d{4}_\d{2}_\d{2}_\d{2}_[a-z0-9_]+\.sql$")
        raw = SQL.read_bytes()
        self.assertTrue(raw.endswith(b";\n"))
        self.assertIn(b"CREATE TABLE IF NOT EXISTS `ai_playerbot_db_store`", raw)
        self.assertIn(b"no ALTER", raw)  # the file's own idempotence note
        self.assertEqual(columns_of(create_of(SQL.read_text())), CREATE_COLUMNS)

    def test_module_create_names_the_same_columns(self):
        self.assertEqual(columns_of(create_of(STORE_CPP.read_text())), CREATE_COLUMNS)

    def test_readme_documents_the_file(self):
        self.assertIn(SQL.name, README.read_text())


if __name__ == "__main__":
    unittest.main()
