#!/usr/bin/env python3
"""Wiring test for fishing Farm orders (TBF #155): the boot-time fish source
index, the terminal farm-step verdict and the pure policy that backs it.
Run:
  python3 -m unittest forever/tools/test_guild_order_fish_wiring.py
  python3 forever/tools/test_guild_order_fish_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestGuildOrderFishWiring(unittest.TestCase):
    def test_slice_build_sees_the_new_host_files(self):
        # The ForeverCore slice build adds forever/src/host/TbHost*.cpp, so the
        # fish module must keep the TbHost prefix or it silently stops building.
        for name in ("TbHostGuildOrderFish.cpp", "TbHostGuildOrderFish.h", "TbHostGuildOrderFishPure.h"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/src/host", name)), name)
        cmake = read("CMakeLists.txt")
        self.assertIn("host/TbHost*.cpp", cmake)

    def test_boot_builds_the_fish_index(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace("LoadFishingSources();", "LoadNothing();")
        self.assertIn('#include "host/TbHostGuildOrderFish.h"', orders)
        self.assertIn("tbhost::guild_order_fish::LoadFishingSources();", orders)

    def test_terminal_farm_step_asks_for_the_fish_verdict(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace("ResolveFarmRefusal", "ResolveNothing")
        self.assertIn("tbhost::guild_order_fish::ResolveFarmRefusal(bot, rec.itemId)", orders)
        self.assertIn("StateForReason(*fishReason)", orders)
        self.assertIn("fishing=1", orders)

    def test_policy_is_pure_and_maps_the_contract_reasons(self):
        pure = read("forever/src/host/TbHostGuildOrderFishPure.h")
        for symbol in ("DecideFishFarmVerdict", "FishFarmReason", "FishFarmVerdict::BuildCannotFish"):
            self.assertIn(symbol, pure)
        self.assertIn("OrderReason::NoSkill", pure)
        self.assertIn("OrderReason::LowSkill", pure)
        self.assertIn("OrderReason::NoSource", pure)
        # No core headers: the policy stays standalone-testable.
        self.assertNotIn('#include "Player.h"', pure)
        self.assertNotIn('#include "ObjectMgr.h"', pure)

    def test_host_source_reads_the_fishing_loot_tables(self):
        host = read("forever/src/host/TbHostGuildOrderFish.cpp")
        for table in ("fishing_loot_template", "reference_loot_template", "gameobject_loot_template", "gameobject_template"):
            self.assertIn(table, host)
        self.assertIn("WorldQueryCompat", host)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
