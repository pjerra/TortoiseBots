#!/usr/bin/env python3
"""Wiring pins for the addon order window verbs (TBF #155).

The core cannot run here, so these pin where the `order` verb is registered,
which capability tokens it advertises, that every reply is built through the
existing order store/read helpers, and that the live file keeps off the
unlinked legacy runtime formatters. Reach into the TBM shapes through
forever/tools/test_addon_orders.cpp.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
TOOLS = ROOT / "forever" / "tools"
PURE = HOST / "TbAddonOrdersPure.h"
IMPL = HOST / "TbAddonOrders.cpp"
HDR = HOST / "TbAddonOrders.h"
UNIT = TOOLS / "test_addon_orders.cpp"


def read(path):
    return Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class Wiring(unittest.TestCase):
    def setUp(self):
        self.pure = read(PURE)
        self.cpp = read(IMPL)
        self.hdr = read(HDR)
        self.scripts = read(SRC / "TbScripts.cpp")
        self.unit = read(UNIT)
        self.access_h = read(HOST / "TbHostGuildOrderAccess.h")
        self.access_cpp = read(HOST / "TbHostGuildOrderAccess.cpp")

    def test_registered_on_the_addon_channel(self):
        self.assertIn('#include "host/TbAddonOrders.h"', self.scripts)
        self.assertIn("TortoiseBots::addonorders::RegisterAddonVerbsOrders();", self.scripts)
        cpp = strip_comments(self.cpp)
        self.assertIn('addon::RegisterVerb("order", &HandleOrderVerb);', cpp)
        self.assertIn('#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1', cpp)
        self.assertIn("void RegisterAddonVerbsOrders() { }", cpp)

    def test_capability_tokens_are_the_addon_gate(self):
        cpp = strip_comments(self.cpp)
        self.assertIn('addon::AdvertiseCap("guild-orders");', cpp)
        self.assertIn('addon::AdvertiseCap(GuildOrders::PlayerBuyCapToken());', cpp)
        self.assertIn('addon::AdvertiseCap("guild-order-steps");', cpp)
        # The owner rule: no player Buy option.
        self.assertIn("PlayerBuyOrderRefused", strip_comments(read(
            ROOT / "runtime" / "GuildOrderPlayerBuyPolicy.h")))

    def test_every_verb_uses_the_existing_order_functions(self):
        cpp = strip_comments(self.cpp)
        for call in (
            "tbhost::guild_orders::ListOrdersForGuild(",
            "tbhost::guild_orders::FindOrder(",
            "tbhost::guild_orders::GiveOrder(",
            "tbhost::guild_orders::CancelOrder(",
            "tbhost::guild_orders::ReassignOrder(",
            "tbhost::guild_orders::SetOrderWorking(",
            "tbhost::guild_order_access::BuildAccessReport(",
            "tbhost::guild_order_access::MayGiveOrders(",
            "tbhost::guild_order_access::MayChangeBotRank(",
            "tbhost::guild_order_access::SetBotRank(",
            "h3::guild_read::Handle(",
        ):
            self.assertIn(call, cpp, call)

    def test_create_starts_the_order_and_reports_the_row(self):
        cpp = strip_comments(self.cpp)
        self.assertIn("tbhost::guild_orders::SetOrderWorking(bot->GetGUID().GetCounter(), true);", cpp)
        # No live TBM:ORDER push exists, so the give answers with a snapshot.
        create = cpp[cpp.index("void HandleCreate("):cpp.index("void HandlePreview(")]
        self.assertIn("SendOrderList(requester, guildId);", create)
        # Owner rule: only a craft the bot can make and hand over.
        self.assertIn("RefuseHandoff(requester, preview)", create)
        helper = cpp[cpp.index("bool RefuseHandoff("):cpp.index("std::vector<addonorders::Record> RunRead(")]
        for flag in ('"handoff"', '"bound"', '"quest"', '"timed"', '"conjured"'):
            self.assertIn(flag, helper, flag)

    def test_read_verbs_replay_the_tb_reply(self):
        cpp = strip_comments(self.cpp)
        self.assertIn("class CaptureHandler final : public ChatHandler", cpp)
        self.assertIn("void SendSysMessage(std::string_view text, bool", cpp)
        for sub in ('"search"', '"who"', '"preview"', '"steps"'):
            self.assertIn(sub, cpp)
        # Commands that exist in the old store are never answered by a fork of
        # their logic: only the read half is replayed.
        self.assertNotIn("HandleGuildOrder(", cpp)

    def test_no_unlinked_legacy_formatters(self):
        cpp = strip_comments(self.cpp)
        self.assertNotIn("GuildOrderStore.h", cpp)
        self.assertNotIn("FormatOrderLine(", cpp)
        self.assertNotIn("GuildOrders::Carry(", cpp)

    def test_reply_shapes(self):
        pure = strip_comments(self.pure)
        for prefix in (
            '"TBM:ORDER|"',
            '"TBM:ORDER_ACCESS|"',
            '"TBM:ORDER_ITEM|"',
            '"TBM:ORDER_WHO|"',
            '"TBM:ORDER_PREVIEW|"',
            '"TBM:ORDER_STEP|"',
            '"TBM:ORDER_BEGIN|"',
            '"TBM:ORDER_END"',
        ):
            self.assertIn(prefix, pure, prefix)
        self.assertIn("PercentDecode", pure)
        self.assertIn("kind.back() == '+'", pure)

    def test_contract_samples_come_from_the_addon(self):
        self.assertIn("tests/regression.lua:3400", self.unit)
        self.assertIn("tests/regression.lua:5392", self.unit)
        self.assertIn("tests/geometry_tests.lua:1793", self.unit)
        self.assertIn("TBM:ORDER|Goldie|farm|2318|Light Leather|10|10|working|-|Ashenvale|-|-|keep|-|-|farm|-|2|-",
            self.unit)
        self.assertIn("TBM:ORDER_WHO|craft|3860|Iron Buckle|2|-|165|Goldie:165:3:3:0;Silver:140:1:3:1",
            self.unit)
        self.assertIn("TBM:ORDER_ACCESS|2|Raider|1,2|-|1|0", self.unit)
        self.assertIn("TBM:ORDER_PREVIEW|Goldie|craft|3860|Iron Buckle|5|5|-|-|2319:8:3:1:2:2:Ashenvale:1:0:1:1|40|-|-",
            self.unit)

    def test_access_bank_gold_bit(self):
        self.assertIn("bool bankGold", self.access_h)
        self.assertIn("GR_RIGHT_WITHDRAW_GOLD", self.access_cpp)
        self.assertIn("`rights` FROM `guild_rank`", self.access_cpp)


if __name__ == "__main__":
    unittest.main(verbosity=2)
