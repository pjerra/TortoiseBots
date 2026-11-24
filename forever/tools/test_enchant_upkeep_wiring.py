#!/usr/bin/env python3
"""Wiring pins for the free-bot enchanting upkeep (TBF #153 P3): the two
host switches, the per-bot tick and engine pause, the core cast and gate the
service uses, the kit grant, the shared disenchant loot helper and the
shop-window rows.

Run:
  python3 forever/tools/test_enchant_upkeep_wiring.py
  python3 forever/tools/test_enchant_upkeep_wiring.py --mutate   # must FAIL
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
HOST = "forever/src/host/TbHostProfessionUpkeep.cpp"
WEIGHT = "forever/src/host/TbHostProfessionUpkeepWeight.cpp"
HDR = "forever/src/host/TbHostProfessionUpkeep.h"
POLICY = "ai/playerbot/policy/profession/EnchantUpkeepPolicy.h"
BOTMGR = "forever/src/TbBotMgr.cpp"
PROF = "forever/src/host/TbHostProfession.cpp"
DISENCHANT = "forever/src/host/TbHostGuildOrderDisenchant.cpp"
DISENCHANT_HDR = "forever/src/host/TbHostGuildOrderDisenchant.h"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def botmgr_text():
    text = read(BOTMGR)
    if MUTATE:
        text = text.replace('GetBoolDefault("TortoiseBots.World.Disenchant", true)',
                            'GetBoolDefault("TortoiseBots.World.Disenchant", false)')
    return text


class TestEnchantUpkeepWiring(unittest.TestCase):
    def test_host_files_exist_and_are_slice_host_files(self):
        for rel in (HOST, WEIGHT, HDR, POLICY):
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)
        # forever/src/host/TbHost*.cpp is the slice-only glob; a new host file
        # needs no CMake edit.
        self.assertIn("host/TbHost*.cpp", read("CMakeLists.txt"))
        hdr = read(HDR)
        for symbol in ("TickProfessionUpkeep", "HasActiveUpkeepCast", "ForgetProfessionUpkeep", "EnchantWeightFor"):
            self.assertIn(symbol, hdr, symbol)

    def test_switches_default_to_the_ported_behaviour(self):
        text = botmgr_text()
        self.assertIn('GetBoolDefault("TortoiseBots.World.Disenchant", true)', text)
        self.assertIn('GetBoolDefault("TortoiseBots.World.EnchantGear", true)', text)
        self.assertIn("_disenchantUpkeepEnabled", text)
        self.assertIn("_enchantGearEnabled", text)
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.World.Disenchant = 1", conf)
        self.assertIn("TortoiseBots.World.EnchantGear = 1", conf)
        info = read("forever/src/host/TbAddonInfo.cpp")
        self.assertIn('"TortoiseBots.World.Disenchant", true, 1', info)
        self.assertIn('"TortoiseBots.World.EnchantGear", true, 1', info)
        docs = read("docs/server/configuration.md")
        self.assertIn("`TortoiseBots.World.Disenchant`", docs)
        self.assertIn("`TortoiseBots.World.EnchantGear`", docs)

    def test_tick_runs_for_free_bots_and_pauses_the_engine(self):
        text = botmgr_text()
        self.assertIn("#include \"host/TbHostProfessionUpkeep.h\"", text)
        self.assertIn("TickProfessionUpkeep(me, diff, _disenchantUpkeepEnabled, _enchantGearEnabled)", text)
        self.assertIn("bot.free && (_disenchantUpkeepEnabled || _enchantGearEnabled)", text)
        self.assertIn("tbhost::upkeep::HasActiveUpkeepCast(me)", text)

    def test_upkeep_casts_through_the_core(self):
        host = read(HOST)
        self.assertIn("HandleCastSpellOpcode(packet)", host)
        self.assertIn("TARGET_FLAG_ITEM", host)
        # Disenchant: the same core gates the mail service reads; the dead
        # legacy prototype id must not come back.
        self.assertIn("GetDisenchantLootId()", host)
        self.assertIn("GetDisenchantSkillRequired()", host)
        self.assertNotIn("ItemDisenchantIdCompat", host)
        # Enchant: fit and weight are the core's and the catalogue's.
        self.assertIn("IsFitToSpellRequirements(spell)", host)
        self.assertIn("EnchantWeightFor(bot, enchantId)", host)
        # The legacy weight manager needs the compat shim, so its call is the
        # only thing in its own TU; the tick file keeps the core macros.
        weight = read(WEIGHT)
        self.assertIn("CalculateEnchantWeight", weight)
        self.assertIn('#include "compat/botpch_forever.h"', weight)
        self.assertNotIn("botpch_forever.h", host)
        self.assertIn("GetEnchantmentId(PERM_ENCHANTMENT_SLOT)", host)
        # Only a known spell is cast (the legacy action's own rule); the kit
        # grants the recipes and the rod the profession needs.
        self.assertIn("!bot->HasSpell(entry.spellId)", host)
        self.assertIn("EnchantRecipeGrantable", host)
        self.assertIn("LearnSpell", host)
        self.assertIn("kArcaniteRod", host)
        # No legacy maintenance action may be re-registered: those classes are
        # out of the slice.
        self.assertNotIn("disenchant random item", host)
        self.assertNotIn("enchant random item", host)

    def test_policy_decides_not_the_host(self):
        host = read(HOST)
        self.assertIn("DisenchantUpkeepWants", host)
        self.assertIn("EnchantUpkeepWants", host)
        self.assertIn("SharesWithRealPlayer", host)
        self.assertIn("IsItemNeededByGuildOrder", host)
        self.assertIn("DisenchantRowsHoldItem", host)

    def test_disenchant_loot_helper_is_shared_not_copied(self):
        hdr = read(DISENCHANT_HDR)
        self.assertIn("void TakeItemLoot(Player* bot, ObjectGuid const& itemGuid);", hdr)
        service = read(DISENCHANT)
        self.assertIn("void TakeItemLoot(Player* bot, ObjectGuid const& itemGuid)", service)
        self.assertEqual(service.count("void TakeItemLoot(Player* bot, ObjectGuid const& itemGuid)"), 1,
                         "the helper must be defined once, at namespace scope")
        self.assertIn("tbhost::guild_orders::TakeItemLoot(bot, cast.itemGuid)", read(HOST))

    def test_logout_forgets_the_state(self):
        prof = read(PROF)
        self.assertIn("upkeep::ForgetProfessionUpkeep(botGuid.GetCounter())", prof)

    def test_state_is_not_kept_in_the_slice_stubs(self):
        stubs = read("forever/src/slice/SliceLinkStubs.cpp")
        self.assertNotIn("TickProfessionUpkeep", stubs)
        self.assertNotIn("ForgetProfessionUpkeep", stubs)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
