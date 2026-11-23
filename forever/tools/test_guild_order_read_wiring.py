#!/usr/bin/env python3
"""Wiring test for the read-only guild order verbs (search, who, preview, steps).
Run:
  python3 -m unittest forever/tools/test_guild_order_read_wiring.py
  python3 forever/tools/test_guild_order_read_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestGuildOrderReadWiring(unittest.TestCase):
    def test_read_files_exist_and_policy_is_compiled(self):
        for rel in (
            "runtime/GuildOrderReadPolicy.h",
            "forever/src/host/TbHostGuildOrderRead.h",
            "forever/src/host/TbHostGuildOrderRead.cpp",
            "forever/tools/test_guild_order_read.cpp",
        ):
            self.assertTrue(os.path.isfile(os.path.join(ROOT, rel)), f"missing {rel}")
        self.assertIn("runtime/GuildOrderReadPolicy.h",
                      read("forever/src/slice/SliceGuildOrderHeaders.cpp"))

    def test_dispatch_reaches_the_read_verbs(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        if MUTATE:
            orders = orders.replace("guild_read::IsReadVerb", "guild_read::NotAReadVerb")
        self.assertIn("guild_read::IsReadVerb", orders)
        self.assertIn("guild_read::Handle", orders)
        for verb in ("search", "who", "preview", "steps"):
            self.assertIn(verb, orders)
        self.assertIn("search|who|preview|steps", orders)

    def test_store_records_step_history_and_reads_never_write(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        self.assertIn("NoteOrderHistory", orders)
        self.assertIn("ForgetOrderHistory", orders)
        self.assertIn("ForgetAllOrderHistory", orders)
        reads = read("forever/src/host/TbHostGuildOrderRead.cpp")
        # A read verb must never place or cancel an order.
        self.assertNotIn("GiveOrder(", reads)
        self.assertNotIn("CancelOrder(", reads)
        # Decision 6: no guild bank withdrawal anywhere in the read path.
        self.assertNotIn("Withdraw", reads)
        self.assertNotIn("HandleGuildBankWithdrawMoney", reads)
        # Reuses the shared pure policies instead of reimplementing them.
        self.assertIn("NoteOrderStep", reads)
        self.assertIn("OrderSearchShows", reads)
        self.assertIn("CountWhoReagents", reads)
        self.assertIn("SearchOrderLess", reads)

    def test_item_index_reads_world_data_through_the_guard(self):
        reads = read("forever/src/host/TbHostGuildOrderRead.cpp")
        self.assertIn("WorldQueryCompat", reads)
        self.assertIn("gameobject_loot_template", reads)
        self.assertIn("reference_loot_template", reads)
        self.assertIn("Player::GetClassicProfessionSkill", reads)
        self.assertNotIn("WorldDatabase.Query", reads)
        # Live preview data comes from the existing host seams, not a second copy.
        self.assertIn("tbhost::ah::QueryMarketByItemId", reads)
        self.assertIn("tbhost::guild_orders::BotBagItemCount", reads)

    def test_read_verbs_are_documented(self):
        commands = read("forever/docs/TB-COMMANDS.md")
        self.assertIn("search <bot>", commands)
        self.assertIn("preview <bot>", commands)
        self.assertIn("steps <bot>", commands)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
