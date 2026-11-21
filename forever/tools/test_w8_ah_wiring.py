#!/usr/bin/env python3
"""Wiring test for W8 (auction house: compat pair, host seam, stackable check, orders, state, SLICE-W8).

Run:
  python3 -m unittest forever/tools/test_w8_ah_wiring.py
  python3 forever/tools/test_w8_ah_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def check_stackable_and_host_wiring(sell_policy, host_src, ah_action):
    if "in.maxStackSize > 1" not in sell_policy or "AhPostKind::Commodity" not in sell_policy:
        raise AssertionError("AhSellPolicy.h missing stackable -> Commodity check")
    for forbidden in ("AddAuction(", "RemoveAuction(", "WorldDatabase.Query", "WorldDatabase.PQuery"):
        if forbidden in host_src:
            raise AssertionError(f"TbHostAuction.cpp must not call {forbidden}")
    for required in ("HandleAuctionSellItem", "HandleAuctionSellCommodity", "HandleAuctionPlaceBid",
                     "HandleAuctionGetCommodityQuote", "HandleAuctionConfirmCommoditiesPurchase",
                     "HandleAuctionRemoveItem", "BuildListOwnedItems", "UNIT_NPC_FLAG_2_NONE"):
        if required not in host_src:
            raise AssertionError(f"TbHostAuction.cpp missing {required}")
    if "TbCompatWL8.h" not in ah_action or "AhPostFromDecisionCompat" not in ah_action:
        raise AssertionError("AhAction.cpp must route through TbCompatWL8.h / tbhost::ah")


class TestW8AhWiring(unittest.TestCase):
    def test_slice_lists_include_ah_action(self):
        for list_rel in ("forever/SLICE-W8.txt", "forever/SLICE-W1.txt"):
            txt = read(list_rel)
            self.assertIn("ai/playerbot/strategy/actions/AhAction.cpp", txt, f"missing AhAction.cpp in {list_rel}")

    def test_context_registers_w8_keys(self):
        gen = read("forever/tools/slice_gen_context.py")
        self.assertIn("W8_KEYS", gen)
        ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
        for key in ('"ah"', '"ah bid"', '"ah cancel"'):
            self.assertIn(key, ctx, f"missing {key} in SliceAiObjectContext.cpp")

    def test_compat_pair_and_auction_item_count(self):
        f_hdr = read("forever/src/compat/TbCompat_WL8.h")
        t_hdr = read("ai/cmangos-compat-wl8.h")
        self.assertIn("AuctioneerIfCanInteractCompat", f_hdr)
        self.assertIn("AuctioneerIfCanInteractCompat", t_hdr)
        s9 = read("forever/src/compat/TbCompat_S9.h")
        self.assertIn("GetAItem", s9)
        self.assertNotIn("return 0; }", s9.split("AuctionItemCountCompat")[1].split("\n")[0])

    def test_orders_and_state_and_config(self):
        orders = read("forever/src/host/TbOrders.cpp")
        self.assertIn('"ah"', orders)
        state = read("forever/src/host/TbPartyState.cpp")
        self.assertIn('"ah"', state)
        host = read("forever/src/host/TbHostAuction.cpp")
        self.assertIn("TortoiseBots.World.Auction", host)

    def test_stackable_and_host_wiring(self):
        sell_policy = read("ai/playerbot/policy/ah/AhSellPolicy.h")
        host_src = read("forever/src/host/TbHostAuction.cpp")
        ah_action = read("ai/playerbot/strategy/actions/AhAction.cpp")
        if MUTATE:
            sell_policy = sell_policy.replace("in.maxStackSize > 1", "false")
        check_stackable_and_host_wiring(sell_policy, host_src, ah_action)

    def test_unported_row_removed(self):
        unported_h = read("ai/playerbot/UnportedFeatures.h")
        unported_md = read("forever/docs/UNPORTED.md")
        self.assertNotIn("auction (type only)", unported_h)
        self.assertNotIn("auction (type only)", unported_md)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
