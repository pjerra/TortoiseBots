#!/usr/bin/env python3
"""Wiring test for the automatic AH market sweep (TbHostAhMarket.{h,cpp}, BotMgr tick and engine pause).

Run:
  python3 -m unittest forever/tools/test_ah_market_wiring.py
  python3 forever/tools/test_ah_market_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def check_sweep_wiring(pure, source, botmgr):
    # The policy, not the host, owns the eligibility answer; without the stock field nothing gates the walk.
    body = pure[pure.index("inline bool IsSweepCandidate("):]
    body = body[:body.index("}")]
    if MUTATE:
        body = body.replace("in.hasSellableStock", "true")
    if "in.freeBot" not in body or "in.hasSellableStock" not in body:
        raise AssertionError("IsSweepCandidate must gate on the free flag and on sellable stock")
    for flag in ("in.inCombat", "in.casting", "in.inDuel", "in.grouped", "in.busyElsewhere"):
        if flag not in body:
            raise AssertionError(f"IsSweepCandidate missing {flag}")

    for required in ("ah::FindNearestFactionAuctioneer", "RunAction(bot, \"ah\", \"vendor\")",
                     "MovePoint(", "ah::IsWorldAuctionEnabled", "sPlayerbotAIConfig.ahMarketEnabled",
                     "sPlayerbotAIConfig.ahMarketInterval", "sPlayerbotAIConfig.ahMarketBatchSize",
                     "ClearBotAhMarket"):
        if required not in source:
            raise AssertionError(f"TbHostAhMarket.cpp missing {required}")
    for forbidden in ("AddAuction(", "RemoveAuction(", "Item::CreateItem", "->TeleportTo(", "WorldDatabase"):
        if forbidden in source:
            raise AssertionError(f"TbHostAhMarket.cpp must not use {forbidden}")

    if '#include "host/TbHostAhMarket.h"' not in botmgr:
        raise AssertionError("TbBotMgr.cpp must include host/TbHostAhMarket.h")
    if "tbhost::ah_market::Tick(me, diff);" not in botmgr:
        raise AssertionError("TbBotMgr.cpp must tick the sweep for free bots")
    if "tbhost::ah_market::HasActiveErrand(me)" not in botmgr:
        raise AssertionError("TbBotMgr.cpp must pause the engine during a sweep walk")
    if botmgr.count("tbhost::ah_market::ClearBotAhMarket(") != 2:
        raise AssertionError("TbBotMgr.cpp must clear the sweep state on both logout paths")


class TestAhMarketWiring(unittest.TestCase):
    def test_files_exist(self):
        for rel in ("forever/src/host/TbHostAhMarket.h", "forever/src/host/TbHostAhMarket.cpp",
                    "forever/src/host/TbHostAhMarketPure.h"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)

    def test_sweep_wiring(self):
        check_sweep_wiring(
            read("forever/src/host/TbHostAhMarketPure.h"),
            read("forever/src/host/TbHostAhMarket.cpp"),
            read("forever/src/TbBotMgr.cpp"),
        )

    def test_world_auction_gate(self):
        host = read("forever/src/host/TbHostAuction.cpp")
        self.assertIn("TortoiseBots.World.Auction", host)
        source = read("forever/src/host/TbHostAhMarket.cpp")
        self.assertIn("ah::IsWorldAuctionEnabled()", source)

    def test_host_glob_picks_up_the_new_tu(self):
        cmake = read("CMakeLists.txt")
        self.assertIn("host/TbHost[^/]*", cmake)
        self.assertIn("TB_SLICE_NEW", cmake)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
