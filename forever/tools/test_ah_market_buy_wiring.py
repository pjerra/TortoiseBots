#!/usr/bin/env python3
"""Wiring test for the automatic AH market buyer (TbHostAhMarketBuy.{h,cpp}, QueryMarketByName offset).

Run:
  python3 -m unittest forever/tools/test_ah_market_buy_wiring.py
  python3 forever/tools/test_ah_market_buy_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def check_buy_wiring(pure, source, seller, host_h, host_cpp, action):
    # Own and same-account listings are skipped before the action is even asked.
    grouper = source[source.index("std::vector<PageItem> GroupPage("):]
    grouper = grouper[:grouper.index("} // namespace") if "} // namespace" in grouper else len(grouper)]
    if MUTATE:
        grouper = grouper.replace("if (listing.isOwn)", "if (false)")
    if "listing.isOwn" not in grouper:
        raise AssertionError("GroupPage must skip own/same-account listings")
    if "IsBuyCandidate(in)" not in source:
        raise AssertionError("the scan must use IsBuyCandidate")
    if "BuyChancePasses(" not in source:
        raise AssertionError("the scan must gate on the chance roll")
    for required in ("ah::QueryMarketByName(buyer, auctioneer, \"\", offset)",
                     "RunAction(buyer, \"ah bid\"",
                     "sPlayerbotAIConfig.ahMarketBuyer", "sPlayerbotAIConfig.ahMarketChanceBuy",
                     "sPlayerbotAIConfig.ahMarketMaxOperations", "sPlayerbotAIConfig.ahMarketBudgetUs",
                     "sPlayerbotAIConfig.ahMarketMaxSpendPerBot",
                     "ITEM_USAGE_EQUIP",
                     "FindNearestFactionAuctioneer"):
        if required not in source:
            raise AssertionError(f"TbHostAhMarketBuy.cpp missing {required}")
    for forbidden in ("AddAuction(", "RemoveAuction(", "Item::CreateItem", "->TeleportTo(", "WorldDatabase"):
        if forbidden in source:
            raise AssertionError(f"TbHostAhMarketBuy.cpp must not use {forbidden}")

    if "RunBuyStep();" not in seller:
        raise AssertionError("the seller sweep must call the buyer pass")
    if '#include "host/TbHostAhMarketBuy.h"' not in seller:
        raise AssertionError("TbHostAhMarket.cpp must include the buyer header")

    # The page offset is the one additive change to the host seam.
    decl = "QueryMarketByName(Player* bot, Creature const* auctioneer, std::string const& nameSubstr"
    if decl not in host_h or "uint32_t offset = 0" not in host_h:
        raise AssertionError("TbHostAuction.h must add the offset parameter with a default")
    if "BuildListBuckets(bucketsResult, bot, wname, 0, MAX_LEVEL, everyQuality,\n        std::nullopt, {}, 0, offset, {})" not in host_cpp:
        raise AssertionError("TbHostAuction.cpp must pass the offset to BuildListBuckets")
    if "in.isNeededMatOrExplicit = true;" not in action:
        raise AssertionError("AhBidAction must keep buying the scanned item within its ceiling")


class TestAhMarketBuyWiring(unittest.TestCase):
    def test_files_exist(self):
        for rel in ("forever/src/host/TbHostAhMarketBuy.h", "forever/src/host/TbHostAhMarketBuy.cpp",
                    "forever/src/host/TbHostAhMarketBuyPure.h"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, rel)), rel)

    def test_buy_wiring(self):
        check_buy_wiring(
            read("forever/src/host/TbHostAhMarketBuyPure.h"),
            read("forever/src/host/TbHostAhMarketBuy.cpp"),
            read("forever/src/host/TbHostAhMarket.cpp"),
            read("forever/src/host/TbHostAuction.h"),
            read("forever/src/host/TbHostAuction.cpp"),
            read("ai/playerbot/strategy/actions/AhAction.cpp"),
        )

    def test_auction_action_skips_own_listings(self):
        action = read("ai/playerbot/strategy/actions/AhAction.cpp")
        self.assertIn("if (l.isOwn || l.unitPrice == 0)", action)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
