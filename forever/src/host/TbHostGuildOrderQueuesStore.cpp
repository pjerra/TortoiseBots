// The guarded characters store of the guild relist ledger (TBF #155): one row
// per auction surplus an order posted for the guild, whose sale gold or
// returned stack arrives by mail later, often after the order is gone.
//
// ForeverCore aborts the world on a statement that names a missing table or
// column (MySQLConnection.cpp: ER_NO_SUCH_TABLE / ER_BAD_FIELD_ERROR), so
// every statement here asks information_schema first and answers "nothing"
// when `tortoise_bots_guild_relist` is not there: the ledger stays off with
// one warning (does less, never aborts). Only this file's own literals can
// name a table or a column (the parser in compat/TbSqlNames.h).
//
// A one-way synchronous statement cannot answer, so every write is read back
// before memory changes: a claim the table did not keep must not let the mail
// be taken, and a received row the table lost is not reported as in hand.
//
// This translation unit deliberately includes no game types, so
// forever/tools/guild_relist_store_check.cpp can compile it against the fake
// characters database and pin the row contract.
#include "host/TbHostGuildOrderQueues.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbSqlNames.h"
#include "DatabaseEnv.h"
#include "Log.h"

#include "runtime/GuildOrderStore.h"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using TortoiseBots::GuildOrders::RelistBotPresence;
using TortoiseBots::GuildOrders::RelistEntryAged;
using TortoiseBots::GuildOrders::RelistEntryExpiry;
using TortoiseBots::GuildOrders::RelistExpiry;
using TortoiseBots::GuildOrders::RelistLedgerEntry;
using TortoiseBots::GuildOrders::RelistNeedsDeposit;
using TortoiseBots::GuildOrders::RelistState;
using TortoiseBots::GuildOrders::RelistStateFromDb;
using TortoiseBots::GuildOrders::TakenMailSender;

char const* const kTable = "tortoise_bots_guild_relist";
char const* const kColumns =
    "`bot_guid`, `seq`, `guild_id`, `item_id`, `count`, `unit_price`, `bid`, `buyout`, "
    "`deposit`, `posted_at`, `expires_at`, `state`, `claim_mail`, `held_gold`, `held_count`";
char const* const kSelect = "SELECT `bot_guid`, `seq`, `guild_id`, `item_id`, `count`, "
    "`unit_price`, `bid`, `buyout`, `deposit`, `posted_at`, `expires_at`, `state`, "
    "`claim_mail`, `held_gold`, `held_count` FROM `tortoise_bots_guild_relist`";

