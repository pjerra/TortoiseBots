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


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
