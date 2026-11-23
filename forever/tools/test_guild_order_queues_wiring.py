#!/usr/bin/env python3
"""Wiring test for the guild gold refund queue (TBF #155): a bot holding
unspent guild gold repays it at a guild banker through the deposit-only seam.

Run:
  python3 forever/tools/test_guild_order_queues_wiring.py
  python3 forever/tools/test_guild_order_queues_wiring.py --mutate   # must FAIL

Pinned here: the banker pass is ticked beside the order tick, it pays only
through the deposit-only guild seam and only when the row still holds gold,
and a finished or cancelled row holding gold parks instead of disappearing.
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False
SERVICE = "forever/src/host/TbHostGuildOrderQueues.cpp"
HEADER = "forever/src/host/TbHostGuildOrderQueues.h"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def service_text():
    text = read(SERVICE)
    if MUTATE:
        # The failing case first: a pass that skips the refund policy would
        # deposit an amount the row does not owe.
        text = text.replace("GuildGoldRefundAmount", "BypassRefund")
    return text


class TestGuildOrderRefundWiring(unittest.TestCase):
    def test_startup_tick_and_logout_hooks(self):
        self.assertIn("TickGuildOrderQueues(me)", read("forever/src/TbBotMgr.cpp"))
        self.assertIn("ForgetGuildOrderQueueSession(botGuidLow)",
                      read("forever/src/host/TbHostGuildOrders.cpp"))

    def test_banker_pass_pays_through_the_deposit_only_seam(self):
        header = read(HEADER)
        for symbol in ("TickGuildOrderQueues", "ForgetGuildOrderQueueSession"):
            self.assertIn(symbol, header, f"missing {symbol} in {HEADER}")
        service = service_text()
        for symbol in ("GuildGoldRefundAmount", "GuildGoldRefundRemaining",
                       "tbhost::guild::DepositMoney", "GAMEOBJECT_TYPE_GUILD_BANK"):
            self.assertIn(symbol, service, f"missing {symbol} in {SERVICE}")
        self.assertNotIn("WithdrawMoney", service)
        self.assertNotIn("WithdrawItem", service)
        guild = read("forever/src/host/TbHostGuild.h")
        self.assertIn("Guild bank DEPOSIT only", guild)

    def test_only_a_row_that_still_holds_gold_is_paid(self):
        service = read(SERVICE)
        self.assertIn("order->guildGoldHeld == 0", service)
        self.assertIn("order->guildId != bot->GetGuildId()", service)
        self.assertIn("bot->isDead() || bot->IsInCombat()", service)
        self.assertIn("GetGameObjectIfCanInteractWith", service)

    def test_finished_row_holding_gold_parks(self):
        orders = read("forever/src/host/TbHostGuildOrders.cpp")
        self.assertIn("IsFinished(record.state) && record.guildGoldHeld > 0", orders)
        self.assertIn("record.cancelled = true;", orders)
        # Cancelling a row that still holds gold parks the debt as a stub.
        self.assertIn("if (it->second.guildGoldHeld > 0)", orders)
        self.assertIn("refund stub the refund pass pays", orders)
        reassign = read("forever/src/host/TbHostGuildOrderReassign.cpp")
        self.assertIn("stub.cancelled = true;", reassign)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
