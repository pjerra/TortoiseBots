#!/usr/bin/env python3
"""Wiring pins for host fishing (TBF #155): the errand joins the gather/craft
step, the guild order grants the skill and pole, the travel fish store is real,
and the fishing loot is no longer counted unported.
Run:
  python3 -m unittest forever/tools/test_fishing_wiring.py
  python3 forever/tools/test_fishing_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestFishingWiring(unittest.TestCase):
    def test_host_errand_is_a_slice_host_file(self):
        for name in ("TbHostFishing.cpp", "TbHostFishing.h", "TbHostFishingPure.h"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/src/host", name)), name)
        cmake = read("CMakeLists.txt")
        self.assertIn("host/TbHost*.cpp", cmake)

    def test_gather_step_starts_and_polls_the_errand(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace("StartOrderErrand", "StartNothing")
        self.assertIn('#include "host/TbHostFishing.h"', orders)
        self.assertIn("tbhost::fishing::CanFishOrder(bot, itemId)", orders)
        self.assertIn("tbhost::fishing::StartOrderErrand(bot, itemId, fishReason)", orders)
        self.assertIn("tbhost::fishing::EnsureOrderFishing(bot, rec.itemId)", orders)
        self.assertIn("_pending[botGuidLow] = { itemId, beforeCount, false, true };", orders)

    def test_profession_errand_tick_and_pause_see_fishing(self):
        prof = read("forever/src/host/TbHostProfession.cpp")
        self.assertIn('#include "host/TbHostFishing.h"', prof)
        self.assertIn("fishing::TickErrand(bot, diffMs)", prof)
        self.assertIn("fishing::HasActiveErrand(bot)", prof)
        self.assertIn("fishing::ClearErrand(botGuid)", prof)

    def test_travel_fish_store_is_real_on_forevercore(self):
        compat = read("forever/src/compat/TbCompat_WL4.h")
        self.assertIn("inline bool TravelFishingCompat() { return true; }", compat)
        self.assertIn("AreaIdOfStoredFishFlagWL4", compat)
        self.assertIn("FishingBaseSkillQuietWL4", compat)
        travel = read("ai/playerbot/TravelMgr.cpp")
        self.assertIn("bool TravelMgr::NearestFishSpotOnMap", travel)
        bridge = read("forever/src/compat/TbCompat_WL4.cpp")
        self.assertIn("NearestFishingSpot", bridge)
        self.assertIn("FishingStoreLoaded", bridge)

    def test_order_verdict_now_reports_a_real_executor(self):
        fish = read("forever/src/host/TbHostGuildOrderFish.cpp")
        if MUTATE:
            fish = fish.replace("return fishing::HostAvailable();", "return false;")
        self.assertIn("return fishing::HostAvailable();", fish)
        host = read("forever/src/host/TbHostFishing.cpp")
        self.assertIn("bool HostAvailable()", host)
        self.assertIn("return tbcompat::FishingStoreLoaded();", host)

    def test_errand_loots_a_hole_catch_too(self):
        host = read("forever/src/host/TbHostFishing.cpp")
        if MUTATE:
            host = host.replace("LOOT_FISHINGHOLE", "LOOT_CORPSE")
        self.assertIn("LOOT_FISHINGHOLE", host)
        self.assertIn("bot->GetAELootView()", host)
        self.assertIn("loot->GetOwnerGUID()", host)

    def test_open_water_is_the_executor_source(self):
        fish = read("forever/src/host/TbHostGuildOrderFish.cpp")
        if MUTATE:
            fish = fish.replace("addOpenWaterItem(itemId, skill);", "addItem(itemId, skill);")
        self.assertIn("addOpenWaterItem(itemId, skill)", fish)
        self.assertIn("IsOpenWaterFishingSource", fish)
        host = read("forever/src/host/TbHostFishing.cpp")
        self.assertIn("IsOpenWaterFishingSource", host)

    def test_fishing_loot_is_no_longer_listed_unported(self):
        features = read("ai/playerbot/UnportedFeatures.h")
        self.assertNotIn("skinning a corpse, pickpocketing, fishing and disenchant loot", features)
        unported = read("forever/docs/UNPORTED.md")
        self.assertNotIn("fishing and disenchant loot stay", unported)

    def test_idle_fishing_key_defaults_on(self):
        mgr = read("forever/src/TbBotMgr.cpp")
        if MUTATE:
            mgr = mgr.replace('GetBoolDefault("TortoiseBots.World.Fishing", true)',
                              'GetBoolDefault("TortoiseBots.World.Fishing", false)')
        self.assertIn('GetBoolDefault("TortoiseBots.World.Fishing", true)', mgr)
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.World.Fishing = 1", conf)
        docs = read("docs/server/configuration.md")
        self.assertIn("`TortoiseBots.World.Fishing` | `1`", docs)
        addon = read("forever/src/host/TbAddonInfo.cpp")
        self.assertIn('"world-fishing", "TortoiseBots.World.Fishing", true, 1', addon)

    def test_idle_tick_is_wired_and_pauses_the_engine(self):
        mgr = read("forever/src/TbBotMgr.cpp")
        if MUTATE:
            mgr = mgr.replace("tbhost::fishing::TickIdle(me, diff, bot.free && _fishingEnabled);", "")
        self.assertIn('#include "host/TbHostFishing.h"', mgr)
        self.assertIn("tbhost::fishing::TickIdle(me, diff, bot.free && _fishingEnabled);", mgr)
        self.assertIn("tbhost::fishing::HasActiveIdleErrand(me)", mgr)

    def test_idle_errand_tick_search_and_guard(self):
        host = read("forever/src/host/TbHostFishing.cpp")
        if MUTATE:
            host = host.replace("FindNearbyWater(bot, botX, botY, botZ, radius, water)", "FindNothing()")
        for needle in ("void TickIdle(Player* bot, std::uint32_t diffMs, bool enabled)",
                       "FindNearbyWater", "FindFishingHole", "FindShoreCastSpot",
                       "StepTowardFishableWater", "IdleSessionOver", "IdleSearchAllowed",
                       "SpotGuarded", "it->second.idle"):
            self.assertIn(needle, host)
        pure = read("forever/src/host/TbHostFishingPure.h")
        self.assertIn("IdleSessionStartable", pure)
        self.assertIn("IdleSpotChosen", pure)
        prof = read("forever/src/host/TbHostProfession.cpp")
        self.assertIn("fishing::TickErrand(bot, diffMs)", prof)
        header = read("forever/src/host/TbHostFishing.h")
        self.assertIn("HasActiveIdleErrand", header)

    def test_spot_guard_bridge_uses_the_static_spawn_index(self):
        compat = read("forever/src/compat/TbCompat_Fishing.h")
        self.assertIn("bool SpotGuarded(", compat)
        impl = read("forever/src/compat/TbCompat_WL4.cpp")
        if MUTATE:
            impl = impl.replace("getHighestHostileLevelNear", "getHighestUnrelatedLevelNear")
        self.assertIn("getHighestHostileLevelNear", impl)
        self.assertIn("highest + 2 >= botLevel", impl)

    def test_the_bot_learns_and_casts_its_fishing_rank_spell(self):
        host = read("forever/src/host/TbHostFishing.cpp")
        if MUTATE:
            host = host.replace("if (!EnsureFishingSpell(bot))", "if (false)")
        # The rank spells are Learned rows, so the skill grant must teach one.
        self.assertIn("Player::GetClassicProfessionSkill(ability->SkillLine) != SKILL_FISHING", host)
        self.assertNotIn("GetSkillLineAbilitiesBySkill(SKILL_FISHING)", host)
        self.assertIn("GetValueForTierIndex(rank - 1)", host)
        self.assertIn("bot->LearnSpell(pick, false)", host)
        self.assertNotIn("kFishingCastSpellId", host)
        self.assertNotIn("CastSpell(dest,", host)
        # Legacy fish action form: the spell the bot knows, aimed at the bot.
        self.assertIn("bot->CastSpell(bot, spellId)", host)
        # Both the idle gear and the order grant run it.
        self.assertEqual(host.count("EnsureFishingSpell(bot)"), 3)

    def test_the_cast_waits_for_the_picked_spot(self):
        pure = read("forever/src/host/TbHostFishingPure.h")
        if MUTATE:
            pure = pure.replace("kCastSpotYd = 2.5f", "kCastSpotYd = 12.0f")
        self.assertIn("kCastSpotYd = 2.5f", pure)
        self.assertIn("f.dryStand ? kCastSpotYd : kCastRangeYd", pure)
        host = read("forever/src/host/TbHostFishing.cpp")
        self.assertIn("facts.dryStand = errand.idle;", host)
        self.assertNotIn("f.distanceToSpot <= 12.0f", pure)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
