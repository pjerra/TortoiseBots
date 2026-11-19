// ForeverCore side of the both-core helpers of slice 5 (the plan loader: ai/playerbot/DungeonPlanMgr.cpp, BossTacticsMgr.cpp).
// Owned by package P5 of the slice-5 work: no other package edits this file. Turtle side: ai/cmangos-compat-s5.h (same names and
// signatures, each is the call the loader always made). Notes: forever/docs/RUN-S5-P-NOTES.md (every helper, with the file:line
// of the core call it makes).
//
// The two shims include this pair (forever-compat-shim.h / ai/cmangos-compat-shim.h); the loader files need no include of their own.
#pragma once

#include "TbSqlNames.h"

// Core headers (a host file includes this header on its own, without the legacy shim and its renaming macros; the shim path has them already).
#include "DatabaseEnv.h"
#include "Log.h"

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

// `schemaMissing` (optional): set true when the guard refused the query because the table or a named column does not exist (or the query form is not
// the single-table one), false otherwise; a null result with `*schemaMissing == false` is an empty or failed query. The message does not say what to
// apply: whether a missing table is fatal is the caller's rule (the loader tolerates a missing ai_dungeon_leg_need, the host's unit table is optional).
inline QueryResult WorldQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    std::string const text(sql ? sql : "");
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(text, table, columns))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: world query refused (not a single-table SELECT ... FROM `table`): {}", text);
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    if (!WorldSchemaHasCompat(table, columns))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: world table `{}` is missing, or lacks a column this query names: not read", table);
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    return WorldDatabase.Query(sql);
}

// The seek_aura reconciler of a plan reload is not registered. Turtle logs it (the boss tactics always register it there); ForeverCore has no boss tactics: nothing to say.
inline void SeekReconcilerMissingCompat() {}

// The text of a VARCHAR/TEXT field as a std::string. T `Field::GetCppString()` (the 1.12 core's name), F `Field::GetString()` (Field.h:119).
// GetUInt8/16/32/64 and GetFloat exist under the same names on both cores, so they are not wrapped.
inline std::string FieldTextCompat(Field& field) { return field.GetString(); }
