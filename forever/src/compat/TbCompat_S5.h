// ForeverCore side of the both-core helpers of slice 5 (the plan loader: ai/playerbot/DungeonPlanMgr.cpp, BossTacticsMgr.cpp).
// Owned by package P5 of the slice-5 work: no other package edits this file. Turtle side: ai/cmangos-compat-s5.h (same names and
// signatures, each is the call the loader always made). Notes: forever/docs/RUN-S5-P-NOTES.md (every helper, with the file:line
// of the core call it makes).
//
// Until the two shims include this pair (forever-compat-shim.h / ai/cmangos-compat-shim.h, see "Needs at merge" in the notes), the
// two loader files include it themselves through the one name both cores have: `cmangos-compat-s5.h` (here: the forwarder
// forever/src/compat/cmangos-compat-s5.h, which comes first on the include path; on the Turtle core: ai/cmangos-compat-s5.h).
#pragma once

#include <set>
#include <string>
#include <vector>

// ===============================================================================================================
// WORLD DATABASE READS (DungeonPlanMgr.cpp: five table reads and the revision query; BossTacticsMgr.cpp: one table read)
// ===============================================================================================================

// The result of a world-database query the way the loader holds it. T: the core's Query hands over a raw QueryResult* the caller
// frees, held by a `std::unique_ptr<QueryResult>` (the original expression, see ai/cmangos-compat-s5.h). F: Query already returns
// the shared `QueryResult` (Database/QueryResult.h), `result->Fetch()` and `result->NextRow()` read the same way, and a null result
// is a failed or empty query on both.
//
// ONE more rule on F, and why. ForeverCore's connection ABORTS THE SERVER (after a 10 s sleep) on a query that names a missing table
// or column (MySQLConnection.cpp:596-601: ER_BAD_FIELD_ERROR / ER_NO_SUCH_TABLE "database structure is not up to date"). Turtle
// logs the error and hands back a null result, and the loader relies on it: a database without `ai_dungeon_leg_need` loads the plans
// with no needs (DungeonPlanMgr.cpp, "an old database without the table"). So on F the helper first asks information_schema
// whether the table has every column the query names between SELECT and FROM (the backticked names) and, when it does not, logs
// one error line and answers a null result: the same "no rows" the Turtle loader already handles, never an abort. The check is
// for the single-table `SELECT ... FROM `table`` form every loader query has; any other form is refused the same way (do less).
// Cost: one extra query per call, at boot and at `.tb plan reload` only.
inline bool WorldSchemaHasCompat(std::string const& table, std::set<std::string> const& columns)
{
    std::string sql = "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" + table + "'";
    if (!columns.empty())
    {
        sql += " AND COLUMN_NAME IN (";
        bool first = true;
        for (std::string const& column : columns)
        {
            sql += (first ? "'" : ", '") + column + "'";
            first = false;
        }
        sql += ")";
    }
    QueryResult count = WorldDatabase.Query(sql.c_str());
    if (!count)
        return false;
    uint64 const found = count->Fetch()[0].GetUInt64();
    return columns.empty() ? found > 0 : found == columns.size();
}

inline QueryResult WorldQueryCompat(char const* sql)
{
    std::string const text(sql ? sql : "");
    std::size_t const from = text.find(" FROM `");
    std::size_t const tableEnd = from == std::string::npos ? std::string::npos : text.find('`', from + 7);
    if (from == std::string::npos || tableEnd == std::string::npos || text.compare(0, 7, "SELECT ") != 0)
    {
        LegacyLogCompat().outError("TortoiseBots: world query refused (not a single-table SELECT ... FROM `table`): %s", text.c_str());
        return QueryResult();
    }
    std::string const table = text.substr(from + 7, tableEnd - (from + 7));
    std::set<std::string> columns;
    for (std::size_t at = 0; at < from;)
    {
        std::size_t const open = text.find('`', at);
        if (open == std::string::npos || open >= from)
            break;
        std::size_t const close = text.find('`', open + 1);
        if (close == std::string::npos || close >= from)
            break;
        columns.insert(text.substr(open + 1, close - open - 1));
        at = close + 1;
    }
    if (!WorldSchemaHasCompat(table, columns))
    {
        LegacyLogCompat().outError("TortoiseBots: world table `%s` is missing or lacks a column the plan loader reads: not read (the module's world SQL is not applied)", table.c_str());
        return QueryResult();
    }
    return WorldDatabase.Query(sql);
}

// The text of a VARCHAR/TEXT field as a std::string. T `Field::GetCppString()` (the 1.12 core's name), F `Field::GetString()` (Field.h:119).
// GetUInt8/16/32/64 and GetFloat exist under the same names on both cores, so they are not wrapped.
inline std::string FieldTextCompat(Field& field) { return field.GetString(); }
