#!/usr/bin/env python3
"""Wiring test for the disenchant service (TBF #155).

Run:
  python3 forever/tools/test_guild_order_disenchant_wiring.py
  python3 forever/tools/test_guild_order_disenchant_wiring.py --mutate   # must FAIL

The pieces pinned here: the table file matches the store's column list, the
store is guarded and reads the full item guid, the service uses the core's
cast/loot/mail handlers (no MailDraft), and each hook is in place.
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
STORE = "forever/src/host/TbHostGuildOrderDisenchantStore.cpp"
SERVICE = "forever/src/host/TbHostGuildOrderDisenchant.cpp"
SQL = "forever/sql/characters/2026_11_23_01_tb_guild_disenchant.sql"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def store_text():
    text = read(STORE)
    if MUTATE:
        # The failing case first: a store that skips the schema probe would
        # let the core abort on a missing table.
        text = text.replace("CharacterSchemaHasCompat", "SkipSchemaProbe")
        text = text.replace("fields[2].GetUInt64()", "fields[2].GetUInt32()")
    return text


class TestDisenchantWiring(unittest.TestCase):
    def test_sql_matches_store_columns(self):
        sql = read(SQL)
        store = read(STORE)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_disenchant`", sql)
        columns = re.search(r"const columns = \{(.*?)\};", store, re.S)
        self.assertIsNotNone(columns)
        for column in re.findall(r'"([a-z_]+)"', columns.group(1)):
            self.assertIn(f"`{column}`", sql, f"missing column `{column}` in {SQL}")
        select = re.search(r"kSelect\s*=\s*(.*?);", store, re.S)
        self.assertIsNotNone(select)
        for column in re.findall(r"`([a-z_]+)`", select.group(1)):
            self.assertIn(f"`{column}`", sql, f"missing SELECT column `{column}` in {SQL}")
        # ForeverCore item instance guids are bigint (characters.item_instance.guid);
        # the TB 32-bit column would truncate them.
        self.assertIn("`item_guid` bigint unsigned NOT NULL", sql)
        readme = read("forever/sql/README.md")
        self.assertIn("2026_11_23_01_tb_guild_disenchant.sql", readme)
        contract = read("runtime/GuildOrderContract.h")
        self.assertIn("uint64_t itemGuidLow", contract)

    def test_store_is_guarded_and_reads_the_full_guid(self):
        store = store_text()
        self.assertIn("CharacterSchemaHasCompat", store)
        self.assertIn("CharacterQueryGuardedCompat", store)
        self.assertIn("CharacterExecuteGuardedCompat", store)
        self.assertIn("WorldQueryNamesCompat", store)
        self.assertNotRegex(store, r"\bCharacterDatabase\s*(?:\.|->)\s*(?:Query|PQuery|Execute|PExecute|DirectExecute)\b")
        self.assertIn("fields[2].GetUInt64()", store)
        self.assertIn("\"created_at\"", store)
        # every write is confirmed by a read-back before memory changes
        self.assertIn("not stored", store)
        self.assertIn("not deleted", store)

    def test_service_uses_core_handlers(self):
        service = read(SERVICE)
        for symbol in (
            "HandleCastSpellOpcode",
            "HandleAutostoreLootItemOpcode",
            "DoLootRelease",
            "tbhost::guild::SendMail",
            "GetDisenchantLootId",
            "GetDisenchantSkillRequired",
            "DisenchantIntakeHasRoom",
            "DisenchantCastReady",
            "IsDisenchantServiceSubject",
        ):
            self.assertIn(symbol, service, f"missing {symbol} in {SERVICE}")
        self.assertIn("kSpellDisenchant = 13262", service)
        self.assertNotIn("MailDraft", service)
        self.assertIn("Target.Flags = TARGET_FLAG_ITEM", service)
        self.assertIn("packet.Cast.Target.Item", service)

    def test_hooks_wired(self):
        bot_mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn("TickDisenchantService", bot_mgr)
        self.assertIn("HasActiveDisenchantErrand", bot_mgr)
        self.assertIn("LoadDisenchantQueue", read("forever/src/TbScripts.cpp"))
        self.assertIn("DisenchantForgetBot", read("forever/src/host/TbHostGuildOrders.cpp"))
        ownership = read("forever/src/host/TbHostOwnership.cpp")
        self.assertIn("return tbhost::guild_orders::DisenchantQueued(botGuidLow);", ownership)
        mail_action = read("ai/playerbot/strategy/actions/MailAction.cpp")
        if MUTATE:
            mail_action = mail_action.replace("IsDisenchantServiceMail", "NotTheService")
        self.assertIn("IsDisenchantServiceMail(bot, mail)", mail_action)
        self.assertGreaterEqual(mail_action.count("IsDisenchantServiceMail(bot, mail)"), 2,
                                "the broad take and the order take both leave service mail alone")

    def test_item_usage_sheet_sees_the_rows(self):
        # The compiled ItemUsageValue/ItemVisitors gate shields queued items
        # and cast results; the legacy name now answers from the host store.
        self.assertIn("DisenchantRowsHoldItem(", read("ai/playerbot/strategy/values/ItemUsageValue.cpp"))
        self.assertIn("DisenchantQueued", read("forever/src/host/TbHostOwnership.cpp"))


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
