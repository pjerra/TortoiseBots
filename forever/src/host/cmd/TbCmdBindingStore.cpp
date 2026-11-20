// Commander port, package P2: see TbCmdBindingStore.h. The characters-database side of the opt-in binding persistence (decision D3).
//
// The DDL is the one in forever/sql/characters/2026_11_20_00_tb_commander_binding.sql, kept here as text and run once when the key is on, so an enabled
// server works without the migration. ForeverCore aborts the world on a statement naming a missing table or column (MySQLConnection.cpp), so EVERY
// statement here - the read, the schema check and both writes - asks information_schema first and refuses when the five columns are not all there.
// The column check is repeated before every write: a table dropped or altered after the first check must disable persistence, not reach the core.
#include "host/cmd/TbCmdBindingStore.h"

#include "compat/TbSqlNames.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "StringFormat.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace store
{
namespace
{
// The five fields of one row; nothing else is ever selected or written, so the guard's column check is also the D2 check (no group id, no group
// counter).
char const* const kTable = "tb_commander_binding";
char const* const kColumns = "`commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`";
char const* const kSelect = "SELECT `commander_guid`, `lead_guid`, `members`, `flags`, `bound_at` FROM `tb_commander_binding`";

struct State
{
    bool configured = false;
    bool enabled = false;
    bool tableReady = false;
    bool tableWarned = false;
    bool tableTried = false;            // the module's own CREATE ran once for this config
    bool loadTried = false;
    int64_t commanderGraceMs = 300000;
    int64_t leadDeadGraceMs = 180000;
    std::vector<life::Stored> rows;
    std::set<uint64_t> pendingErase;    // a DELETE that did not land: retried, and never offered to a restore
};

State& Get()
{
    static State state;
    return state;
}

char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `tb_commander_binding` ("
    "`commander_guid` bigint unsigned NOT NULL,"
    "`lead_guid` bigint unsigned NOT NULL,"
    "`members` text NOT NULL,"
    "`flags` tinyint unsigned NOT NULL DEFAULT '0',"
    "`bound_at` bigint NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`commander_guid`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

// The information_schema check of the five columns. Runs on a synchronous connection: a queued statement could race it. Called before the load and
// again before EVERY write (a cached `true` is never trusted, review fix).
bool CheckSchema(State& s, bool create)
{
    if (!s.enabled)
        return false;
    if (create && !s.tableTried)
    {
        s.tableTried = true;
        CharacterDatabase.DirectExecute(kCreate);       // on a database whose user may not create tables the row is simply not stored
    }
    std::set<std::string> const columns = { "commander_guid", "lead_guid", "members", "flags", "bound_at" };
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
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: TortoiseBots.Commander.KeepBindings is on but `{}` is missing or lacks a column: commander bindings are not persisted. Apply forever/sql/characters/2026_11_20_00_tb_commander_binding.sql.", kTable);
    }
    return s.tableReady;
}

// The schema guard of TbCompat_S5.h, for the characters database: the core aborts on a query naming a missing table or column (MySQLConnection.cpp
// ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), so ask information_schema first and answer a null result (do less), never the abort. Only this store's one
// table may ever be named: the SQL is this file's own literal, so a table or column name can never come from anywhere else.
QueryResult CharacterQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    std::string const text(sql ? sql : "");
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(text, table, columns) || table != kTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: characters query refused (not this store's single-table SELECT): {}", text);
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

// Every stored row, read through the guard. Never touches `loadTried`: it is the one reader Rows() and the write confirmations share.
std::vector<life::Stored> ReadRows()
{
    std::vector<life::Stored> rows;
    QueryResult result = CharacterQueryCompat(kSelect);
    if (!result)
        return rows;
    // The core's Query hands the result over standing on its first row (BasicStatementTask::Query calls NextRow, and an empty answer is a null
    // result), so that row is read before the loop advances; a `while (NextRow())` first would silently drop it.
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
            rows.push_back(std::move(row));
            continue;
        }
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: stored commander binding of #{}: refused (lead #{}, {} members)", row.commander, row.lead, row.members.size());
    } while (result->NextRow());
    return rows;
}

// The row as the database has it now (nullptr when it is not there), for a write's confirmation.
life::Stored const* Find(std::vector<life::Stored> const& rows, uint64_t commander)
{
    for (life::Stored const& row : rows)
        if (row.commander == commander)
            return &row;
    return nullptr;
}

bool SameRow(life::Stored const& a, life::Stored const& b)
{
    return a.commander == b.commander && a.lead == b.lead && a.members == b.members && a.flags == b.flags && a.boundAt == b.boundAt;
}

// The rows a restore may see: a row whose delete has not landed is not a binding any more, whatever the database still says.
std::vector<life::Stored> VisibleRows(State& s)
{
    std::vector<life::Stored> rows;
    rows.reserve(s.rows.size());
    for (life::Stored const& row : s.rows)
        if (!s.pendingErase.count(row.commander))
            rows.push_back(row);
    return rows;
}

void Remember(State& s, life::Stored const& stored)
{
    auto const found = std::find_if(s.rows.begin(), s.rows.end(), [&](life::Stored const& row) { return row.commander == stored.commander; });
    if (found != s.rows.end())
        *found = stored;
    else
        s.rows.push_back(stored);
}

// The write guard: the key on, the five columns there now, and a synchronous connection for the statement (one statement, no transaction, so no
// statement can land in front of another). False = nothing was sent.
bool Writable(State& s)
{
    if (!s.configured)
        LoadConfig();
    return CheckSchema(s, true);
}
} // namespace

