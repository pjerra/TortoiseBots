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

}
}
}
