// Commander port, package P2: see TbCmdBindingStore.h. The characters-database side of the opt-in binding persistence (decision D3).
//
// The DDL is the one in forever/sql/characters/2026_11_20_00_tb_commander_binding.sql, kept here as text and run once when the key is on, so an enabled
// server works without the migration. Every query goes through CharacterQueryCompat (the ForeverCore schema guard: a missing table aborts the world).
#include "host/cmd/TbCmdBindingStore.h"

#include "compat/TbSqlNames.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "StringFormat.h"

#include <algorithm>
#include <set>
#include <string>

namespace TortoiseBots
{
namespace cmd
{
namespace store
{
namespace
{
// The five fields of one row; nothing else is ever selected, so the guard's column check is also the D2 check (no group id, no group counter).
char const* const kSelect = "SELECT `commander_guid`, `lead_guid`, `members`, `flags`, `bound_at` FROM `tb_commander_binding`";
char const* const kTable = "tb_commander_binding";

struct State
{
    bool configured = false;
    bool enabled = false;
    bool loaded = false;
    bool tableReady = false;
    bool tableWarned = false;
    bool loadTried = false;
    int64_t commanderGraceMs = 300000;
    int64_t leadDeadGraceMs = 180000;
    std::vector<life::Stored> rows;
};

State& Get()
{
    static State state;
    return state;
}

void EnsureTable(State& s)
{
    if (!s.enabled || s.tableReady)
        return;
    // CREATE TABLE IF NOT EXISTS on a synchronous connection (a queued Execute could race the column check below), then the guard's own check: on a
    // database whose user may not create tables the row is simply not stored, and one line says so.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `tb_commander_binding` ("
        "`commander_guid` bigint unsigned NOT NULL,"
        "`lead_guid` bigint unsigned NOT NULL,"
        "`members` text NOT NULL,"
        "`flags` tinyint unsigned NOT NULL DEFAULT '0',"
        "`bound_at` bigint NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`commander_guid`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    std::set<std::string> columns = { "commander_guid", "lead_guid", "members", "flags", "bound_at" };
    std::string sql = "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" + std::string(kTable) +
        "' AND COLUMN_NAME IN (";
    bool first = true;
    for (std::string const& column : columns)
    {
        sql += (first ? "'" : ", '") + column + "'";
        first = false;
    }
    sql += ")";
    QueryResult count = CharacterDatabase.Query(sql.c_str());
    uint64 found = count ? count->Fetch()[0].GetUInt64() : 0;
    s.tableReady = found == columns.size();
    if (!s.tableReady && !s.tableWarned)
    {
        s.tableWarned = true;
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: TortoiseBots.Commander.KeepBindings is on but `{}` could not be created or lacks a column: commander bindings are not persisted. Apply forever/sql/characters/2026_11_20_00_tb_commander_binding.sql.", kTable);
    }
}

// The schema guard of TbCompat_S5.h, for the characters database: the core aborts on a query naming a missing table or column (MySQLConnection.cpp
// ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), so ask information_schema first and answer a null result (do less), never the abort.
QueryResult CharacterQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    std::string const text(sql ? sql : "");
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(text, table, columns))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: characters query refused (not a single-table SELECT ... FROM `table`): {}", text);
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    std::string has = "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" + table + "'";
    if (!columns.empty())
    {
        has += " AND COLUMN_NAME IN (";
        bool first = true;
        for (std::string const& column : columns)
        {
            has += (first ? "'" : ", '") + column + "'";
            first = false;
        }
        has += ")";
    }
    QueryResult count = CharacterDatabase.Query(has.c_str());
    uint64 found = count ? count->Fetch()[0].GetUInt64() : 0;
    if (columns.empty() ? found == 0 : found != columns.size())
    {
        if (schemaMissing)
            *schemaMissing = true;
        return QueryResult();
    }
    return CharacterDatabase.Query(text.c_str());
}

} // namespace

void LoadConfig()
{
    State& s = Get();
    s.configured = true;
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Commander.KeepBindings", false);
    int32 const commanderGrace = sConfigMgr->GetIntDefault("TortoiseBots.Slice.MasterGraceSeconds", 300);
    s.commanderGraceMs = int64_t(std::clamp<int32>(commanderGrace, 0, 3600)) * 1000;
    int32 const deadGrace = sConfigMgr->GetIntDefault("TortoiseBots.Slice.DeadBackstopSeconds", 180);
    s.leadDeadGraceMs = int64_t(std::clamp<int32>(deadGrace, 10, 3600)) * 1000;
}

std::vector<life::Stored> Rows()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || s.loadTried)
        return s.rows;
    s.loadTried = true;
    EnsureTable(s);
    if (!s.tableReady)
        return s.rows;
    QueryResult result = CharacterQueryCompat(kSelect);
    if (!result)
        return s.rows;
    do
    {
        Field* fields = result->Fetch();
        life::Stored row;
        row.commander = fields[0].GetUInt64();
        row.lead = fields[1].GetUInt64();
        row.members = life::DecodeMembers(fields[2].GetString());
        row.flags = fields[3].GetUInt32();
        row.boundAt = int64_t(fields[4].GetUInt64());
        if (life::Valid(row))
        {
            std::vector<uint64_t> ordered;
            ordered.push_back(row.lead);
            for (uint64_t member : row.members)
                if (member != row.lead)
                    ordered.push_back(member);
            row.members = std::move(ordered);
            s.rows.push_back(std::move(row));
        }
        else
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: stored commander binding of #{}: refused (lead #{}, {} members)", row.commander, row.lead, row.members.size());
    } while (result->NextRow());
    return s.rows;
}

bool Enabled()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.enabled;
}

bool Save(life::Stored const& stored)
{
    if (!life::Valid(stored))
        return false;
    State& s = Get();
    if (!s.enabled)
        return false;
    EnsureTable(s);
    if (!s.tableReady)
        return false;
    auto const found = std::find_if(s.rows.begin(), s.rows.end(), [&](life::Stored const& row) { return row.commander == stored.commander; });
    if (found != s.rows.end())
        *found = stored;
    else
        s.rows.push_back(stored);
    // One connection, one statement: the delete and the insert are sent in this order on the same pool.
    CharacterDatabase.Execute(Trinity::StringFormat("DELETE FROM `tb_commander_binding` WHERE `commander_guid` = {}", stored.commander).c_str());
    CharacterDatabase.Execute(Trinity::StringFormat("INSERT INTO `tb_commander_binding` (`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`) VALUES ({}, {}, '{}', {}, {})",
        stored.commander, stored.lead, life::EncodeMembers(stored.members), stored.flags, stored.boundAt).c_str());
    return true;
}

bool Erase(uint64_t commander)
{
    State& s = Get();
    s.rows.erase(std::remove_if(s.rows.begin(), s.rows.end(), [&](life::Stored const& row) { return row.commander == commander; }), s.rows.end());
    if (!s.enabled)
        return false;
    EnsureTable(s);
    if (!s.tableReady)
        return false;
    CharacterDatabase.Execute(Trinity::StringFormat("DELETE FROM `tb_commander_binding` WHERE `commander_guid` = {}", commander).c_str());
    return true;
}

int64_t CommanderGraceMs()
{
    if (!Get().configured)
        LoadConfig();
    return Get().commanderGraceMs;
}

int64_t LeadDeadGraceMs()
{
    if (!Get().configured)
        LoadConfig();
    return Get().leadDeadGraceMs;
}
}
}
}
