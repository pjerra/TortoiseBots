// The `lootlog` addon verb's core-free half (forever/src/host/TbAddonLootLog.cpp):
// parses `lootlog [cursor]` and builds the TBM:LOOT_* reply lines TB sends
// (TortoiseBots/commands/Inventory.cpp HandleLootLog): TBM:LOOT_BEGIN|<n>|
// <nextCursor>, one TBM:LOOT row per message, TBM:LOOT_END|<nextCursor>.
// No core or legacy-engine includes; tested standalone by
// forever/tools/test_addon_lootlog.cpp.
#pragma once

// The repo-root path, like TbAddonChoicePure.h: the option-OFF build has no `ai/` include dir (CMakeLists.txt adds it to the slice build only).
#include "ai/playerbot/policy/loot/LootHistoryPolicy.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace lootlog
{

inline char const* LootLogUsage()
{
    return "Usage: lootlog [cursor]";
}

// This stream's field cleaning (commands/CommandShared.cpp ProtocolSafe):
// '|' CR LF become blanks. Unlike the ACK/ERR Safe, empty stays empty: the
// addon's nil for a missing source or quality.
inline std::string LootSafe(std::string_view field)
{
    std::string out(field);
    for (char& c : out)
        if (c == '|' || c == '\r' || c == '\n')
            c = ' ';
    return out;
}

struct ParsedLootLogArgs
{
    bool valid = false;
    ai::LootHistoryCursor cursor;
};

inline ParsedLootLogArgs ParseLootLogArgs(std::string_view args)
{
    ParsedLootLogArgs out;
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t'))
        args.remove_prefix(1);
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t'))
        args.remove_suffix(1);
    out.valid = ai::LootHistoryPolicy::DecodeLootCursor(std::string(args), out.cursor);
    return out;
}

inline std::string FormatLootBegin(std::size_t rows, std::string_view nextCursor)
{
    std::string line = "TBM:LOOT_BEGIN|";
    line += std::to_string(rows);
    line.push_back('|');
    line.append(nextCursor);
    return line;
}

inline std::string FormatLootEnd(std::string_view nextCursor)
{
    std::string line = "TBM:LOOT_END|";
    line.append(nextCursor);
    return line;
}

struct LootWireRow
{
    std::string timestamp;
    std::string bot;
    std::string event;
    std::string info2;
    std::string item;
    std::string sourceType;
    std::string source;
    std::string quality;
};

// One TBM:LOOT line for a parsed history row, TB-exact: field order
// ts|bot|event|info2|item|sourceType|source|quality with the 250-byte fit
// (LootHistoryPolicy.h: LootRoll rows trim the vote reason first, every other
// row trims the two display names).
inline std::string FormatLootLine(LootWireRow row)
{
    row.timestamp = LootSafe(row.timestamp);
    row.bot = LootSafe(row.bot);
    row.event = LootSafe(row.event);
    row.info2 = LootSafe(row.info2);
    row.item = LootSafe(row.item);
    row.sourceType = LootSafe(row.sourceType);
    row.source = LootSafe(row.source);
    row.quality = LootSafe(row.quality);

    if (row.event == "LootRoll")
    {
        // Seven separators ride the wire (ts|bot|event|info2|item|type|source|quality);
        // TB's reader counts six here and can emit 251 B on a very long vote
        // reason. Same grammar, enforced 250 B budget.
        std::size_t const fixedBytes = 9 + row.timestamp.size() + 1 + row.bot.size() + 1 +
            row.event.size() + 1 + 1 + row.sourceType.size() + 1 + row.source.size() + 1 + row.quality.size() + 1;
        ai::LootHistoryPolicy::FitLootRollWireLine(row.info2, row.item, fixedBytes);
    }
    else
    {
        std::string const fixed = "TBM:LOOT|" + row.timestamp + "|" + row.bot + "|" +
            row.event + "|" + row.info2 + "||" + row.sourceType + "||" + row.quality;
        ai::LootHistoryPolicy::FitLootWireLine(row.item, row.source, fixed.size());
    }

    return "TBM:LOOT|" + row.timestamp + "|" + row.bot + "|" + row.event + "|" +
        row.info2 + "|" + row.item + "|" + row.sourceType + "|" + row.source + "|" + row.quality;
}

inline LootWireRow RowFromHistory(ai::LootHistoryRow const& row)
{
    LootWireRow wire;
    wire.timestamp = row.timestamp;
    wire.bot = row.bot;
    wire.event = row.event;
    wire.info2 = row.info2;
    wire.item = row.info1;
    wire.sourceType = row.info3;
    wire.source = row.info4;
    wire.quality = row.info5;
    return wire;
}

} // namespace lootlog
} // namespace TortoiseBots
