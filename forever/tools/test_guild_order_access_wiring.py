#!/usr/bin/env python3
"""Wiring test for the guild-order access control (TBF #155): the bot rank table, the access report,
the order-permission gate, and reassign.

Run:
  python3 -m unittest forever/tools/test_guild_order_access_wiring.py
  python3 forever/tools/test_guild_order_access_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestGuildOrderAccessWiring(unittest.TestCase):
    def test_new_host_files_are_adopted_by_the_slice_glob(self):
        # CMakeLists.txt compiles forever/src/host/TbHost*.cpp only in the slice build; both files
        # must carry that prefix or they never build.
        for rel in (
            "forever/src/host/TbHostGuildOrderAccess.h",
            "forever/src/host/TbHostGuildOrderAccess.cpp",
            "forever/src/host/TbHostGuildOrderAccessPure.h",
            "forever/src/host/TbHostGuildOrderReassign.h",
            "forever/src/host/TbHostGuildOrderReassign.cpp",
        ):
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)
        cmake = read("CMakeLists.txt")
        self.assertIn("host/TbHost*.cpp", cmake)

    def test_sql_matches_the_module_create_and_the_readme(self):
        sql = read("forever/sql/characters/2026_11_23_01_tb_guild_bot_rank.sql")
        access = read("forever/src/host/TbHostGuildOrderAccess.cpp")
        for col in ("guild_id", "bot_rank", "updated_at"):
            self.assertIn(f"`{col}`", sql, col)
            self.assertIn(f"`{col}`", access, col)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank`", sql)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank`", access)
        readme = read("forever/sql/README.md")
        self.assertIn("2026_11_23_01_tb_guild_bot_rank.sql", readme)
        self.assertIn("tortoise_bots_guild_bot_rank", readme)

    def test_every_characters_statement_goes_through_the_guard(self):
        access = read("forever/src/host/TbHostGuildOrderAccess.cpp")
        if MUTATE:
            access = access.replace("QueryGuarded(sql, \"guild_rank\")", "CharacterDatabase.Query(sql)")
        self.assertIn("CharacterSchemaHasCompat", access)
        self.assertIn("CharacterQueryGuardedCompat", access)
        self.assertIn("CharacterExecuteGuardedCompat", access)
        self.assertNotIn("CharacterDatabase.Query", access)
        self.assertNotIn("CharacterDatabase.PQuery", access)
        self.assertNotIn("CharacterDatabase.DirectExecute", access)
        for table in ("guild_rank", "guild_bank_tab", "guild_bank_right"):
            self.assertIn(f'"{table}"', access, table)

    def test_rank_store_config_matches_the_shipped_template(self):
        access = read("forever/src/host/TbHostGuildOrderAccess.cpp")
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertIn('kConfigKey = "TortoiseBots.World.GuildOrderBotRank"', access)
        self.assertIn("GetBoolDefault(kConfigKey, true)", access)
        self.assertIn("TortoiseBots.World.GuildOrderBotRank = 1", conf)

    def test_startup_and_join_hooks_call_the_rank_rule(self):
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn("LoadBotRankAtStartup()", scripts)
        guild = read("forever/src/host/TbHostGuild.cpp")
        self.assertIn("ApplyBotRankOnJoin(invitee", guild)

    def test_commands_are_dispatched(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace('if (sub == "access")', 'if (sub == "noaccess")')
        for verb in ("access", "rank", "reassign"):
            self.assertIn(f'if (sub == "{verb}")', orders, verb)
        self.assertIn("guild_order_access::BuildAccessReport", orders)
        self.assertIn("guild_order_access::SetBotRank", orders)
        self.assertIn("guild_orders::ReassignOrder", orders)
        # Place/cancel/work all go through the one permission gate.
        self.assertGreaterEqual(orders.count("guild_order_access::MayGiveOrders"), 4)
        self.assertNotIn("CanEditOfficerNotes(guildId, sender->GetGUID())", orders)

    def test_reassign_moves_the_row_and_the_children(self):
        reassign = read("forever/src/host/TbHostGuildOrderReassign.cpp")
        if MUTATE:
            reassign = reassign.replace("if (!GiveOrder(moved))", "if (false)")
        self.assertIn("guild_order_access::CheckReassign", reassign)
        self.assertIn("GiveOrder(stub)", reassign)
        self.assertIn("GiveOrder(moved)", reassign)
        self.assertIn("child.parentBotGuidLow = toBotGuidLow", reassign)
        self.assertIn("SetOrderWorking(fromBotGuidLow, false)", reassign)
        self.assertIn("SetOrderWorking(toBotGuidLow, wasWorking)", reassign)
        # The goal and the progress already handed over survive; the bot's own work state does not.
        self.assertIn("moved.reportedHave = original.delivered;", reassign)
        self.assertIn("ResetWorkState(moved)", reassign)
        for field in ("farmZoneId", "supplyStep", "workedTotalSec"):
            self.assertIn(f"record.{field} =", reassign, field)
        self.assertIn("record.needItems.clear();", reassign)

    def test_the_policy_test_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="guild-order-access-") as tmp:
            exe = os.path.join(tmp, "test_guild_order_access_policy")
            subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", ROOT, "-I",
                            os.path.join(ROOT, "forever", "src"),
                            os.path.join(ROOT, "forever/tools/test_guild_order_access_policy.cpp"),
                            "-o", exe], check=True, capture_output=True, text=True)
            out = subprocess.run([exe], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)


if __name__ == "__main__":
    MUTATE = "--mutate" in sys.argv
    if MUTATE:
        sys.argv = [a for a in sys.argv if a != "--mutate"]
    unittest.main()
