// ForeverCore side of the both-core helpers of the world slice W0 (the pool: registry and creation). Owned by round W0-A of
// FOREVER-W0-W4-SPEC.md: no other round edits this file. Turtle side: ai/cmangos-compat-wl0.h (same names and signatures).
//
// Two things differ between the cores here:
//  - the character-name check takes a locale on ForeverCore and none on Turtle (`CheckPlayerNameCompat`, called by
//    runtime/BotNameService.cpp, the name service of a created pool character);
//  - the module's own character-database tables need the same information_schema guard as the commander store
//    (compat/TbCompat_CharStore.h): ForeverCore aborts the world on a statement naming a missing table or column
//    (MySQLConnection.cpp: ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), Turtle answers a null result. `PoolCharacterQueryCompat`
//    and `PoolCharacterExecuteCompat` are that guard for the two pool tables and nothing else.
//
#pragma once

#include "ObjectMgr.h"     // CheckPlayerName, CHAR_NAME_SUCCESS, LOCALE_enUS

#include "compat/TbCompat_CharStore.h"
#include "compat/TbSqlNames.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Defined in forever/src/host/pool/TbPoolRegistry.cpp, a TU that sees the core's logger (the guard cannot use the core's log
// macros: under the shim `sLog` is the legacy printf proxy).
void PoolGuardLogCompat(std::string const& message);

// The name check of runtime/BotNameService.cpp, with the core's create-time rules. T `ObjectMgr::CheckPlayerName(name, true)`
// (ObjectMgr.h:1306); F adds the locale (ObjectMgr.h:1610).
inline bool CheckPlayerNameCompat(std::string const& name)
{
    return ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) == CHAR_NAME_SUCCESS;
}

// ===============================================================================================================
// THE POOL TABLES (characters database)
// ===============================================================================================================

// The only two tables this guard may ever name: forever/sql/characters/2026_11_20_01_tb_pool.sql.
inline bool PoolTableCompat(std::string const& table)
{
    return table == "tb_pool_account" || table == "tb_pool_character";
}

