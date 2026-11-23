// The guarded characters store of the disenchant service queue (TBF #155).
//
// ForeverCore aborts the world on a statement that names a missing table or
// column (MySQLConnection.cpp: ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), so
// every statement here asks information_schema first and answers "nothing"
// when `tortoise_bots_guild_disenchant` is not there: the service stays off
// with one log line (does less, never aborts). Only this file's own literals
// can name a table or a column (the parser in compat/TbSqlNames.h rejects
// anything else).
//
// A one-way synchronous statement cannot answer, so every write is read back
// before memory changes: a row that did not land is not "queued", and the
// intake can mail the item straight back instead of stranding it.
//
// This translation unit deliberately includes no game types, so
// forever/tools/guild_disenchant_store_check.cpp can compile it against the
// fake characters database and pin the row contract.
#include "host/TbHostGuildOrderDisenchant.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbSqlNames.h"
#include "DatabaseEnv.h"
#include "Log.h"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using Entry = TortoiseBots::GuildOrders::DisenchantQueueEntry;

char const* const kTable = "tortoise_bots_guild_disenchant";
char const* const kSelect =
    "SELECT `bot_guid`, `sender_guid`, `item_guid`, `item_entry`, `item_name`, `pending`, `results` "
    "FROM `tortoise_bots_guild_disenchant`";

// created_at is not selected; it is written, so a table without it is not
// usable either.
std::set<std::string> const& RequiredColumns()
{
    static std::set<std::string> const columns = {
        "bot_guid", "sender_guid", "item_guid", "item_entry", "item_name", "results", "pending", "created_at"
    };
    return columns;
}

struct StoreState
{
    std::mutex mutex;
    bool loaded = false;
    bool available = false;
    bool warned = false;
    std::unordered_map<uint32_t, std::vector<Entry>> rows;
};

StoreState& State()
{
    static StoreState state;
    return state;
}

std::string EscapeSql(std::string value)
{
    CharacterDatabase.EscapeString(value);
    return value;
}

bool EnsureSchema(StoreState& s)
{
    if (s.available)
        return true;
    if (!CharacterSchemaHasCompat(kTable, RequiredColumns()))
    {
        if (!s.warned)
        {
            s.warned = true;
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; the disenchant service is off. "
                "Apply forever/sql/characters/2026_11_23_02_tb_guild_disenchant.sql.", kTable);
        }
        return false;
    }
    s.available = true;
    return true;
}

// The SELECT guard: this store's own single-table query, its columns checked
// right now.
QueryResult QueryCompat(std::string const& sql)
{
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(sql, table, columns) || table != kTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: disenchant query refused: {}", sql);
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
        return QueryResult();
    return CharacterQueryGuardedCompat(sql);
}

bool Valid(Entry const& entry)
{
    return entry.botGuidLow && entry.senderGuidLow && entry.itemGuidLow && entry.itemId;
}

// The rows as the table has them now. The core's Query hands the result over
// standing on its first row (BasicStatementTask::Query calls NextRow; an
// empty answer is a null result), so that row is read before the loop.
std::vector<Entry> ReadRows(std::string const& sql)
{
    std::vector<Entry> rows;
    QueryResult result = QueryCompat(sql);
    if (!result)
        return rows;
    do
    {
        Field* fields = result->Fetch();
        Entry entry;
        entry.botGuidLow = fields[0].GetUInt32();
        entry.senderGuidLow = fields[1].GetUInt32();
        entry.itemGuidLow = fields[2].GetUInt64();
        entry.itemId = fields[3].GetUInt32();
        entry.itemName = fields[4].GetString();
        entry.results = fields[6].GetString();
        entry.pendingResults = fields[5].GetUInt32() != 0;
        if (Valid(entry))
            rows.push_back(std::move(entry));
    } while (result->NextRow());
    return rows;
}

void Remember(StoreState& s, Entry const& entry)
{
    std::vector<Entry>& rows = s.rows[entry.botGuidLow];
    auto const found = std::find_if(rows.begin(), rows.end(),
        [&](Entry const& queued) { return queued.itemGuidLow == entry.itemGuidLow; });
    if (found != rows.end())
        *found = entry;
    else
        rows.push_back(entry);
}

void Forget(StoreState& s, uint32_t botGuidLow, uint64_t itemGuidLow)
{
    auto const bot = s.rows.find(botGuidLow);
    if (bot == s.rows.end())
        return;
    std::vector<Entry>& rows = bot->second;
    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [&](Entry const& queued) { return queued.itemGuidLow == itemGuidLow; }), rows.end());
    if (rows.empty())
        s.rows.erase(bot);
}