// Same DDL as forever/sql/characters/2026_11_24_01_tb_guild_relist.sql, run
// once so a server without the migration still works when its database user
// may create tables.
char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist` ("
    "`bot_guid` int unsigned NOT NULL,"
    "`seq` int unsigned NOT NULL,"
    "`guild_id` int unsigned NOT NULL,"
    "`item_id` int unsigned NOT NULL,"
    "`count` int unsigned NOT NULL,"
    "`unit_price` int unsigned NOT NULL,"
    "`bid` int unsigned NOT NULL DEFAULT '0',"
    "`buyout` int unsigned NOT NULL DEFAULT '0',"
    "`deposit` int unsigned NOT NULL DEFAULT '0',"
    "`posted_at` int unsigned NOT NULL DEFAULT '0',"
    "`expires_at` int unsigned NOT NULL DEFAULT '0',"
    "`state` tinyint unsigned NOT NULL DEFAULT '0',"
    "`claim_mail` int unsigned NOT NULL DEFAULT '0',"
    "`held_gold` int unsigned NOT NULL DEFAULT '0',"
    "`held_count` int unsigned NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`bot_guid`, `seq`),"
    "KEY `guild_id` (`guild_id`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

std::set<std::string> const& RequiredColumns()
{
    static std::set<std::string> const columns = {
        "bot_guid", "seq", "guild_id", "item_id", "count", "unit_price", "bid", "buyout",
        "deposit", "posted_at", "expires_at", "state", "claim_mail", "held_gold", "held_count"
    };
    return columns;
}

struct StoreState
{
    std::mutex mutex;
    bool loaded = false;
    bool available = false;
    bool warned = false;
    bool createTried = false;
    uint64_t lastSweepSec = 0;
    std::unordered_map<uint32_t, std::vector<RelistLedgerEntry>> rows;
    std::vector<TakenMailSender> takenMail;
};

StoreState& State()
{
    static StoreState state;
    return state;
}

// ForeverCore aborts the world on a statement naming a missing table or
// column, so every read asks information_schema first: the query is this
// file's own single-table literal, its names are parsed out of it, and a
// table or column the guard does not find answers "nothing".
QueryResult QueryGuarded(std::string const& sql)
{
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(sql, table, columns) || table != kTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: guild relist query refused: {}", sql);
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
        return QueryResult();
    return CharacterQueryGuardedCompat(sql);
}

std::string Number(uint64_t value)
{
    return std::to_string(value);
}

std::string WhereKey(uint32_t botGuidLow, uint32_t seq)
{
    return " WHERE `bot_guid` = " + Number(botGuidLow) + " AND `seq` = " + Number(seq);
}

RelistLedgerEntry ReadRow(Field* fields)
{
    RelistLedgerEntry entry;
    entry.botGuidLow = fields[0].GetUInt32();
    entry.seq = fields[1].GetUInt32();
    entry.guildId = fields[2].GetUInt32();
    entry.itemId = fields[3].GetUInt32();
    entry.count = fields[4].GetUInt32();
    entry.unitPrice = fields[5].GetUInt32();
    entry.bid = fields[6].GetUInt32();
    entry.buyout = fields[7].GetUInt32();
    entry.deposit = fields[8].GetUInt32();
    entry.postedAtSec = fields[9].GetUInt64();
    entry.expiresAtSec = fields[10].GetUInt64();
    entry.state = RelistStateFromDb(fields[11].GetUInt32());
    entry.claimMailId = fields[12].GetUInt32();
    entry.heldGold = fields[13].GetUInt32();
    entry.heldCount = fields[14].GetUInt32();
    return entry;
}

// The row the table holds for one key, if any.
bool ReadOne(uint32_t botGuidLow, uint32_t seq, RelistLedgerEntry& out)
{
    QueryResult result = QueryGuarded(std::string(kSelect) + WhereKey(botGuidLow, seq));
    if (!result)
        return false;
    out = ReadRow(result->Fetch());
    return true;
}

bool SameRow(RelistLedgerEntry const& a, RelistLedgerEntry const& b)
{
    return a.botGuidLow == b.botGuidLow && a.seq == b.seq && a.guildId == b.guildId &&
        a.itemId == b.itemId && a.count == b.count && a.unitPrice == b.unitPrice &&
        a.bid == b.bid && a.buyout == b.buyout && a.deposit == b.deposit &&
        a.postedAtSec == b.postedAtSec && a.expiresAtSec == b.expiresAtSec &&
        a.state == b.state && a.claimMailId == b.claimMailId && a.heldGold == b.heldGold &&
        a.heldCount == b.heldCount;
}

std::string ReplaceSql(RelistLedgerEntry const& row)
{
    return std::string("REPLACE INTO `tortoise_bots_guild_relist` (") + kColumns +
        ") VALUES (" + Number(row.botGuidLow) + ", " + Number(row.seq) + ", " +
        Number(row.guildId) + ", " + Number(row.itemId) + ", " + Number(row.count) + ", " +
        Number(row.unitPrice) + ", " + Number(row.bid) + ", " + Number(row.buyout) + ", " +
        Number(row.deposit) + ", " + Number(row.postedAtSec) + ", " + Number(row.expiresAtSec) +
        ", " + Number(uint32_t(row.state)) + ", " + Number(row.claimMailId) + ", " +
        Number(row.heldGold) + ", " + Number(row.heldCount) + ")";
}

std::string DeleteSql(uint32_t botGuidLow, uint32_t seq)
{
    return "DELETE FROM `tortoise_bots_guild_relist`" + WhereKey(botGuidLow, seq);
}

bool EnsureSchema(StoreState& s, bool create)
{
    if (create && !s.createTried)
    {
        s.createTried = true;
        CharacterExecuteGuardedCompat(kCreate);
    }
    s.available = CharacterSchemaHasCompat(kTable, RequiredColumns());
    if (!s.available && !s.warned)
    {
        s.warned = true;
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; the guild relist ledger is off. "
            "Apply forever/sql/characters/2026_11_24_01_tb_guild_relist.sql.", kTable);
    }
    return s.available;
}

bool StoreRowLocked(StoreState& s, RelistLedgerEntry const& row)
{
    CharacterExecuteGuardedCompat(ReplaceSql(row));
    RelistLedgerEntry stored;
    if (!ReadOne(row.botGuidLow, row.seq, stored) || !SameRow(stored, row))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: relist row bot #{} seq #{} (item #{} x{}) was not stored",
            row.botGuidLow, row.seq, row.itemId, row.count);
        return false;
    }
    for (RelistLedgerEntry& existing : s.rows[row.botGuidLow])
    {
        if (existing.seq == row.seq)
        {
            existing = row;
            return true;
        }
    }
    s.rows[row.botGuidLow].push_back(row);
    return true;
}

bool DeleteRowLocked(StoreState& s, uint32_t botGuidLow, uint32_t seq)
{
    CharacterExecuteGuardedCompat(DeleteSql(botGuidLow, seq));
    RelistLedgerEntry stored;
    if (ReadOne(botGuidLow, seq, stored))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: relist row bot #{} seq #{} was not deleted", botGuidLow, seq);
        return false;
    }
    auto const it = s.rows.find(botGuidLow);
    if (it != s.rows.end())
    {
        std::vector<RelistLedgerEntry>& rows = it->second;
        rows.erase(std::remove_if(rows.begin(), rows.end(),
            [seq](RelistLedgerEntry const& row) { return row.seq == seq; }), rows.end());
        if (rows.empty())
            s.rows.erase(it);
    }
    return true;
}

bool WriteState(uint32_t botGuidLow, uint32_t seq, RelistState state, uint32_t mailId,
    uint32_t gold, uint32_t items)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.available)
        return false;
    auto const it = s.rows.find(botGuidLow);
    if (it == s.rows.end())
        return false;
    bool found = false;
    for (RelistLedgerEntry const& row : it->second)
        if (row.seq == seq)
        {
            found = true;
            break;
        }
    if (!found)
        return false;

    CharacterExecuteGuardedCompat(
        "UPDATE `tortoise_bots_guild_relist` SET `state` = " + Number(uint32_t(state)) +
        ", `claim_mail` = " + Number(mailId) + ", `held_gold` = " + Number(gold) +
        ", `held_count` = " + Number(items) + WhereKey(botGuidLow, seq));
    RelistLedgerEntry stored;
    if (!ReadOne(botGuidLow, seq, stored) || stored.state != state ||
        stored.claimMailId != mailId || stored.heldGold != gold || stored.heldCount != items)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: relist row bot #{} seq #{} state {} was not stored",
            botGuidLow, seq, uint32_t(state));
        return false;
    }
    for (RelistLedgerEntry& row : it->second)
    {
        if (row.seq != seq)
            continue;
        row.state = state;
        row.claimMailId = mailId;
        row.heldGold = gold;
        row.heldCount = items;
        break;
    }
    return true;
}

} // namespace

void LoadGuildOrderQueues()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.rows.clear();
    s.loaded = true;
    s.available = false;
    if (!EnsureSchema(s, true))
        return;

    QueryResult result = QueryGuarded(kSelect);
    if (!result)
        return;
    do
    {
        RelistLedgerEntry row = ReadRow(result->Fetch());
        if (row.botGuidLow && row.itemId && row.count)
            s.rows[row.botGuidLow].push_back(row);
    } while (result->NextRow());
    uint32_t loaded = 0;
    for (auto const& [_, rows] : s.rows)
        loaded += uint32_t(rows.size());
    TC_LOG_INFO("tortoisebots", "[W9] loaded {} guild relist row(s)", loaded);
}

bool IsRelistStoreAvailable()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded)
        return EnsureSchema(s, true);
    return s.available;
}

std::vector<RelistLedgerEntry> RelistLedger(uint32_t botGuidLow)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto const it = s.rows.find(botGuidLow);
    if (it == s.rows.end())
        return {};
    return it->second;
}

bool RelistClaimed(uint32_t botGuidLow, uint32_t seq, uint32_t mailId, uint32_t gold, uint32_t items)
{
    if (!mailId)
        return false;
    return WriteState(botGuidLow, seq, RelistState::Claimed, mailId, gold, items);
}

bool RelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items)
{
    return WriteState(botGuidLow, seq, RelistState::Received, 0, gold, items);
}

std::vector<RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<RelistLedgerEntry> out;
    auto const it = s.rows.find(botGuidLow);
    if (it == s.rows.end())
        return out;
    for (RelistLedgerEntry const& row : it->second)
        if (RelistNeedsDeposit(row))
            out.push_back(row);
    return out;
}

bool SaveRelistRow(RelistLedgerEntry const& row)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.available)
        return false;
    return StoreRowLocked(s, row);
}

bool DeleteRelistRow(uint32_t botGuidLow, uint32_t seq)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.available)
        return false;
    return DeleteRowLocked(s, botGuidLow, seq);
}

bool RelistSweepDue(uint64_t nowSec)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.lastSweepSec && nowSec < s.lastSweepSec + 60)
        return false;
    s.lastSweepSec = nowSec;
    return true;
}

std::vector<uint32_t> AgedRelistBots(uint64_t nowSec)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<uint32_t> out;
    for (auto const& [guid, rows] : s.rows)
        for (RelistLedgerEntry const& row : rows)
            if (RelistEntryAged(row, nowSec))
            {
                out.push_back(guid);
                break;
            }
    return out;
}

void ExpireRelistRows(uint64_t nowSec, std::unordered_map<uint32_t, RelistBotPresence> const& presence)
{
    struct Dropped
    {
        RelistLedgerEntry row;
        RelistExpiry why;
    };
    std::vector<Dropped> dropped;
    {
        StoreState& s = State();
        std::lock_guard<std::mutex> lock(s.mutex);
        for (auto it = s.rows.begin(); it != s.rows.end();)
        {
            std::vector<RelistLedgerEntry>& rows = it->second;
            auto const seen = presence.find(it->first);
            for (auto row = rows.begin(); row != rows.end();)
            {
                // No presence read for this bot: keep the row for the next sweep.
                RelistExpiry const why = seen == presence.end() ? RelistExpiry::Keep :
                    RelistEntryExpiry(*row, seen->second, nowSec);
                if (why != RelistExpiry::Keep)
                {
                    dropped.push_back({ *row, why });
                    row = rows.erase(row);
                }
                else
                    ++row;
            }
            it = rows.empty() ? s.rows.erase(it) : std::next(it);
        }
    }
    for (Dropped const& d : dropped)
    {
        RelistLedgerEntry const& row = d.row;
        CharacterExecuteGuardedCompat(DeleteSql(row.botGuidLow, row.seq));
        RelistLedgerEntry stored;
        if (ReadOne(row.botGuidLow, row.seq, stored))
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: stale relist row bot #{} seq #{} was not deleted",
                row.botGuidLow, row.seq);
            continue;
        }
        if (d.why == RelistExpiry::MailExpired)
            TC_LOG_INFO("tortoisebots", "TortoiseBots: guild relist for bot {} (item {} x{}) is past the auction "
                "mail lifetime, mail gone; dropped the ledger row", row.botGuidLow, row.itemId, row.count);
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots: guild relist for bot {} (item {} x{}) returned nothing "
                "after the settle window; dropped the ledger row", row.botGuidLow, row.itemId, row.count);
    }
}

void StashTakenMailSender(uint32_t senderLow, uint32_t itemId, uint32_t count)
{
    if (!senderLow || !itemId || !count)
        return;
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.takenMail.push_back(TakenMailSender{ senderLow, itemId, count });
}

std::vector<TakenMailSender> TakeTakenMailSenders(uint32_t itemId)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<TakenMailSender> out;
    if (!itemId)
        return out;
    auto& stash = s.takenMail;
    for (auto it = stash.begin(); it != stash.end();)
        if (it->itemId == itemId)
        {
            out.push_back(*it);
            it = stash.erase(it);
        }
        else
            ++it;
    return out;
}

} // namespace guild_orders
} // namespace tbhost

// The legacy store names the compiled mail read and the not-yet-ported buy
// action call (runtime/GuildOrderStore.h): they answer from this store.
namespace TortoiseBots
{
namespace GuildOrders
{

std::vector<RelistLedgerEntry> RelistEntries(uint32_t botGuidLow)
{
    return tbhost::guild_orders::RelistLedger(botGuidLow);
}

bool NoteRelistClaimed(uint32_t botGuidLow, uint32_t seq, uint32_t mailId, uint32_t gold, uint32_t items)
{
    return tbhost::guild_orders::RelistClaimed(botGuidLow, seq, mailId, gold, items);
}

bool NoteRelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items)
{
    return tbhost::guild_orders::RelistReceived(botGuidLow, seq, gold, items);
}

bool SaveRelistRow(RelistLedgerEntry const& row)
{
    return tbhost::guild_orders::SaveRelistRow(row);
}

bool DeleteRelistRow(uint32_t botGuidLow, uint32_t seq)
{
    return tbhost::guild_orders::DeleteRelistRow(botGuidLow, seq);
}

std::vector<RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow)
{
    return tbhost::guild_orders::RelistDeposits(botGuidLow);
}

void NoteTakenMailSender(uint32_t senderLow, uint32_t itemId, uint32_t count)
{
    tbhost::guild_orders::StashTakenMailSender(senderLow, itemId, count);
}

std::vector<TakenMailSender> TakeTakenMailSenders(uint32_t itemId)
{
    return tbhost::guild_orders::TakeTakenMailSenders(itemId);
}

} // namespace GuildOrders
} // namespace TortoiseBots
