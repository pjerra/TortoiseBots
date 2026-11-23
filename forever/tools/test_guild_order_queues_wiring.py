#!/usr/bin/env python3
"""Wiring test for the guild order queues (TBF #155): the relist ledger and
the refund of unspent guild gold.

Run:
  python3 forever/tools/test_guild_order_queues_wiring.py
  python3 forever/tools/test_guild_order_queues_wiring.py --mutate   # must FAIL

Pinned here: the table file matches the store's column list, the store is
guarded and confirms every write by reading it back, the compiled legacy mail
read reaches the real store instead of the unported stubs, the banker pass is
ticked beside the order tick and pays only through the deposit-only guild
seam, and a finished row holding gold parks instead of disappearing.
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
STORE = "forever/src/host/TbHostGuildOrderQueuesStore.cpp"
SERVICE = "forever/src/host/TbHostGuildOrderQueues.cpp"
HEADER = "forever/src/host/TbHostGuildOrderQueues.h"
SQL = "forever/sql/characters/2026_11_24_01_tb_guild_relist.sql"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def store_text():
    text = read(STORE)
    if MUTATE:
        # The failing case first: a store that skips the schema probe would
        # let the core abort on a missing table.
        text = text.replace("CharacterSchemaHasCompat", "SkipSchemaProbe")
    return text


def service_text():
    text = read(SERVICE)
    if MUTATE:
        text = text.replace("RelistBankTrip", "BypassLedger")
    return text


class TestGuildOrderQueuesWiring(unittest.TestCase):
    def test_sql_matches_store_columns(self):
        sql = read(SQL)
        store = read(STORE)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist`", sql)
        columns = re.search(r"const columns = \{(.*?)\};", store, re.S)
        self.assertIsNotNone(columns)
        for column in re.findall(r'"([a-z_]+)"', columns.group(1)):
            self.assertIn(f"`{column}`", sql, f"missing column `{column}` in {SQL}")
        select = re.search(r"kSelect = (.*?);", store, re.S)
        self.assertIsNotNone(select)
        for column in re.findall(r"`([a-z_]+)`", select.group(1)):
            self.assertIn(f"`{column}`", sql, f"missing SELECT column `{column}` in {SQL}")
        self.assertIn("`state` tinyint unsigned", sql)
        self.assertIn("PRIMARY KEY (`bot_guid`, `seq`)", sql)
        readme = read("forever/sql/README.md")
        self.assertIn("2026_11_24_01_tb_guild_relist.sql", readme)
        self.assertIn("guildGoldHeld", read("runtime/GuildOrderContract.h"))

    def test_store_is_guarded_and_confirms_writes(self):
        store = store_text()
        self.assertIn("CharacterSchemaHasCompat", store)
        self.assertIn("CharacterQueryGuardedCompat", store)
        self.assertIn("CharacterExecuteGuardedCompat", store)
        self.assertIn("WorldQueryNamesCompat", store)
        self.assertNotRegex(store, r"\bCharacterDatabase\s*(?:\.|->)\s*(?:Query|PQuery|Execute|PExecute|DirectExecute)\b")
        # every write is confirmed by a read-back before memory changes
        self.assertIn("SameRow", store)
        self.assertIn("was not stored", store)
        self.assertIn("was not deleted", store)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist`", store)
        self.assertIn("2026_11_24_01_tb_guild_relist.sql", store)

    def test_legacy_names_reach_the_real_store(self):
        header = read(HEADER)
        for symbol in ("LoadGuildOrderQueues", "TickGuildOrderQueues",
                       "ForgetGuildOrderQueueSession", "RelistDeposits",
                       "SaveRelistRow", "DeleteRelistRow", "RelistSweepDue",
                       "ExpireRelistRows", "StashTakenMailSender"):
            self.assertIn(symbol, header, f"missing {symbol} in {HEADER}")
        store = read(STORE)
        for symbol in ("RelistEntries", "NoteRelistClaimed", "NoteRelistReceived",
                       "NoteTakenMailSender", "TakeTakenMailSenders"):
            self.assertIn(symbol, store, f"missing legacy {symbol} in {STORE}")
        ownership = read("forever/src/host/TbHostOwnership.cpp")
        for gone in ("RelistEntries", "NoteRelistClaimed", "NoteRelistReceived",
                     "NoteTakenMailSender"):
            self.assertNotIn(gone, ownership, f"{gone} must leave {ownership}")
        mail = read("ai/playerbot/strategy/actions/MailAction.cpp")
        self.assertIn("RelistEntries(bot->GetGUIDLow())", mail)
        self.assertIn("NoteRelistClaimed(bot->GetGUIDLow(), entry.seq,", mail)

    def test_startup_tick_and_logout_hooks(self):
        self.assertIn("LoadGuildOrderQueues()", read("forever/src/TbScripts.cpp"))
        self.assertIn("TickGuildOrderQueues(me)", read("forever/src/TbBotMgr.cpp"))
        self.assertIn("ForgetGuildOrderQueueSession(botGuidLow)",
                      read("forever/src/host/TbHostGuildOrders.cpp"))

    def test_banker_pass_pays_through_the_deposit_only_seam(self):
        service = service_text()
        for symbol in ("RelistBankTrip", "GuildGoldRefundAmount",
                       "GuildGoldRefundRemaining", "tbhost::guild::DepositMoney",
                       "tbhost::guild::DepositItem", "RelistDeposits(botGuidLow)",
                       "ExpireRelistRows", "RelistSweepDue"):
            self.assertIn(symbol, service, f"missing {symbol} in {SERVICE}")
        self.assertNotIn("WithdrawMoney", service)
        self.assertNotIn("WithdrawItem", service)
        guild = read("forever/src/host/TbHostGuild.h")
        self.assertIn("Guild bank DEPOSIT only", guild)

    def test_finished_row_holding_gold_parks(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        self.assertIn("IsFinished(record.state) && record.guildGoldHeld > 0", orders)
        self.assertIn("record.cancelled = true;", orders)
        reassign = read("forever/src/host/TbHostGuildOrderReassign.cpp")
        self.assertIn("stub.cancelled = true;", reassign)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
