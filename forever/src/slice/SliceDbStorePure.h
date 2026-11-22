// Piece c-store (TBF #133): pure row/SQL builders for the VALUES-only ai_playerbot_db_store persistence.
//
// The engine's saved values (AiObjectContext::Save) are "name>data" strings, one per saved value, where `name`
// carries the qualifier ("manual saved string::totem order earth"). TB stores each string as one row with
// `key` = 'value' (PlayerbotDbStore::Save); Load reads the `key` = 'value' rows back into the context. Strategy
// rows (`co`/`nc`/`dead`/`react`) are #135's and are never written or applied here.
//
// No core or legacy header may be included here: forever/tools/test_aistore_pure.cpp compiles this file alone.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
// <utility> for the key/value pairs BuildLoadPlan takes.
#include <utility>
namespace ai
{
namespace slice
{
namespace aistore
{

// The one key this store writes and the only key it reads back (TB: PlayerbotDbStore::SaveValue(guid, preset, "value", row)).
inline char const* ValueKey() { return "value"; }

// TB row shape. Encode is what AiObjectContext::Save produces; Parse is what its Load accepts: a row that does not
// split into one or two parts (data holding '>') is dropped on load. Returns false for a row to skip.
inline std::string EncodeRow(std::string const& name, std::string const& saved) { return name + ">" + saved; }

inline bool ParseRow(std::string const& row, std::string& name, std::string& saved)
{
    std::string::size_type at = row.find('>');
    if (at == std::string::npos)
    {
        name = row;
        saved.clear();
        return true;
    }
    if (row.find('>', at + 1) != std::string::npos)
        return false;
    name = row.substr(0, at);
    saved = row.substr(at + 1);
    return true;
}

// Every interpolated string literal goes through `escape` first (the caller passes CharacterDatabase.EscapeString);
// the guid is numeric and never quoted. Statements name only ai_playerbot_db_store and run after the schema probe.
inline std::string SelectValuesSql(uint32_t guid, std::string preset, std::function<void(std::string&)> const& escape)
{
    escape(preset);
    return "SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " AND `preset` = '" + preset + "'";
}

struct SaveStatements
{
    std::string deleteSql;                  // clears this bot+preset's value rows; strategy rows are left alone (#135)
    std::vector<std::string> insertSql;     // one INSERT per value row, same order as the context's Save()
};

inline SaveStatements BuildSaveStatements(uint32_t guid, std::string preset, std::vector<std::string> const& rows,
    std::function<void(std::string&)> const& escape)
{
    escape(preset);
    SaveStatements out;
    out.deleteSql = "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) + " AND `preset` = '" +
        preset + "' AND `key` = 'value'";
    std::string const head = "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (" +
        std::to_string(guid) + ", '" + preset + "', 'value', '";
    for (std::string row : rows)
    {
        escape(row);
        out.insertSql.push_back(head + row + "')");
    }
    return out;
}

inline std::string FormatStrategies(std::vector<std::string> const& strategies)
{
    std::string out;
    for (std::string const& s : strategies)
    {
        if (!out.empty())
            out += ',';
        out += '+';
        out += s;
    }
    return out;
}

// --- Strategy snapshot load plan ---
//
// The saved co/nc/dead/react rows REPLACE the kit-built engines: Load clears
// an engine whose own row was read and replays the saved rows verbatim, with
// no baseline added first. The slice kits hold no chat/quest (nc) or chat (co),
// so a baseline would invent strategies the kit never had, and it would re-add
// a strategy the player explicitly removed (`nc -loot` must survive a relog).
// TB's baseline guards years-old snapshots; this table is new and kit changes
// invalidate snapshots on the role/spec path, so verbatim replace is exact.
// A bot with no saved rows gets an empty plan: its kit stands untouched.
struct LoadOp
{
    std::string key;    // one of co, nc, dead, react
    std::string value;  // the saved row, verbatim ("+a,+b")
};

struct LoadPlan
{
    bool clearCombat = false;
    bool clearNonCombat = false;
    std::vector<LoadOp> ops;
};

// Drops an empty-qualifier custom attach (`+custom`, `+custom::`) from one saved
// strategy row. Attaching it stores the engine entry under the name `custom::`,
// which no later lookup of `custom` finds again; it can do nothing (no lines
// can be stored under the empty name) and only pollutes snapshots.
inline std::string DropEmptyCustomAttach(std::string const& row)
{
    std::string out;
    std::size_t from = 0;
    while (true)
    {
        std::size_t comma = row.find(',', from);
        std::string piece = row.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
        std::string bare = piece;
        if (!bare.empty() && (bare[0] == '+' || bare[0] == '-' || bare[0] == '~'))
            bare.erase(0, 1);
        bool drop = bare == "custom";
        if (!drop && bare.size() >= 8 && bare.compare(0, 8, "custom::") == 0)
        {
            std::string qualifier = bare.substr(8);
            std::size_t first = qualifier.find_first_not_of(" \t");
            std::size_t last = qualifier.find_last_not_of(" \t");
            drop = first == std::string::npos || last == std::string::npos;
        }
        if (!drop)
        {
            if (!out.empty())
                out += ',';
            out += piece;
        }
        if (comma == std::string::npos)
            break;
        from = comma + 1;
    }
    return out;
}

inline LoadPlan BuildLoadPlan(std::vector<std::pair<std::string, std::string>> const& rows)
{
    LoadPlan plan;
    for (std::pair<std::string, std::string> const& row : rows)
    {
        if (row.first == "co")
            plan.clearCombat = true;
        else if (row.first == "nc")
            plan.clearNonCombat = true;
        else if (row.first != "dead" && row.first != "react")
            continue;
        std::string value = DropEmptyCustomAttach(row.second);
        if (value.empty())
            continue;
        plan.ops.push_back({ row.first, value });
    }
    return plan;
}

struct StrategyRow
{
    std::string key;
    std::string value;
};

inline SaveStatements BuildSaveAllStatements(uint32_t guid, std::string preset,
    std::vector<std::string> const& valueRows, std::vector<StrategyRow> const& stratRows,
    std::function<void(std::string&)> const& escape)
{
    escape(preset);
    SaveStatements out;
    std::string const guidStr = std::to_string(guid);
    out.deleteSql = "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + guidStr + " AND `preset` = '" + preset + "'";
    std::string const prefix = "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (" +
        guidStr + ", '" + preset + "', '";
    for (std::string row : valueRows)
    {
        escape(row);
        out.insertSql.push_back(prefix + "value', '" + row + "')");
    }
    for (StrategyRow const& sr : stratRows)
    {
        std::string key = sr.key;
        std::string val = sr.value;
        escape(key);
        escape(val);
        out.insertSql.push_back(prefix + key + "', '" + val + "')");
    }
    return out;
}

inline std::string BuildResetSql(uint32_t guid, std::string preset, std::function<void(std::string&)> const& escape)
{
    escape(preset);
    return "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) + " AND `preset` = '" + preset + "'";
}

inline std::string BuildInvalidateStrategySnapshotsSql(uint32_t guid)
{
    return "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " AND `key` IN ('co','nc','dead','react')";
}

inline std::string BuildSelectValueOnlySql(uint32_t guid, std::string preset,
    std::function<void(std::string&)> const& escape)
{
    escape(preset);
    return "SELECT `value` FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " AND `preset` = '" + preset + "' AND `key` = 'value'";
}

inline std::string BuildDeleteAllValuesSql(uint32_t guid)
{
    return "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) + " AND `key` = 'value'";
}

inline std::string BuildSelectDistinctPresetsSql(uint32_t guid)
{
    return "SELECT DISTINCT `preset` FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid);
}

}
}
}