void LoadConfig()
{
    State& s = Get();
    bool const reload = s.configured;
    s.configured = true;
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Commander.KeepBindings", false);
    int32 const commanderGrace = sConfigMgr->GetIntDefault("TortoiseBots.Slice.MasterGraceSeconds", 300);
    s.commanderGraceMs = int64_t(std::clamp<int32>(commanderGrace, 0, 3600)) * 1000;
    int32 const deadGrace = sConfigMgr->GetIntDefault("TortoiseBots.Slice.DeadBackstopSeconds", 180);
    s.leadDeadGraceMs = int64_t(std::clamp<int32>(deadGrace, 10, 3600)) * 1000;
    if (!reload)
        return;
    // A config reload re-reads everything: the key, the table and the rows. A server that was started with `KeepBindings = 0` and turned it on with
    // `.reload config` must read the rows then (they were never loaded), and a table that appeared meanwhile must be used.
    s.rows.clear();
    s.loadTried = false;
    s.tableReady = false;
    s.tableWarned = false;
    s.tableTried = false;
    // pendingErase is NOT cleared: a delete that did not land is still owed, and a reload must not turn its row back into a restorable binding.
}

std::vector<life::Stored> Rows()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || s.loadTried)
        return VisibleRows(s);
    s.loadTried = true;
    if (!CheckSchema(s, true))
        return VisibleRows(s);
    s.rows = ReadRows();
    return VisibleRows(s);
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
    if (!Writable(s))
        return false;
    // One statement: the row is replaced in place, so a crash, a disconnect or a refused INSERT can never leave the binding deleted and unwritten
    // (review fix: a DELETE followed by an INSERT had both failure modes).
    std::string const sql = Trinity::StringFormat(
        "INSERT INTO `tb_commander_binding` ({}) VALUES ({}, {}, '{}', {}, {}) ON DUPLICATE KEY UPDATE "
        "`lead_guid` = VALUES(`lead_guid`), `members` = VALUES(`members`), `flags` = VALUES(`flags`), `bound_at` = VALUES(`bound_at`)",
        kColumns, stored.commander, stored.lead, life::EncodeMembers(stored.members), stored.flags, stored.boundAt);
    CharacterDatabase.DirectExecute(sql.c_str());
    // A one-way statement cannot answer, so the row is read back: the in-memory view changes only after the database really holds it.
    std::vector<life::Stored> const rows = ReadRows();
    life::Stored const* now = Find(rows, stored.commander);
    if (!now || !SameRow(*now, stored))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the commander binding of #{} was not stored (the row is not there after the write); it stands in memory only",
            stored.commander);
        return false;
    }
    Remember(s, stored);
    s.pendingErase.erase(stored.commander);     // an explicit write is newer than the delete it replaces
    return true;
}

bool Erase(uint64_t commander)
{
    State& s = Get();
    if (!s.enabled)
        return false;
    if (!Writable(s))
        return false;
    CharacterDatabase.DirectExecute(Trinity::StringFormat("DELETE FROM `tb_commander_binding` WHERE `commander_guid` = {}", commander).c_str());
    std::vector<life::Stored> const rows = ReadRows();
    if (Find(rows, commander))
    {
        // The row is still durable, so the release it belonged to is not complete: keep it pending for the upkeep pass and out of every restore.
        s.pendingErase.insert(commander);
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the stored commander binding of #{} was not deleted (the row is still there); it is kept pending for a retry and is not restored", commander);
        return false;
    }
    s.pendingErase.erase(commander);
    s.rows.erase(std::remove_if(s.rows.begin(), s.rows.end(), [&](life::Stored const& row) { return row.commander == commander; }), s.rows.end());
    return true;
}

bool PendingErase(uint64_t commander)
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.pendingErase.count(commander) != 0;
}

bool RetryErases()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || s.pendingErase.empty())
        return s.pendingErase.empty();
    std::vector<uint64_t> const pending(s.pendingErase.begin(), s.pendingErase.end());
    for (uint64_t commander : pending)
        Erase(commander);
    return s.pendingErase.empty();
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
