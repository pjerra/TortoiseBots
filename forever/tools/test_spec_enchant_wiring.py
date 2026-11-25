#!/usr/bin/env python3
"""Wiring pins for the level-60 enchant choice: the Forever table, its reader, the startup load and the two
users (the gear seed's lookup and the free-bot enchanter's skip). It also pins that nothing of it sits on the
Turtle side, where the enchant picker is a different build (`ai/playerbot/RandomItemMgr.cpp` is not compiled
on ForeverCore; its members there are stubs).

Run:
  python3 forever/tools/test_spec_enchant_wiring.py
  python3 forever/tools/test_spec_enchant_wiring.py --mutate   # must FAIL
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
SQL = "forever/sql/world/2026_11_25_00_tortoisebots_spec_enchant.sql"
READER = "forever/src/host/TbSpecEnchant.cpp"
READER_HDR = "forever/src/host/TbSpecEnchant.h"
RULES = "forever/src/host/TbSpecEnchantPure.h"
STARTUP = "forever/src/TbScripts.cpp"
UPKEEP = "forever/src/host/TbHostProfessionUpkeep.cpp"
UPKEEP_HDR = "forever/src/host/TbHostProfessionUpkeep.h"
UPKEEP_WEIGHT = "forever/src/host/TbHostProfessionUpkeepWeight.cpp"
GEAR_SEED = "forever/src/host/gear/TbGearGen.cpp"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def startup_text():
    text = read(STARTUP)
    if MUTATE:
        text = text.replace("TortoiseBots::enchant::LoadSpecEnchants();", "// the table is never loaded")
    return text


class TestSpecEnchantWiring(unittest.TestCase):
    def test_table_and_reader_live_on_the_forever_side(self):
        for rel in (SQL, READER, READER_HDR, RULES):
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)
        self.assertIn("`ai_playerbot_spec_enchant`", read(SQL))
        # The reader is a plain host file (the CMake glob compiles it with and without the slice): only
        # host/TbHost*.cpp is slice-only, so the reader must not carry that prefix.
        self.assertFalse(os.path.basename(READER).startswith("TbHost"))
        cmake = read("CMakeLists.txt")
        self.assertIn("file(GLOB_RECURSE PRIVATE_SOURCES", cmake)
        self.assertIn("/forever/src/(slice/[^/]*|host/TbHost[^/]*)", cmake)
        for slice_list in ("forever/SLICE-W4.txt", "forever/SLICE1.txt"):
            self.assertNotIn("TbSpecEnchant.cpp", read(slice_list))
        readme = read("forever/sql/README.md")
        self.assertIn(os.path.basename(SQL), readme)
        self.assertIn("`ai_playerbot_spec_enchant`", readme)

    def test_turtle_side_is_untouched(self):
        for rel in ("ai/playerbot/RandomItemMgr.h", "ai/playerbot/RandomItemMgr.cpp", "runtime/HireProvisionService.cpp"):
            text = read(rel)
            self.assertNotIn("SpecEnchant", text, rel)
            self.assertNotIn("spec_enchant", text, rel)
        for name in os.listdir(os.path.join(ROOT, "data", "sql", "world")):
            if name.endswith(".sql"):
                self.assertNotIn("ai_playerbot_spec_enchant", read("data/sql/world/" + name),
                    "the Turtle world folder is not where the Forever table belongs")

    def test_startup_loads_the_table(self):
        text = startup_text()
        self.assertIn('#include "host/TbSpecEnchant.h"', text)
        self.assertIn("TortoiseBots::enchant::LoadSpecEnchants();", text)
        self.assertIn("void OnWorldInitialized() override", text)

    def test_reader_reads_once_through_the_schema_guard(self):
        reader = read(READER)
        self.assertIn("WorldQueryCompat(", reader)
        self.assertIn("FROM `ai_playerbot_spec_enchant`", reader)
        self.assertEqual(reader.count("FROM `ai_playerbot_spec_enchant`"), 1, "one read, at startup")
        self.assertIn("std::lock_guard", reader)
        # The rows are keyed by the class and the canonical spec name, the same pair the caller passes.
        self.assertIn("spec_name", reader)
        self.assertIn("class_id", reader)

    def test_the_rules_are_pure_and_tested(self):
        rules = read(RULES)
        self.assertNotIn("#include", rules.replace('#include <cstdint>', ""))
        self.assertIn("DecideSpecEnchant", rules)
        self.assertIn("kSpecEnchantLevel = 60", rules)
        self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/tools/test_spec_enchant_rules.cpp")))
        self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/tools/test_spec_enchant_data.py")))

    def test_upkeep_leaves_the_governed_slots_alone(self):
        host = read(UPKEEP)
        self.assertIn("specGoverned[slot] = SpecEnchantGoverns(bot, item)", host)
        self.assertIn("|| specGoverned[slot]", host)
        self.assertIn("SpecEnchantGoverns", read(UPKEEP_HDR))
        weight = read(UPKEEP_WEIGHT)
        self.assertIn("TortoiseBots::enchant::SpecEnchantFor", weight)
        self.assertIn("sRandomItemMgr.GetSpecName", weight)
        self.assertNotIn("sRandomItemMgr.SpecEnchantGoverns", weight)

    def test_the_lookup_names_its_spec_argument(self):
        # The gear seed and the upkeep both pass the canonical weight-scale name, the string
        # GearSetOutcome::specName holds, so the header says so and the table is keyed by it.
        self.assertIn("GearSetOutcome::specName", read(READER_HDR))
        self.assertIn("SpecEnchantFor", read(READER_HDR))

    def test_the_enchant_must_fit_the_worn_item(self):
        # The off-hand slot can hold a shield or a 1H weapon (GearSetPolicy), while the row's spell is
        # a shield spike for arms/prot and a weapon enchant for fury: the seed asks the core before stamping.
        seed = read(GEAR_SEED)
        body = seed[seed.index("static void ApplySpecEnchant("):]
        body = body[:body.index("GearSetOutcome SeedGearForTarget(")]
        self.assertIn("IsFitToSpellRequirements(spell)", body)
        self.assertLess(body.index("IsFitToSpellRequirements(spell)"), body.index("SetEnchantment("))


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
