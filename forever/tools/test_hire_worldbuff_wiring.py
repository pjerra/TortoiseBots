#!/usr/bin/env python3
"""Wiring pins for the world-buff page at the capital recruiters (TurtleBotsForever issue #298, package P5).

The pure rules (ids, faction catalogue, credit map, raid rule) are exercised by
forever/tools/test_hire_worldbuff_policy.cpp, which is built and run here. The pins around it cover what a source test
can: the migration ships every quest the page can sell and every aura script name the code registers, the fee rows
match the config defaults, the six capital recruiters take and turn in the quests, and the contract functions are
defined for both builds (the default build must answer "not visible", not fail to link).

Run: python3 -m unittest forever/tools/test_hire_worldbuff_wiring.py
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = "forever/src/host/hire/TbHireWorldBuffsPure.h"
PURE_TEST = "forever/tools/test_hire_worldbuff_policy.cpp"
HOST = "forever/src/host/hire/TbHireWorldBuffs.cpp"
HOST_H = "forever/src/host/hire/TbHireWorldBuffs.h"
SCRIPTS = "forever/src/TbScripts.cpp"
SQL = "forever/sql/world/2026_11_25_20_tortoisebots_hire_world_buffs.sql"

SOURCES = (PURE, PURE_TEST, HOST, HOST_H, SCRIPTS, SQL)


def read(rel):
    return (ROOT / rel).read_text()


class PurePolicy(unittest.TestCase):
    def test_pure_policy_test_builds_and_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        exe = pathlib.Path(tempfile.mkdtemp()) / "test_hire_worldbuff_policy"
        build = subprocess.run(
            [gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT), str(ROOT / PURE_TEST), "-o", str(exe)],
            capture_output=True, text=True, timeout=300)
        self.assertEqual(build.returncode, 0, "pure test does not compile:\n" + build.stderr[-3000:])
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, "pure test failed:\n" + run.stdout + run.stderr)
        self.assertIn("0 failed", run.stdout)


class HostContract(unittest.TestCase):
    def test_both_builds_define_the_contract(self):
        host = read(HOST)
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", host)
        for definition in (r"^bool WorldBuffsVisible\(", r"^void ShowWorldBuffMenu\(", r"^bool OnWorldBuffSelect\("):
            self.assertEqual(len(re.findall(definition, host, re.MULTILINE)), 2,
                             f"{definition} must be defined in the engine build and stubbed in the default one")

    def test_scripts_are_registered(self):
        self.assertIn("TortoiseBots::hire::worldbuffs::AddScripts();", read(SCRIPTS))
        host = read(HOST)
        self.assertIn('SpellScriptLoader("tortoisebots_hire_worldbuff_aura")', host)
        self.assertIn("new WorldBuffAuraLoader();", host)
        self.assertIn("new WorldBuffHooks();", host)
        self.assertIn("new WorldBuffFees();", host)

    def test_purchase_is_an_aura_from_the_recruiter(self):
        host = read(HOST)
        # Caster identity is the whole unlock filter: AddAura(spell, target) on the recruiter, never a cast.
        self.assertIn("creature->AddAura(spellId, target)", host)
        self.assertNotIn("CastSpell(", host)
        self.assertIn("ObjectAccessor::FindPlayer(slot.guid)", host)
        self.assertIn("IsWithinDistInMap(member, 40.0f)", host)
        self.assertIn("RemoveAurasDueToSpell(stripId)", host)
        # Refund when no target received the aura.
        self.assertIn("player->ModifyMoney(static_cast<int64>(total));", host)

    def test_raid_rule_and_fee_hook(self):
        host = read(HOST)
        self.assertIn("ShouldStripOnRaidEntry(", host)
        self.assertIn("player->GetMap()->IsRaid()", host)
        self.assertIn("SetClassicMoney(-static_cast<int32>(fee), 0)", host)


class Migration(unittest.TestCase):
    def test_every_purchase_has_its_quest(self):
        sql = read(SQL)
        quests = set(int(value) for value in re.findall(r"^\((1000\d\d),", sql, re.MULTILINE))
        # Seven purchases per faction, from Rally (100000/100001) to Silithyst (100012/100013).
        self.assertEqual(quests, set(range(100000, 100014)))

    def test_fees_match_the_config_defaults(self):
        sql = read(SQL)
        fees = {int(quest): int(money) for quest, money in
                re.findall(r"\((1000\d\d), (-?\d+), 0\)", sql)}
        self.assertEqual(len(fees), 14)
        for quest in range(100000, 100006):
            self.assertEqual(fees[quest], -2000000)  # Rally, Rend, Zandalar: 200 gold
        for quest in range(100006, 100014):
            self.assertEqual(fees[quest], -1000000)  # the rest: 100 gold

    def test_credit_creatures_and_bosses(self):
        sql = read(SQL)
        for credit in ("1000070", "1000071", "10429", "14834"):
            self.assertIn(f", {credit}, ", sql)
        self.assertNotIn("10339", sql)  # Gyth dies with Rend and must not credit

    def test_every_aura_spell_is_bound_to_the_registered_script(self):
        sql = read(SQL)
        for spell in (22817, 22818, 22820, 23735, 23736, 23737, 23738, 23766, 23767, 23768, 23769, 15366):
            self.assertIn(f"({spell}, 'tortoisebots_hire_worldbuff_aura')", sql)
        names = set(re.findall(r"'(tortoisebots_hire_worldbuff_aura)'", sql))
        self.assertEqual(names, {"tortoisebots_hire_worldbuff_aura"})

    def test_six_capitals_take_and_turn_in(self):
        sql = read(SQL)
        for entry in (1000006, 1000012, 1000017, 1000018, 1000019, 1000025):
            self.assertIn(f"SELECT {entry}", sql)
        self.assertEqual(sql.count("SELECT 1000006"), 2)  # starter + ender
        self.assertIn("creature_queststarter", sql)
        self.assertIn("creature_questender", sql)

    def test_all_quests_are_level_sixty(self):
        sql = read(SQL)
        self.assertEqual(len(re.findall(r"\(1000\d\d, 60, 60, 0\)", sql)), 14)


class Consistency(unittest.TestCase):
    def test_senders_live_in_the_contract_range(self):
        pure = read(PURE)
        for name, sender in (("kSenderRoot", 505), ("kSenderBuy", 506), ("kSenderSayge", 507),
                             ("kSenderBack", 508), ("kSenderConfirm", 509)):
            self.assertIn(f"constexpr uint32_t {name} = {sender};", pure)

    def test_sql_and_code_agree_on_the_aura_script_name(self):
        host = read(HOST)
        sql = read(SQL)
        name = re.search(r'SpellScriptLoader\("([a-z_]+)"\)', host).group(1)
        self.assertIn(name, sql)


if __name__ == "__main__":
    unittest.main()
