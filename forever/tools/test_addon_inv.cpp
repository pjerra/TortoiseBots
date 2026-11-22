// Standalone test of the `inv`/`item` addons' core-free half (forever/src/host/TbAddonInvPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_addon_inv.cpp -o /tmp/test_addon_inv && /tmp/test_addon_inv
// The parse ports TB's `.bot inv` / `.bot item` grammar (TortoiseBots/commands/Inventory.cpp:213-228,353-380);
// the INV lines match TB's TBM:INV_* snapshot (Inventory.cpp:158-211) which TBFM Comms.lua parses.
#include "forever/src/host/TbAddonInvPure.h"

#include <cstdio>
#include <string>

namespace inv = TortoiseBots::inv;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestParseInv()
{
    inv::InvRequest r = inv::ParseInvRequest("Garr");
    CHECK(r.ok && r.bot == "garr");

    // Case does not matter; surrounding blanks are trimmed.
    r = inv::ParseInvRequest("  GARR  ");
    CHECK(r.ok && r.bot == "garr");

    CHECK(!inv::ParseInvRequest("").ok);
    CHECK(!inv::ParseInvRequest("   ").ok);
    CHECK(!inv::ParseInvRequest("Garr extra").ok);
    CHECK(inv::ParseInvRequest("").error == "invalid");
    CHECK(inv::ParseInvRequest("Garr extra").message == inv::InvUsage());
}

static void TestParseItem()
{
    inv::ItemRequest r = inv::ParseItemRequest("Garr equip 255 35");
    CHECK(r.ok && r.bot == "garr" && r.op == inv::ItemRequest::Op::Equip);
    CHECK(r.intent == "item equip" && r.hasPos && r.bag == 255 && r.slot == 35 && !r.hasTargetSlot);

    r = inv::ParseItemRequest("Garr unequip 255 15");
    CHECK(r.ok && r.op == inv::ItemRequest::Op::Unequip && r.intent == "item unequip");

    r = inv::ParseItemRequest("Garr give 19 3");
    CHECK(r.ok && r.op == inv::ItemRequest::Op::Give && r.intent == "item give");

    // Slot words only ride equip; trade takes no position.
    r = inv::ParseItemRequest("garr EQUIP 255 35 MainHand");
    CHECK(r.ok && r.hasTargetSlot && r.targetSlot == 15);
    r = inv::ParseItemRequest("garr equip 255 35 finger2");
    CHECK(r.ok && r.hasTargetSlot && r.targetSlot == 11);
    r = inv::ParseItemRequest("garr equip 255 35 trinket1");
    CHECK(r.ok && r.hasTargetSlot && r.targetSlot == 12);
    r = inv::ParseItemRequest("Garr trade");
    CHECK(r.ok && r.op == inv::ItemRequest::Op::Trade && !r.hasPos && r.intent == "item trade");
}

static void TestParseItemRejects()
{
    CHECK(!inv::ParseItemRequest("").ok);
    CHECK(!inv::ParseItemRequest("Garr").ok);
    CHECK(!inv::ParseItemRequest("Garr sell 255 35").ok); // no such op
    CHECK(inv::ParseItemRequest("Garr sell 255 35").intent == "item ?");
    CHECK(!inv::ParseItemRequest("Garr equip").ok);          // missing pos
    CHECK(!inv::ParseItemRequest("Garr equip 255").ok);      // half pos
    CHECK(!inv::ParseItemRequest("Garr equip x 35").ok);
    CHECK(!inv::ParseItemRequest("Garr equip 255 999").ok);  // > 255
    CHECK(!inv::ParseItemRequest("Garr equip 256 1").ok);
    CHECK(!inv::ParseItemRequest("Garr give 255 35 wrist").ok); // no such slot word
    CHECK(inv::ParseItemRequest("Garr give 255 35 wrist").error == "invalid");
    // Case-insensitive op and bot.
    inv::ItemRequest r = inv::ParseItemRequest("GARR GIVE 255 35");
    CHECK(r.ok && r.bot == "garr" && r.op == inv::ItemRequest::Op::Give);
}

static void TestWireLines()
{
    // Exact TB shapes (Inventory.cpp:158-211): the addon splits on '|' and
    // requires 2 fields after INV_BEGIN, 4-5 after INV_EQ, 5 after INV_ITEM.
    CHECK(inv::FormatInvBegin("garr", 12345) == "TBM:INV_BEGIN|garr|12345");
    CHECK(inv::FormatInvEq(15, 19019, 80, 100) == "TBM:INV_EQ|15|19019|80|100");
    CHECK(inv::FormatInvEqBag(19, 4498, 16) == "TBM:INV_EQ|19|4498|0|0|16");
    CHECK(inv::FormatInvItem(255, 35, 2589, 5, "eu") == "TBM:INV_ITEM|255|35|2589|5|eu");
    CHECK(inv::FormatInvItem(19, 3, 6948, 1, "-") == "TBM:INV_ITEM|19|3|6948|1|-");
    CHECK(inv::FormatInvEnd("garr", 42, 60) == "TBM:INV_END|garr|42|60");

    // Flags: e/u/b or "-" (the addon greps each letter).
    CHECK(inv::InvFlags(false, false, false) == "-");
    CHECK(inv::InvFlags(true, false, false) == "e");
    CHECK(inv::InvFlags(true, true, false) == "eu");
    CHECK(inv::InvFlags(false, false, true) == "b");
    CHECK(inv::InvFlags(true, true, true) == "eub");

    CHECK(inv::ItemScope("garr") == "bot:garr");
    CHECK(std::string(inv::ItemOpWord(inv::ItemRequest::Op::Give)) == "give");
    CHECK(std::string(inv::ItemOpWord(inv::ItemRequest::Op::Trade)) == "trade");
}

int main()
{
    TestParseInv();
    TestParseItem();
    TestParseItemRejects();
    TestWireLines();
    if (g_fail == 0)
        std::printf("test_addon_inv: OK (%d checks)\n", g_checks);
    return g_fail ? 1 : 0;
}