bool SameStored(Entry const& left, Entry const& right)
{
    return left.botGuidLow == right.botGuidLow && left.senderGuidLow == right.senderGuidLow &&
        left.itemGuidLow == right.itemGuidLow && left.itemId == right.itemId &&
        left.pendingResults == right.pendingResults && left.results == right.results;
}

} // namespace

namespace
{
// The whole table into memory. The caller holds the store lock.
void LoadLocked(StoreState& s)
{
    s.rows.clear();
    if (!EnsureSchema(s))
        return;
    std::vector<Entry> const rows = ReadRows(kSelect);
    for (Entry const& entry : rows)
        s.rows[entry.botGuidLow].push_back(entry);
    TC_LOG_INFO("tortoisebots", "[W9] loaded {} disenchant queue row(s)", uint32_t(rows.size()));
}
} // namespace

void LoadDisenchantQueue()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.loaded = true;
    s.available = false;
    LoadLocked(s);
}

bool IsDisenchantStoreAvailable()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded)
    {
        // A server that reached the service before its startup hook (or with
        // the engine off) still serves the rows already queued.
        s.loaded = true;
        s.available = false;
        LoadLocked(s);
    }
    return s.available;
}

std::vector<Entry> DisenchantQueued(uint32_t botGuidLow)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto const found = s.rows.find(botGuidLow);
    if (found == s.rows.end())
        return {};
    return found->second;
}

bool NoteDisenchantQueued(Entry const& entry)
{
    return SaveDisenchantRow(entry);
}

bool SaveDisenchantRow(Entry const& entry)
{
    if (!Valid(entry))
        return false;

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!EnsureSchema(s))
        return false;

    uint64_t const now = uint64_t(std::time(nullptr));
    std::string const sql =
        "REPLACE INTO `tortoise_bots_guild_disenchant` (`bot_guid`, `sender_guid`, `item_guid`, `item_entry`, "
        "`item_name`, `pending`, `results`, `created_at`) VALUES (" +
        std::to_string(entry.botGuidLow) + ", " + std::to_string(entry.senderGuidLow) + ", " +
        std::to_string(entry.itemGuidLow) + ", " + std::to_string(entry.itemId) + ", '" +
        EscapeSql(entry.itemName) + "', " + (entry.pendingResults ? "1" : "0") + ", '" +
        EscapeSql(entry.results) + "', FROM_UNIXTIME(" + std::to_string(now) + "))";
    CharacterExecuteGuardedCompat(sql);

    // The read-back of this bot's rows: the write landed when the key holds
    // the fields that were sent.
    std::vector<Entry> const rows = ReadRows(std::string(kSelect) + " WHERE `bot_guid` = " + std::to_string(entry.botGuidLow));
    auto const stored = std::find_if(rows.begin(), rows.end(),
        [&](Entry const& queued) { return queued.itemGuidLow == entry.itemGuidLow; });
    if (stored == rows.end() || !SameStored(*stored, entry))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the disenchant row of bot #{} item #{} (guid #{}) was not stored",
            entry.botGuidLow, entry.itemId, entry.itemGuidLow);
        return false;
    }
    Remember(s, entry);
    return true;
}

bool DeleteDisenchantRow(uint32_t botGuidLow, uint64_t itemGuidLow)
{
    if (!botGuidLow || !itemGuidLow)
        return false;

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!EnsureSchema(s))
        return false;

    std::string const sql =
        "DELETE FROM `tortoise_bots_guild_disenchant` WHERE `bot_guid` = " + std::to_string(botGuidLow) +
        " AND `item_guid` = " + std::to_string(itemGuidLow);
    CharacterExecuteGuardedCompat(sql);

    std::vector<Entry> const rows = ReadRows(std::string(kSelect) + " WHERE `bot_guid` = " + std::to_string(botGuidLow));
    if (std::any_of(rows.begin(), rows.end(), [&](Entry const& queued) { return queued.itemGuidLow == itemGuidLow; }))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the disenchant row of bot #{} item guid #{} was not deleted",
            botGuidLow, itemGuidLow);
        return false;
    }
    Forget(s, botGuidLow, itemGuidLow);
    return true;
}

} // namespace guild_orders
} // namespace tbhost
