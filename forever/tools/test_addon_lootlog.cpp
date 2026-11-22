// Standalone test of the `lootlog` addon verb's core-free half
// (forever/src/host/TbAddonLootLogPure.h): argument parsing, TBM:LOOT_*
// formatting, TB-exact field order and the 250-byte fit.
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_addon_lootlog.cpp -o /tmp/test_addon_lootlog && /tmp/test_addon_lootlog
#include "forever/src/host/TbAddonLootLogPure.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace TortoiseBots::lootlog;

int main()
{
    int checks = 0;
    auto ok = [&](bool cond)
    {
        assert(cond);
        ++checks;
    };

    // 1. Argument parsing: empty means first page, piece@offset resumes.
    {
        ParsedLootLogArgs p = ParseLootLogArgs("");
        ok(p.valid && p.cursor.piece.empty() && p.cursor.offset == 0);
        p = ParseLootLogArgs("   ");
        ok(p.valid && p.cursor.piece.empty());
        p = ParseLootLogArgs("bot_events_2026-10-27_09-00-00.csv.gz@123");
        ok(p.valid && p.cursor.piece == "bot_events_2026-10-27_09-00-00.csv.gz" && p.cursor.offset == 123);
        ok(!ParseLootLogArgs("no-offset").valid);
        ok(!ParseLootLogArgs("piece@12x").valid);
        ok(!ParseLootLogArgs("piece@").valid);
        ok(std::string(LootLogUsage()) == "Usage: lootlog [cursor]");
    }

    // 2. StoreLootAction row: TB-exact field order ts|bot|event|info2|item|sourceType|source|quality.
    {
        LootWireRow row{"2026-10-27 10:00:00+00", "Dudette", "StoreLootAction", "122", "Tough Jerky", "corpse", "Kobold Vermin", "1"};
        ok(FormatLootLine(row) ==
            "TBM:LOOT|2026-10-27 10:00:00+00|Dudette|StoreLootAction|122|Tough Jerky|corpse|Kobold Vermin|1");
    }

    // 3. LootMoney row: copper rides in <item>, the corpse entry in <info2>, quality stays empty.
    {
        LootWireRow row{"2026-10-27 10:00:01+00", "Dudette", "LootMoney", "1234", "137", "corpse", "Kobold Vermin", ""};
        std::string line = FormatLootLine(row);
        ok(line == "TBM:LOOT|2026-10-27 10:00:01+00|Dudette|LootMoney|1234|137|corpse|Kobold Vermin|");
        ok(line.size() <= 250);
    }

    // 4. LootRoll row: vote:itemId:reason rides in <info2>, the item name in <item>.
    {
        LootWireRow row{"2026-10-27 10:00:02+00", "Dudette", "LootRoll", "need:2586:for equiping as upgrade.", "Gamemaster's Robe", "", "", ""};
        ok(FormatLootLine(row) ==
            "TBM:LOOT|2026-10-27 10:00:02+00|Dudette|LootRoll|need:2586:for equiping as upgrade.|Gamemaster's Robe|||");
    }

    // 5. The 250-byte fit trims a long LootRoll reason first, keeping vote and item id.
    {
        LootWireRow row{"2026-10-27 10:00:02+00", "Dudette", "LootRoll", "need:2586:" + std::string(300, 'r'), "Gamemaster's Robe", "", "", ""};
        std::string line = FormatLootLine(row);
        ok(line.size() <= 250);
        ok(line.find("TBM:LOOT|2026-10-27 10:00:02+00|Dudette|LootRoll|need:2586:") == 0);
        ok(line.find("|Gamemaster's Robe|||") != std::string::npos);
    }

    // 6. The 250-byte fit trims long display names on other rows, never dropping the row.
    {
        LootWireRow row{"2026-10-27 10:00:00+00", "Dudette", "StoreLootAction", "122", std::string(200, 'i'), "corpse", std::string(200, 's'), "1"};
        std::string line = FormatLootLine(row);
        ok(line.size() <= 250);
        ok(line.find("TBM:LOOT|2026-10-27 10:00:00+00|Dudette|StoreLootAction|122|") == 0);
    }

    // 7. Field cleaning is TB's ProtocolSafe: '|' becomes a blank, empty stays empty.
    {
        ok(LootSafe("a|b\rc\nd") == "a b c d");
        ok(LootSafe("").empty());
        LootWireRow row{"t", "b", "StoreLootAction", "1", "a|b", "corpse", "", "0"};
        ok(FormatLootLine(row) == "TBM:LOOT|t|b|StoreLootAction|1|a b|corpse||0");
    }

    // 8. Page framing, including the pruned-piece cursor.
    {
        ok(FormatLootBegin(40, "piece@12") == "TBM:LOOT_BEGIN|40|piece@12");
        ok(FormatLootEnd("piece@12") == "TBM:LOOT_END|piece@12");
        ok(FormatLootBegin(0, "") == "TBM:LOOT_BEGIN|0|");
        ok(FormatLootEnd("") == "TBM:LOOT_END|");
        ok(FormatLootBegin(0, "expired") == "TBM:LOOT_BEGIN|0|expired");
        ok(FormatLootEnd("expired") == "TBM:LOOT_END|expired");
    }

    // 9. End to end: a bot_events.csv line parses and formats into the row the addon files.
    {
        ai::LootHistoryRow parsed;
        ok(ai::LootHistoryPolicy::ParseLootHistoryRow(
            "2026-10-27 10:00:00+00,Dudette,StoreLootAction,\"POINT(1 2)\",1,2,60.000000,"
            "\"Tough Jerky\",\"122\",\"corpse\",\"Kobold Vermin\",\"1\"",
            parsed));
        std::string line = FormatLootLine(RowFromHistory(parsed));
        ok(line == "TBM:LOOT|2026-10-27 10:00:00+00|Dudette|StoreLootAction|122|Tough Jerky|corpse|Kobold Vermin|1");
    }

    std::cout << "test_addon_lootlog: " << checks << " checks passed\n";
    return 0;
}
