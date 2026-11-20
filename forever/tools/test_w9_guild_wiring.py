#!/usr/bin/env python3
"""Wiring test for W9 (guilds, rank rights, deposit-only bank, mail send, guarded order store, .tb guildorder).
Run:
  python3 -m unittest forever/tools/test_w9_guild_wiring.py
  python3 forever/tools/test_w9_guild_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestW9GuildWiring(unittest.TestCase):
    def test_slice_w9_lists_pure_header_census(self):
        slice_w9 = read("forever/SLICE-W9.txt")
        self.assertIn("forever/src/slice/SliceGuildOrderHeaders.cpp", slice_w9)

    def test_sql_matches_store_columns(self):
        sql = read("forever/sql/characters/2026_11_20_01_tb_guild_orders.sql")
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_order`", sql)
        cols_h = read("runtime/GuildOrderStoreColumns.h")
        m = re.search(r"kSelect\[\]\s*=\s*\{(.*?)\};", cols_h, re.S)
        self.assertIsNotNone(m)
        cols = re.findall(r'"([a-z0-9_]+)"', m.group(1))
        self.assertEqual(len(cols), 44)
        for col in cols:
            self.assertIn(f"`{col}`", sql, f"missing column `{col}` in SQL")

    def test_host_guild_uses_core_handlers_deposit_only_no_mail_draft(self):
        guild_cpp = read("forever/src/host/TbHostGuild.cpp")
        for sym in (
            "HandlePetitionBuy",
            "HandleSignPetition",
            "HandleTurnInPetition",
            "HasAnyRankRight",
            "HandleGuildInviteByName",
            "HandleGuildAcceptInvite",
            "HandleGuildAssignRank",
            "HandleGuildBankDepositMoney",
            "HandleAutoGuildBankItem",
            "HandleSendMail",
            "MailSendConfirmed",
        ):
            self.assertIn(sym, guild_cpp, f"missing {sym} in TbHostGuild.cpp")
        self.assertNotIn("MailDraft", guild_cpp)
        self.assertNotIn("HandleGuildBankWithdrawMoney", guild_cpp)
        self.assertNotIn("HandleStoreGuildBankItem", guild_cpp)

    def test_order_store_guarded_and_rank_checked(self):
        orders_cpp = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders_cpp = orders_cpp.replace("CanEditOfficerNotes", "AlwaysAllowRank")
        self.assertIn("CharacterSchemaHasCompat", orders_cpp)
        self.assertIn("CharacterQueryGuardedCompat", orders_cpp)
        self.assertNotIn("CharacterDatabase.Query", orders_cpp)
        self.assertIn("CanEditOfficerNotes", orders_cpp)

    def test_command_and_state_wired(self):
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn('"guildorder"', scripts)
        off = read("forever/src/host/TbH3Off.cpp")
        self.assertIn("HandleGuildOrder", off)
        state = read("forever/src/host/TbPartyState.cpp")
        self.assertIn('"order"', state)

    def test_order_providers_and_w8_reserve_wired(self):
        orders_cpp = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders_cpp = orders_cpp.replace("HostGatherCraftProvider", "NullGatherCraftProvider")
        self.assertIn("HostGatherCraftProvider", orders_cpp)
        self.assertIn("HostAuctionBuyProvider", orders_cpp)
        self.assertIn("HostDeliveryProvider", orders_cpp)
        self.assertIn("FindNearestMailbox", orders_cpp)
        self.assertIn("TickBotOrder", orders_cpp)
        self.assertIn('"work"', orders_cpp)

        bot_mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn("TickBotOrder", bot_mgr)

        ah_policy = read("ai/playerbot/policy/ah/AhSellPolicy.h")
        self.assertIn("ShouldReserveTradeGood", ah_policy)
        self.assertNotIn("AhReserveTradeGoodsForW7", ah_policy)

        wl8 = read("forever/src/compat/TbCompat_WL8.h")
        self.assertIn("AhIsTradeGoodReservedCompat", wl8)
        self.assertIn("IsReagentForBotProfessions", wl8)
        self.assertIn("IsItemNeededByGuildOrder", wl8)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
