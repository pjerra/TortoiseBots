#!/usr/bin/env python3
"""Wiring test for W2 (vendor, repair, trainer, mail read).
Run:
  python3 -m unittest forever/tools/test_w2_town_wiring.py
  python3 forever/tools/test_w2_town_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestW2TownWiring(unittest.TestCase):
    def test_slice9_lists_w2_files(self):
        slice9 = read("forever/SLICE9.txt")
        for rel in (
            "ai/playerbot/strategy/actions/SellAction.cpp",
            "ai/playerbot/strategy/actions/BuyAction.cpp",
            "ai/playerbot/strategy/actions/RepairAllAction.cpp",
            "ai/playerbot/strategy/actions/TrainerAction.cpp",
            "ai/playerbot/strategy/actions/MailAction.cpp",
            "ai/playerbot/strategy/values/BudgetValues.cpp",
        ):
            self.assertIn(rel, slice9, f"missing {rel} in SLICE9.txt")

    def test_context_registers_w2_keys(self):
        gen = read("forever/tools/slice_gen_context.py")
        self.assertIn("W2_KEYS", gen)
        ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
        for key in ('"sell"', '"buy"', '"repair"', '"trainer"', '"mail"', '"free money for"'):
            self.assertIn(key, ctx, f"missing {key} in SliceAiObjectContext.cpp")

    def test_orders_wire_w2_verbs(self):
        orders = read("forever/src/host/TbOrders.cpp")
        for verb in ('"sell"', '"buy"', '"repair"', '"train"', '"mail"'):
            self.assertIn(verb, orders, f"missing verb {verb} in TbOrders.cpp")

    def test_state_reports_dur_and_mail(self):
        state = read("forever/src/host/TbPartyState.cpp")
        self.assertIn('"dur"', state)
        self.assertIn('"mail"', state)

    def test_no_raw_world_db_query_in_town_host(self):
        host = read("forever/src/host/TbHostTown.cpp")
        if MUTATE:
            host += "\nWorldDatabase.Query(\"SELECT 1\");\n"
        self.assertNotIn("WorldDatabase.Query", host)
        self.assertNotIn("WorldDatabase.PQuery", host)
        self.assertIn("UNIT_NPC_FLAG_2_NONE", host)
        self.assertIn("BuyItemFromVendorSlot", host)
        self.assertIn("SellItemToVendor", host)
        self.assertIn("DurabilityRepairAll", host)
        self.assertIn("TeachSpell", host)
        self.assertIn("HandleMailTakeMoney", host)
        self.assertIn("HandleMailTakeItem", host)
        self.assertIn("FindNearestMailbox", host)
        self.assertIn("GetGameObjectIfCanInteractWith", host)
        self.assertIn("MailTakeMoneyConfirmed", host)
        self.assertIn("MailTakeItemConfirmed", host)

    def test_mail_order_honest_and_refuses_no_mailbox(self):
        orders = read("forever/src/host/TbOrders.cpp")
        if MUTATE:
            orders = orders.replace('"no-mailbox"', '"mail-ok"')
        self.assertIn('"no-mailbox"', orders)
        self.assertIn("FindNearestMailbox", orders)
        self.assertIn('"fail:mail-none-taken"', orders)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
