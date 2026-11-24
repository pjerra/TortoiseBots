#!/usr/bin/env python3
"""Wiring pins for piece P135-C (TBF #135): named AI presets and custom strategies.

Run:
  python3 forever/tools/test_aipreset_wiring.py
  python3 forever/tools/test_aipreset_wiring.py --mutate   # must FAIL
"""
import pathlib
import re
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
GEN = ROOT / "forever/tools/slice_gen_context.py"
CTX = ROOT / "forever/src/slice/SliceAiObjectContext.cpp"
COMPAT = ROOT / "forever/src/compat/TbCompat_Unported.h"
CS_CPP = ROOT / "forever/src/slice/SliceCustomStrategy.cpp"
CS_H = ROOT / "forever/src/slice/SliceCustomStrategyPure.h"
AI_CPP = ROOT / "forever/src/host/TbAddonAi.cpp"
SCRIPTS = ROOT / "forever/src/TbScripts.cpp"
SQL = ROOT / "forever/sql/characters/2026_11_22_01_tb_custom_strategy.sql"
README = ROOT / "forever/sql/README.md"

MUTATE = False


def code_only(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


class TestAiPresetWiring(unittest.TestCase):
    def test_custom_strategy_and_cs_registered(self):
        gen = GEN.read_text()
        self.assertIn("PRESET_KEYS", gen)
        self.assertIn('"custom"', gen)
        self.assertIn('"cs"', gen)
        ctx = CTX.read_text()
        self.assertIn('creators["custom"] = [](PlayerbotAI* ai) { return new CustomStrategy(ai); };', ctx)
        self.assertIn('creators["cs"] = [](PlayerbotAI* ai) { return new CustomStrategyEditAction(ai); };', ctx)

    def test_load_custom_strategy_lines_compat_wired(self):
        compat = code_only(COMPAT.read_text())
        self.assertIn("void LoadCustomStrategyLinesCompat(", compat)
        self.assertNotIn("tbcompat::Feature::CustomStrategyStore", compat)
        cs = code_only(CS_CPP.read_text())
        self.assertIn("void LoadCustomStrategyLinesCompat(", cs)
        self.assertIn("CustomSchemaReady()", cs)

    def test_commands_and_addon_verb_registered(self):
        scripts = code_only(SCRIPTS.read_text())
        if MUTATE:
            scripts = scripts.replace("TortoiseBots::aipreset::RegisterAddonVerbsAi();", "")
        self.assertIn("TortoiseBots::aipreset::HandleAiCommand", scripts)
        self.assertIn("TortoiseBots::aipreset::HandleCsCommand", scripts)
        self.assertIn("TortoiseBots::aipreset::RegisterAddonVerbsAi();", scripts)

        ai_cpp = code_only(AI_CPP.read_text())
        self.assertIn('addon::RegisterVerb("ai", &HandleAiVerb)', ai_cpp)
        self.assertIn('addon::AdvertiseCap("ai")', ai_cpp)
        self.assertIn("player::Tier::Owner", ai_cpp)
        # PR #238 (fix/slice-off-build) made the OFF stub answer engine-not-built itself instead of
        # calling h3::NotBuilt; the reply still says the engine is not built.
        self.assertIn("bool NotBuilt(ChatHandler* handler)", ai_cpp)
        self.assertIn("error code=engine-not-built", ai_cpp)
        self.assertIn("return NotBuilt(handler);", ai_cpp)

    def test_sql_and_no_direct_queries(self):
        sql = SQL.read_text()
        for col in ("id", "name", "idx", "owner", "action_line"):
            self.assertIn(f"`{col}`", sql)
        cs = CS_CPP.read_text()
        for col in ("id", "name", "idx", "owner", "action_line"):
            self.assertIn(f"`{col}`", cs)
        self.assertIn("2026_11_22_01_tb_custom_strategy.sql", README.read_text())

        for path in (CS_CPP, CS_H, AI_CPP):
            text = code_only(path.read_text())
            direct = re.findall(
                r"\b(?:World|Character|Login|Hotfix)Database\s*(?:\.|->)\s*"
                r"(?:Query|PQuery|AsyncQuery|AsyncPQuery|Execute|PExecute|DirectExecute|DirectPExecute|GetPreparedStatement)\b",
                text,
            )
            self.assertEqual(direct, [], path.name)

    def test_generators_clean(self):
        proc = subprocess.run([sys.executable, str(GEN), "--check"], capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
