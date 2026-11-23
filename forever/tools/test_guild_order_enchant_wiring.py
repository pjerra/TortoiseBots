#!/usr/bin/env python3
"""Wiring test for the enchant-order host service (TBF #155).

Run:
  python3 forever/tools/test_guild_order_enchant_wiring.py
  python3 forever/tools/test_guild_order_enchant_wiring.py --mutate   # must FAIL

The pieces pinned here: the ForeverCore catalogue is real (no slice stub) and
reads DB2/sSpellMgr; the command surface reaches the service; the per-bot tick,
the logout forget and the addon trade exemption are wired; the trade effects
are the core's own handlers, and no guild bank deposit is reachable from the
enchant path.
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
INDEX = "forever/src/host/TbHostGuildOrderEnchantIndex.cpp"
SERVICE = "forever/src/host/TbHostGuildOrderEnchant.cpp"
HDR = "forever/src/host/TbHostGuildOrderEnchant.h"
ORDERS = "forever/src/host/TbHostGuildOrders.cpp"
READ = "forever/src/host/TbHostGuildOrderRead.cpp"
STUBS = "forever/src/slice/SliceLinkStubs.cpp"
BOTMGR = "forever/src/TbBotMgr.cpp"
TRADE = "forever/src/host/TbAddonTrade.cpp"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def service_text():
    text = read(SERVICE)
    if MUTATE:
        text = text.replace("mine->SetSpell(record.itemId);", "mine->GetSpell();")
    return text


class TestEnchantWiring(unittest.TestCase):
    def test_catalogue_is_real(self):
        index = read(INDEX)
        for symbol in (
            "BuildGuildOrderEnchantIndex",
            "GuildOrderEnchantsReady",
            "GuildOrderEnchants()",
            "GuildOrderEnchantReagents",
            "GuildOrderEnchantTools",
            "sDB2Manager.GetSkillLineAbilitiesBySkill",
            "sSpellMgr->GetSpellInfo",
            "SPELL_EFFECT_ENCHANT_ITEM",
            "SPELL_EFFECT_SKILL_STEP",
            "IsCatalogueEnchantRow",
            "EnchantSlotForMasks",
            "EnchantSortLess",
        ):
            self.assertIn(symbol, index, f"missing {symbol} in {INDEX}")
        self.assertNotIn("BuildGuildOrderEnchantIndex", read(STUBS),
                         "the slice stub must be gone once the catalogue is real")
        self.assertIn("BuildGuildOrderSourceIndex", read(STUBS))

    def test_command_surface(self):
        orders = read(ORDERS)
        self.assertIn('#include "host/TbHostGuildOrderEnchant.h"', orders)
        self.assertIn("HandleEnchantMeet(handler, words, sender)", orders)
        self.assertIn('Lower(words.w[2]) == "enchant"', orders)
        self.assertIn("HandleEnchantCreate(handler, words, sender)", orders)
        for symbol in ("HandleEnchantCreate", "HandleEnchantMeet", "TickEnchantOrder",
                       "IsEnchantOrderTrader", "IsEnchantReagent", "HasActiveEnchantErrand"):
            self.assertIn(symbol, read(HDR), f"missing {symbol} in {HDR}")

    def test_tick_and_logout_hooks(self):
        orders = read(ORDERS)
        self.assertIn("IsEnchantReagent(o.itemId, itemId)", orders)
        self.assertIn("TickEnchantOrder owns this row", orders)
        self.assertIn("an enchant order runs without work", orders)
        self.assertIn("ForgetEnchantOrder(botGuidLow)", orders)
        botmgr = read(BOTMGR)
        self.assertIn("TickEnchantOrder(me)", botmgr)
        self.assertIn("HasActiveEnchantErrand(me)", botmgr)
        trade = read(TRADE)
        if MUTATE:
            trade = trade.replace("IsEnchantOrderTrader(bot, trader)", "true")
        self.assertIn("IsEnchantOrderTrader(bot, trader)", trade,
                      "the addon trade driver must leave an enchant window alone")

    def test_core_trade_effects_and_no_bank(self):
        service = service_text()
        for symbol in (
            "HandleInitiateTradeOpcode",
            "HandleAcceptTradeOpcode",
            "HandleCancelTradeOpcode",
            "SetSpell",
            "GetItemByGuid",
            "GetEnchantmentId",
            "PERM_ENCHANTMENT_SLOT",
            "EnchantDoneVerified",
            "CheckEnchantTrade",
            "CheckMaterialsTrade",
            "DecideMeet",
            "DecideEnchantReplace",
            "CheckTradeTry",
            "EnchantRestartState",
        ):
            self.assertIn(symbol, service, f"missing {symbol} in {SERVICE}")
        # The owner rule: an enchant order never withdraws from the guild bank.
        for forbidden in ("DepositItem", "GuildBank", "HandleAutoGuildBankItem"):
            self.assertNotIn(forbidden, service, f"{SERVICE} must not touch the guild bank")
        self.assertIn("mine->SetSpell(record.itemId);", service)

    def test_read_verbs_accept_enchant(self):
        text = read(READ)
        if MUTATE:
            text = text.replace('if (lower == "enchant") { out = OrderType::Enchant; return true; }', "")
        self.assertIn('if (lower == "enchant") { out = OrderType::Enchant; return true; }', text)
        self.assertNotIn("enchant orders are not in this build", text)
        for symbol in ("EnchantSearchMatches", "EnchantSearchVerdict", "EnchantReagentsHeldInFull",
                       "EnchantEntryFor", "ParseEnchantSpellToken"):
            self.assertIn(symbol, text, f"missing {symbol} in {READ}")
        policy = read("runtime/GuildOrderReadPolicy.h")
        self.assertIn("type == OrderType::Enchant", policy)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
