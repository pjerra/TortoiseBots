// The characters-database side of the module's schema guard (the commander binding store, forever/src/host/cmd/TbCmdBindingStore.cpp; package P2,
// forever/docs/CMD-P2-NOTES.md). TbCompat_S5.h is the world-database guard; ForeverCore aborts the world on a statement naming a missing table or column
// on ANY pool (MySQLConnection.cpp: ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), so the store's statements are made here too.
//
// Split: the store parses the query form and its table (compat/TbSqlNames.h) and checks the row's columns before every write; this file does the raw
// information_schema probe and the guarded SELECT / synchronous statement. These are the only raw characters statements in forever/src: a caller never
// passes unchecked text (tools/test_forever_no_direct_queries.py counts the three calls here).
#pragma once

#include "DatabaseEnv.h"

#include <set>
#include <string>

inline bool CharacterSchemaHasCompat(std::string const& table, std::set<std::string> const& columns)
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
    QueryResult count = CharacterDatabase.Query(sql.c_str());
    if (!count)
        return false;
    uint64 const found = count->Fetch()[0].GetUInt64();
    return columns.empty() ? found > 0 : found == columns.size();
}

// The store has already parsed the SQL (single table, this store's own literal) and checked the columns: this is the statement itself.
inline QueryResult CharacterQueryGuardedCompat(std::string const& sql)
{
    return CharacterDatabase.Query(sql.c_str());
}

// One synchronous statement: a queued one could land after another, and the store reads its effect back at once.
inline void CharacterExecuteGuardedCompat(std::string const& sql)
{
    CharacterDatabase.DirectExecute(sql.c_str());
}
