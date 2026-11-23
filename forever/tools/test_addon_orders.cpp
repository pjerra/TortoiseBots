// Standalone test of the addon order window rule (forever/src/host/TbAddonOrdersPure.h).
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_addon_orders.cpp -o /tmp/test_addon_orders && /tmp/test_addon_orders
//
// The expected lines are copied from the addon's own parser tests, so the
// shapes are pinned to what TortoiseBotsForeverManager already reads:
//   tests/regression.lua:3233,3315,3353,3400  TBM:ORDER
//   tests/regression.lua:3760,3773,3780       TBM:ORDER_ACCESS
//   tests/geometry_tests.lua:1719             TBM:ORDER_ITEM
//   tests/regression.lua:5392                 TBM:ORDER_WHO
//   tests/geometry_tests.lua:1793             TBM:ORDER_PREVIEW
//   tests/regression.lua:3959,3988-3991       TBM:ORDER_STEP(S)
#include "host/TbAddonOrdersPure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace orders = TortoiseBots::addonorders;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; if ((a) != (b)) { ++g_fail; std::printf("FAIL %s:%d:\n  got: %s\n  want: %s\n", __FILE__, __LINE__, std::string(a).c_str(), std::string(b).c_str()); } } while (0)

int main()
{
    // ORDER field order, the modern 18-field line. The addon sample at
    // tests/regression.lua:3400 predates the trailing bring field; the current
    // parser reads it as field 18 (BotOrder.lua:1783-1801).
    // case: order_line_field_order
    {
        orders::OrderView v;
        v.bot = "Goldie";
        v.type = "farm";
        v.itemId = 2318;
        v.item = "Light Leather";
        v.have = 10;
        v.want = 10;
        v.state = "working";
        v.reason = "-";
        v.zone = "Ashenvale";
        v.difficulty = "-";
        v.hours = "-";
        v.keep = "keep";
        v.parent = "-";
        v.goldCap = "-";
        v.step = "farm";
        v.need = "-";
        v.carry = 2;
        v.bring = "-";
        CHECK_EQ(orders::OrderLine(v),
            "TBM:ORDER|Goldie|farm|2318|Light Leather|10|10|working|-|Ashenvale|-|-|keep|-|-|farm|-|2|-");
    }

    // The need list keeps its "id:count;..." token (tests/regression.lua:3315).
    // case: order_line_need_and_gold_cap
    {
        orders::OrderView v;
        v.bot = "Goldie";
        v.type = "craft";
        v.itemId = 2318;
        v.item = "Light Leather";
        v.have = 0;
        v.want = 10;
        v.state = "waiting";
        v.reason = "needitems";
        v.goldCap = "10000";
        v.step = "need";
        v.need = "2319:4;2320:2";
        CHECK_EQ(orders::OrderLine(v),
            "TBM:ORDER|Goldie|craft|2318|Light Leather|0|10|waiting|needitems|-|-|-|-|-|10000|need|2319:4;2320:2|0|-");
    }

    // ACCESS: tests/geometry_tests.lua:1778 and the disabled form at
    // tests/regression.lua:3773.
    // case: access_line
    {
        CHECK_EQ(orders::OrderBegin(2), "TBM:ORDER_BEGIN|2");
        CHECK_EQ(orders::OrderEnd(), "TBM:ORDER_END");
        orders::AccessView v;
        v.rank = "2";
        v.rankName = "Raider";
        v.tabsOk = "1,2";
        v.tabsDenied = "-";
        v.gold = true;
        v.canSetRank = false;
        CHECK_EQ(orders::AccessLine(v), "TBM:ORDER_ACCESS|2|Raider|1,2|-|1|0");
        orders::AccessView off;
        CHECK_EQ(orders::AccessLine(off), "TBM:ORDER_ACCESS|-|-|-|-|0|0");
    }

    // ITEM: the addon sample (tests/geometry_tests.lua:1719) plus the category
    // field the current parser reads (BotOrder.lua:65,1519-1539).
    // case: item_line
    {
        orders::ItemView v;
        v.itemId = 2318;
        v.item = "Light Leather";
        v.quality = 1;
        v.sources = "drop,skin";
        v.minLevel = 5;
        v.skill = "393:1";
        v.verdict = "ok";
        v.zone = "Ashenvale";
        v.chance = 70;
        v.category = "-";
        CHECK_EQ(orders::ItemLine(v),
            "TBM:ORDER_ITEM|2318|Light Leather|1|drop,skin|5|393:1|ok|Ashenvale|70|-|-");
    }

    // WHO: exact sample from tests/regression.lua:5392. The addon wants the
    // busy flag as 0/1 (BotOrder.lua:1406-1449).
    // case: who_line
    {
        orders::WhoView v;
        v.kind = "craft";
        v.itemId = 3860;
        v.item = "Iron Buckle";
        v.quality = "2";
        v.reqSkill = 165;
        v.bots = { { "Goldie", 165, 3, 3, false }, { "Silver", 140, 1, 3, true } };
        CHECK_EQ(orders::WhoLine(v),
            "TBM:ORDER_WHO|craft|3860|Iron Buckle|2|-|165|Goldie:165:3:3:0;Silver:140:1:3:1");
    }

    // PREVIEW: exact sample from tests/geometry_tests.lua:1793.
    // case: preview_line
    {
        orders::PreviewView v;
        v.bot = "Goldie";
        v.kind = "craft";
        v.itemId = 3860;
        v.item = "Iron Buckle";
        v.want = 5;
        v.missing = 5;
        v.reagents = { { 2319, 8, 3, 1, 2, 2, "Ashenvale", 1, 0, 1, 1 } };
        v.minutes = 40;
        CHECK_EQ(orders::PreviewLine(v),
            "TBM:ORDER_PREVIEW|Goldie|craft|3860|Iron Buckle|5|5|-|-|2319:8:3:1:2:2:Ashenvale:1:0:1:1|40|-|-");
    }

    // The `.tb` read grammar is decoded before reshaping: percent escapes and
    // `<kind>+` continuation lines (TbH3.h:83-139).
    // case: reply_decode_and_continuation
    {
        std::vector<orders::Record> const reply = orders::ParseReply({
            "orderitem bot=Goldie item=3860 name=Iron%20Buckle quality=2 type=craft",
            "orderitem+ sources=craft verdict=ok cat=other",
        });
        CHECK_EQ(std::to_string(reply.size()), "1");
        CHECK_EQ(orders::Field(reply[0], "name"), "Iron Buckle");
        CHECK_EQ(orders::Field(reply[0], "verdict"), "ok");
        CHECK_EQ(orders::Field(reply[0], "cat"), "other");
        CHECK(orders::ParseReply({ "error code=no-bot cmd=guildorder msg=No%20such%20bot" })[0].kind == "error");
    }

    // Search items: the `.tb` orderitem rows become one ORDER_ITEMS pair.
    // case: items_from_reply
    {
        std::vector<orders::Record> const reply = orders::ParseReply({
            "guildorder-begin",
            "orderitem bot=Goldie item=3860 name=Iron%20Buckle quality=2 type=craft sources=craft verdict=ok cat=other",
            "guildorder-end bot=Goldie found=1",
        });
        std::vector<std::string> const lines = orders::ItemLines(reply);
        CHECK(lines.size() == 3);
        CHECK_EQ(lines[0], "TBM:ORDER_ITEMS_BEGIN|1");
        CHECK_EQ(lines[1], "TBM:ORDER_ITEM|3860|Iron Buckle|2|craft|0|-|ok|-|0|-|other");
        CHECK_EQ(lines[2], "TBM:ORDER_ITEMS_END");
    }

    // Who rows: `.tb` busy yes/no becomes 0/1.
    // case: who_from_reply
    {
        std::vector<orders::Record> const reply = orders::ParseReply({
            "orderwho guild=1 item=3860 name=Iron%20Buckle type=craft quality=2 cat=other bots=Goldie:165:3:3:yes;Silver:140:1:3:no",
            "guildorder-end guild=1 found=1",
        });
        std::vector<std::string> const lines = orders::WhoLines(reply);
        CHECK(lines.size() == 3);
        CHECK_EQ(lines[0], "TBM:ORDER_WHO_BEGIN|craft|1");
        CHECK_EQ(lines[1], "TBM:ORDER_WHO|craft|3860|Iron Buckle|2|-|0|Goldie:165:3:3:1;Silver:140:1:3:0");
        CHECK_EQ(lines[2], "TBM:ORDER_WHO_END");
    }

    // Preview: the `.tb` reagents carry id:need:have; the shortfall rides the
    // farm lane because TBF sources missing reagents itself. The addon keeps
    // the bring ticks it sent (BotOrder.lua:1544-1663).
    // case: preview_from_reply
    {
        std::vector<orders::Record> const reply = orders::ParseReply({
            "orderpreview bot=Goldie type=craft item=3860 name=Iron%20Buckle quality=2 want=5 bags=0 mail=0 have=0 short=5 verdict=ok reagents=2319:8:3",
            "guildorder-end bot=Goldie item=3860",
        });
        std::vector<std::string> const lines = orders::PreviewLines(reply, "-", {});
        CHECK(lines.size() == 1);
        CHECK_EQ(lines[0],
            "TBM:ORDER_PREVIEW|Goldie|craft|3860|Iron Buckle|5|5|-|-|2319:8:3:0:0:5:-:0:0:0:0|0|-|-");
        // A ticked reagent is the player's to bring, so its shortfall moves to
        // the player lane instead of the farm lane.
        std::vector<std::string> const ticked = orders::PreviewLines(reply, "-", { 2319, 2320 });
        CHECK(ticked.size() == 1);
        CHECK_EQ(ticked[0],
            "TBM:ORDER_PREVIEW|Goldie|craft|3860|Iron Buckle|5|5|-|-|2319:8:3:0:0:0:-:0:0:0:5|0|-|2319,2320");
    }

    // Steps: `.tb` orderstep records reshape into the addon's pair, oldest
    // first, with the leg item in the item slot (tests/regression.lua:3959,
    // 3988-3991).
    // case: steps_from_reply
    {
        std::vector<orders::Record> const reply = orders::ParseReply({
            "guildorder-begin",
            "orderstep bot=Goldie age=90 state=working reason=- step=bank order=2318 leg=2319",
            "orderstep bot=Goldie age=30 state=working reason=nobanker step=bank order=2318 leg=2319",
            "guildorder-end bot=Goldie steps=2 dropped=3",
        });
        std::vector<std::string> const lines = orders::StepLines(reply);
        CHECK(lines.size() == 4);
        CHECK_EQ(lines[0], "TBM:ORDER_STEPS_BEGIN|Goldie|2|3");
        CHECK_EQ(lines[1], "TBM:ORDER_STEP|Goldie|90|working|-|bank|2319");
        CHECK_EQ(lines[2], "TBM:ORDER_STEP|Goldie|30|working|nobanker|bank|2319");
        CHECK_EQ(lines[3], "TBM:ORDER_STEPS_END|Goldie");
    }

    // Give grammar: the addon's trailing options in any order (BotOrder.lua:15-19).
    // case: give_args
    {
        orders::GiveArgs args;
        CHECK(orders::ParseGiveArgs("Goldie craft 3860 5 3 keep gold=0 bring=2319,2320", args));
        CHECK_EQ(args.bot, "Goldie");
        CHECK_EQ(args.kind, "craft");
        CHECK_EQ(std::to_string(args.itemId), "3860");
        CHECK_EQ(std::to_string(args.amount), "5");
        CHECK(args.hasHours);
        CHECK_EQ(std::to_string(args.hours), "3");
        CHECK(args.keep);
        CHECK(args.hasGold);
        CHECK_EQ(std::to_string(args.gold), "0");
        CHECK(args.bring.size() == 2);

        orders::GiveArgs inf;
        CHECK(orders::ParseGiveArgs("Goldie farm 2318 10 inf", inf));
        CHECK(inf.hasHours);
        CHECK_EQ(std::to_string(inf.hours), "0");

        orders::GiveArgs noBringOnFarm;
        CHECK(!orders::ParseGiveArgs("Goldie farm 2318 10 bring=2319", noBringOnFarm));
        orders::GiveArgs tooMany;
        CHECK(!orders::ParseGiveArgs("Goldie craft 3860 201", tooMany));
        orders::GiveArgs badHours;
        CHECK(!orders::ParseGiveArgs("Goldie craft 3860 5 25", badHours));
        orders::GiveArgs twice;
        CHECK(!orders::ParseGiveArgs("Goldie craft 3860 5 gold=1 gold=2", twice));
    }

    // Preview grammar keeps the ticked bring list (BotOrder.lua:15-16).
    // case: preview_args
    {
        orders::PreviewArgs args;
        CHECK(orders::ParsePreviewArgs("Goldie preview craft 3860 5 bring=2319,2320", args));
        CHECK_EQ(args.bot, "Goldie");
        CHECK_EQ(args.kind, "craft");
        CHECK_EQ(std::to_string(args.itemId), "3860");
        CHECK(args.bring.size() == 2);
        orders::PreviewArgs noBring;
        CHECK(orders::ParsePreviewArgs("Goldie preview craft 3860 5", noBring));
        CHECK(noBring.bring.empty());
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