// `schemaMissing` is the character-database twin of WorldQueryCompat's: set when the table (or a column the query names) is not
// there, or the query is not the single-table SELECT form the guard understands. A null result then means "no rows", never a
// half-read.
inline QueryResult PoolCharacterQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    std::string const text(sql ? sql : "");
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(text, table, columns) || !PoolTableCompat(table))
    {
        PoolGuardLogCompat("TortoiseBots: pool query refused (not a single-table SELECT of a pool table): " + text);
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
    {
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    return CharacterQueryGuardedCompat(text);
}

// The rows a pool table holds, by a COUNT (one row always, so a null result is "no answer"). false = the database did not answer.
inline bool PoolRowCountCompat(char const* table, std::uint64_t& count)
{
    count = 0;
    std::string const name(table ? table : "");
    if (!PoolTableCompat(name))
        return false;
    QueryResult result = CharacterQueryGuardedCompat("SELECT COUNT(*) FROM `" + name + "`");
    if (!result)
        return false;
    count = result->Fetch()[0].GetUInt64();
    return true;
}

// The table a write names: `INSERT INTO `table``, `UPDATE `table``, `DELETE FROM `table`` or `CREATE TABLE IF NOT EXISTS
// `table``, nothing else. False for any other form (do less).
inline bool PoolWriteTableCompat(std::string const& text, std::string& table)
{
    char const* const writes[] = { "INSERT INTO `", "UPDATE `", "DELETE FROM `", "CREATE TABLE IF NOT EXISTS `" };
    for (char const* prefix : writes)
    {
        std::string const head(prefix);
        if (text.compare(0, head.size(), head) != 0)
            continue;
        std::size_t const close = text.find('`', head.size());
        if (close == std::string::npos)
            return false;
        table = text.substr(head.size(), close - head.size());
        return !table.empty();
    }
    return false;
}

// ===============================================================================================================
// THE CORE'S OWN characters TABLE
// ===============================================================================================================
//
// The character cache is not an authority: it is filled from this table at start and can miss a row that a queued
// commit has not written yet, or that anything else removed from it. A creation is confirmed, a claim is finished and
// an account is pruned only from the table itself. Every read is a COUNT first: a COUNT over an indexed column answers
// exactly one row, so a null result is "the characters database did not answer", never "no character"; the detail read
// must then return every row the count promised.
//
// These are reads of the core's mandatory schema (the server does not run without `characters`), so they go through the
// characters-database guard's statement helper rather than the pool tables' information_schema check.

struct PoolCoreCharacter
{
    std::uint64_t guid = 0;
    std::uint32_t accountId = 0;
    std::string name;
    bool online = false;
};

inline std::string PoolCharacterSqlTextCompat(std::string value)
{
    CharacterDatabase.EscapeString(value);     // names are letters, but a read is never built from unchecked text
    return "'" + value + "'";
}

inline bool PoolCharacterCountCompat(std::string const& where, std::uint64_t& count)
{
    std::string const sql = "SELECT COUNT(*) FROM `characters` WHERE " + where;
    QueryResult result = CharacterQueryGuardedCompat(sql);
    if (!result)
        return false;
    count = result->Fetch()[0].GetUInt64();
    return true;
}

// The account that owns `name` now. false = the characters database did not answer; the caller keeps its claim and retries.
inline bool PoolCharacterOwnerCompat(std::string const& name, bool& found, std::uint64_t& guid, std::uint32_t& accountId)
{
    found = false;
    guid = 0;
    accountId = 0;
    std::string const where = "`name` = " + PoolCharacterSqlTextCompat(name);
    std::uint64_t count = 0;
    if (!PoolCharacterCountCompat(where, count))
        return false;
    if (!count)
        return true;
    std::string const sql = "SELECT `guid`, `account` FROM `characters` WHERE " + where + " LIMIT 1";
    QueryResult result = CharacterQueryGuardedCompat(sql);
    if (!result)                   // the count promised a row: a null answer is a failed read, never an absence
        return false;
    Field* fields = result->Fetch();
    guid = fields[0].GetUInt64();
    accountId = fields[1].GetUInt32();
    found = true;
    return true;
}

// Every character of the account. false = the database did not answer, or answered fewer rows than it counted.
inline bool PoolAccountCharactersCompat(std::uint32_t accountId, std::vector<PoolCoreCharacter>& out)
{
    out.clear();
    std::string const where = "`account` = " + std::to_string(accountId);
    std::uint64_t count = 0;
    if (!PoolCharacterCountCompat(where, count))
        return false;
    if (!count)
        return true;
    std::string const sql = "SELECT `guid`, `account`, `name`, `online` FROM `characters` WHERE " + where;
    QueryResult result = CharacterQueryGuardedCompat(sql);
    if (!result)
        return false;
    do
    {
        Field* fields = result->Fetch();
        PoolCoreCharacter row;
        row.guid = fields[0].GetUInt64();
        row.accountId = fields[1].GetUInt32();
        row.name = fields[2].GetString();
        row.online = fields[3].GetUInt8() != 0;
        out.push_back(std::move(row));
    } while (result->NextRow());
    return out.size() == count;
}

// How many characters the database holds on any of these accounts (the pool's cap counts what is there, not what a row says).
// false = the database did not answer.
inline bool PoolCharactersOnAccountsCompat(std::set<std::uint32_t> const& accountIds, std::uint64_t& count)
{
    count = 0;
    if (accountIds.empty())
        return true;
    std::string list;
    for (std::uint32_t const id : accountIds)
        list += (list.empty() ? "" : ",") + std::to_string(id);
    return PoolCharacterCountCompat("`account` IN (" + list + ")", count);
}

// The core's own per-account count of a new character (CHAR_SEL_SUM_CHARS: not deleted), for the realm's character count.
// false = the database did not answer.
inline bool PoolRealmCharacterCountCompat(std::uint32_t accountId, std::uint64_t& count)
{
    count = 0;
    return PoolCharacterCountCompat("`account` = " + std::to_string(accountId) + " AND `deleteDate` IS NULL", count);
}

// One synchronous write: the caller has checked the table's columns just before it (TbPoolRegistry's Writable), and only a
// statement that names a pool table is let through. Anything else is refused and logged, never sent.
inline void PoolCharacterExecuteCompat(std::string const& sql)
{
    std::string table;
    if (!PoolWriteTableCompat(sql, table) || !PoolTableCompat(table))
    {
        PoolGuardLogCompat("TortoiseBots: pool statement refused (not a write to a pool table): " + sql);
        return;
    }
    CharacterExecuteGuardedCompat(sql);
}
