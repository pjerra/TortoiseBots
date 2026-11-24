#include "host/TbHostGuildOrderAccess.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_CharStore.h"
#include "compat/TbSqlNames.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderAccessPure.h"
#include "host/pool/TbPoolRegistry.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guild_order_access
{

namespace
{

char const* const kRankTable = "tortoise_bots_guild_bot_rank";
char const* const kConfigKey = "TortoiseBots.World.GuildOrderBotRank";

// Same DDL as forever/sql/characters/2026_11_23_01_tb_guild_bot_rank.sql, run
// once so a server without the migration still works when its database user may
// create tables.
char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank` ("
    "`guild_id` int unsigned NOT NULL,"
    "`bot_rank` tinyint unsigned NOT NULL DEFAULT '1',"
    "`updated_at` bigint unsigned NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`guild_id`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

struct State
{
    std::mutex mutex;
    bool loaded = false;
    bool available = false;
    bool warned = false;
    bool createTried = false;
    std::unordered_map<uint32_t, uint32_t> ranks;
};

State& Store()
{
    static State s;
    return s;
}

struct RankRow
{
    uint32_t rid = 0;
    uint32_t order = 0;
    std::string name;
    uint32_t rights = 0;   // GuildBankRights mask from guild_rank.rights
};

// ForeverCore aborts the world on a statement naming a missing table or column
// (MySQLConnection.cpp), so every characters read asks information_schema first:
// the query is this file's own single-table literal, its names are parsed out of
// it, and a table or column the guard does not find answers "nothing".
QueryResult QueryGuarded(std::string const& sql, char const* expectedTable)
{
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(sql, table, columns) || table != expectedTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: guild-rank query refused: {}", sql);
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
        return QueryResult();
    return CharacterQueryGuardedCompat(sql);
}

bool EnsureSchema(State& s, bool create)
{
    if (create && !s.createTried)
    {
        s.createTried = true;
        CharacterExecuteGuardedCompat(kCreate);
    }
    std::set<std::string> const columns = { "guild_id", "bot_rank", "updated_at" };
    s.available = CharacterSchemaHasCompat(kRankTable, columns);
    if (!s.available && !s.warned)
    {
        s.warned = true;
        TC_LOG_WARN("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; the guild bot rank is not persisted. Apply forever/sql/characters/2026_11_23_01_tb_guild_bot_rank.sql.", kRankTable);
    }
    return s.available;
}

// The guild's ranks in RankOrder order. Empty when the guild has none or the
// table did not answer.
std::vector<RankRow> ReadRanks(uint32_t guildId)
{
    std::vector<RankRow> out;
    if (!guildId)
        return out;
    std::string const sql = "SELECT `rid`, `RankOrder`, `rname`, `rights` FROM `guild_rank` WHERE `guildid` = "
        + std::to_string(guildId) + " ORDER BY `RankOrder`";
    QueryResult result = QueryGuarded(sql, "guild_rank");
    if (!result)
        return out;
    do
    {
        Field* fields = result->Fetch();
        RankRow row;
        row.rid = fields[0].GetUInt32();
        row.order = fields[1].GetUInt32();
        row.name = fields[2].GetString();
        row.rights = fields[3].GetUInt32();
        out.push_back(std::move(row));
    } while (result->NextRow());
    std::sort(out.begin(), out.end(), [](RankRow const& a, RankRow const& b) { return a.order < b.order; });
    return out;
}

// The bot rank's per-tab bank rights, one entry per purchased tab in TabId order
// (0-based; -1 = every right). A tab without a rights row is denied.
std::vector<uint8_t> ReadTabRights(uint32_t guildId, uint32_t rankId)
{
    std::vector<uint8_t> out;
    if (!guildId)
        return out;
    std::vector<uint8_t> tabIds;
    {
        std::string const sql = "SELECT `TabId` FROM `guild_bank_tab` WHERE `guildid` = "
            + std::to_string(guildId) + " ORDER BY `TabId`";
        QueryResult result = QueryGuarded(sql, "guild_bank_tab");
        if (!result)
            return out;
        do
            tabIds.push_back(static_cast<uint8_t>(result->Fetch()[0].GetUInt32()));
        while (result->NextRow());
    }
    std::unordered_map<uint8_t, uint8_t> rightsByTab;
    {
        std::string const sql = "SELECT `TabId`, `gbright` FROM `guild_bank_right` WHERE `guildid` = "
            + std::to_string(guildId) + " AND `rid` = " + std::to_string(rankId);
        QueryResult result = QueryGuarded(sql, "guild_bank_right");
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                int8 const raw = fields[1].GetInt8();
                rightsByTab[static_cast<uint8_t>(fields[0].GetUInt32())] =
                    raw < 0 ? uint8_t(0xff) : static_cast<uint8_t>(raw);
            } while (result->NextRow());
        }
    }
    out.reserve(tabIds.size());
    for (uint8_t tabId : tabIds)
    {
        auto const it = rightsByTab.find(tabId);
        out.push_back(it == rightsByTab.end() ? 0 : it->second);
    }
    return out;
}

std::vector<uint32_t> RankIdsByOrder(std::vector<RankRow> const& ranks)
{
    std::vector<uint32_t> ids;
    ids.reserve(ranks.size());
    for (RankRow const& rank : ranks)
        ids.push_back(rank.rid);
    return ids;
}

// The rank order a member's rank id sits at, or false when the guild no longer
// has that rank.
bool OrderOfRank(std::vector<RankRow> const& ranks, uint32_t rankId, uint32_t& outOrder)
{
    for (RankRow const& rank : ranks)
    {
        if (rank.rid == rankId)
        {
            outOrder = rank.order;
            return true;
        }
    }
    return false;
}

// The rank id the bot rank names, or false when the guild has no such order.
bool RankIdOfOrder(std::vector<RankRow> const& ranks, uint32_t order, uint32_t& outRankId)
{
    return RankIdAtOrder(RankIdsByOrder(ranks), order, outRankId);
}

bool GuildHasRealPlayerMember(Guild const& guild)
{
    for (auto const& [guid, member] : guild.GetMembers())
    {
        (void)member;
        if (!IsOrderBot(guid.GetCounter()))
            return true;
    }
    return false;
}

// Moves every module-owned bot of the guild to `rankId` and reports how many.
void MoveBotsToRank(Guild& guild, std::vector<RankRow> const& ranks, uint32_t rankId, uint32_t& moved)
{
    moved = 0;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (auto const& [guid, member] : guild.GetMembers())
    {
        if (!IsOrderBot(guid.GetCounter()))
            continue;
        uint32_t currentOrder = 0;
        if (!OrderOfRank(ranks, static_cast<uint8_t>(member.GetRankId()), currentOrder))
            continue;
        if (!BotRankMayMoveMember(currentOrder))
            continue;
        if (guild.ChangeMemberRank(trans, guid, GuildRankId(static_cast<uint8_t>(rankId))))
            ++moved;
    }
    CharacterDatabase.CommitTransaction(trans);
}

bool SessionIsRealPlayer(Player* player)
{
    WorldSession* const session = player ? player->GetSession() : nullptr;
    return session && !session->IsBot();
}

bool CanSetRank(uint32_t guildId, Player* asker)
{
    if (!asker || !SessionIsRealPlayer(asker) || !guildId || asker->GetGuildId() != guildId)
        return false;
    return MayChangeBotRank(true,
        tbhost::guild::HasAnyRankRight(guildId, asker->GetGUID(), GR_RIGHT_PROMOTE),
        tbhost::guild::HasAnyRankRight(guildId, asker->GetGUID(), GR_RIGHT_DEMOTE));
}

bool CanGiveOrders(uint32_t guildId, Player* asker)
{
    if (!asker || !guildId || asker->GetGuildId() != guildId)
        return false;
    return MayGiveOrders(SessionIsRealPlayer(asker), true,
        tbhost::guild::CanEditOfficerNotes(guildId, asker->GetGUID()));
}

void Refuse(std::string* outCode, std::string* outDetail, char const* code, char const* detail)
{
    if (outCode)
        *outCode = code;
    if (outDetail)
        *outDetail = detail;
}

} // namespace

bool BotRankEnabled()
{
    return sConfigMgr->GetBoolDefault(kConfigKey, true);
}

bool IsOrderBot(uint32_t guidLow)
{
    if (!guidLow)
        return false;
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
    if (TortoiseBots::BotMgr::Instance().IsPoolBot(guid))
        return IsGuildOrderBot(true, false);
    for (TortoiseBots::pool::Character const& character : TortoiseBots::pool::Characters())
        if (character.guid == guidLow)
            return IsGuildOrderBot(true, false);
    // A player's own character is not a pool row, so it never gets here: the
    // narrower fact (pool membership) is what excludes it, online or offline.
    return false;
}

std::optional<uint32_t> StoredBotRank(uint32_t guildId)
{
    if (!guildId || !BotRankEnabled())
        return std::nullopt;
    State& s = Store();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded)
    {
        s.loaded = true;
        EnsureSchema(s, true);
        if (s.available)
        {
            QueryResult result = QueryGuarded(
                "SELECT `guild_id`, `bot_rank` FROM `tortoise_bots_guild_bot_rank`", kRankTable);
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    uint32_t const guild = fields[0].GetUInt32();
                    uint32_t const rank = fields[1].GetUInt32();
                    if (guild)
                        s.ranks[guild] = rank;
                } while (result->NextRow());
            }
        }
    }
    auto const it = s.ranks.find(guildId);
    if (it == s.ranks.end())
        return std::nullopt;
    return it->second;
}

uint32_t WantedBotRank(uint32_t guildId, uint32_t rankCount)
{
    if (!BotRankEnabled())
        return 0;
    std::optional<uint32_t> const stored = StoredBotRank(guildId);
    if (!stored)
        return DefaultBotRank(rankCount);
    return ClampBotRank(*stored, rankCount);
}

bool BuildAccessReport(uint32_t guildId, Player* asker, AccessReport& out)
{
    out = AccessReport();
    out.guildId = guildId;
    if (!guildId || !sGuildMgr->GetGuildById(guildId))
        return false;

    out.canGiveOrders = CanGiveOrders(guildId, asker);

    std::vector<RankRow> const ranks = ReadRanks(guildId);
    if (ranks.size() < 2)
        return true;
    uint32_t const botRank = WantedBotRank(guildId, static_cast<uint32_t>(ranks.size()));
    uint32_t rankId = 0;
    if (!botRank || !RankIdOfOrder(ranks, botRank, rankId))
        return true;
    uint32_t rankRights = 0;
    for (RankRow const& rank : ranks)
        if (rank.rid == rankId)
        {
            out.rankName = rank.name;
            rankRights = rank.rights;
        }

    std::vector<uint32_t> tabsOk, tabsDenied;
    std::vector<uint8_t> const tabRights = ReadTabRights(guildId, rankId);
    SplitBankTabs(tabRights, kBankRightDepositItem, tabsOk, tabsDenied);
    out.enabled = true;
    out.botRank = botRank;
    out.tabsOk = FormatTabList(tabsOk);
    out.tabsDenied = FormatTabList(tabsDenied);
    // The access line's gold bit: a bank exists and the bot rank itself may
    // withdraw guild money (Forever keeps that as a rank right, not a rule
    // tied to the rank order).
    out.bankGold = !tabRights.empty() && (rankRights & GR_RIGHT_WITHDRAW_GOLD) != 0;
    out.canSetRank = CanSetRank(guildId, asker);
    return true;
}

bool SetBotRank(uint32_t guildId, uint32_t wanted, std::string* outCode, std::string* outDetail, uint32_t* moved)
{
    if (moved)
        *moved = 0;
    if (!BotRankEnabled())
    {
        Refuse(outCode, outDetail, "disabled", "TortoiseBots.World.GuildOrderBotRank is 0.");
        return false;
    }
    Guild* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    if (!guild)
    {
        Refuse(outCode, outDetail, "not-guild", "You are not in a guild.");
        return false;
    }
    std::vector<RankRow> const ranks = ReadRanks(guildId);
    if (ranks.size() < 2 || wanted < 1 || wanted >= ranks.size())
    {
        Refuse(outCode, outDetail, "invalid", "Rank must be between 1 and the lowest rank.");
        return false;
    }
    uint32_t const rankId = RankIdsByOrder(ranks)[wanted];

    {
        State& s = Store();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!EnsureSchema(s, true))
        {
            Refuse(outCode, outDetail, "store-unavailable", "tortoise_bots_guild_bot_rank missing");
            return false;
        }
        std::string const sql = "REPLACE INTO `tortoise_bots_guild_bot_rank` (`guild_id`, `bot_rank`, `updated_at`) VALUES ("
            + std::to_string(guildId) + ", " + std::to_string(wanted) + ", "
            + std::to_string(static_cast<uint64_t>(std::time(nullptr))) + ")";
        CharacterExecuteGuardedCompat(sql);
        s.ranks[guildId] = wanted;
    }

    if (!BotRankAppliesInGuild(GuildHasRealPlayerMember(*guild), wanted))
    {
        TC_LOG_INFO("tortoisebots", "[W9] botrank guild={} rank={} applies=no (no real player member)", guildId, wanted);
        return true;
    }
    uint32_t count = 0;
    MoveBotsToRank(*guild, ranks, rankId, count);
    if (moved)
        *moved = count;
    TC_LOG_INFO("tortoisebots", "[W9] botrank guild={} rank={} bots={}", guildId, wanted, count);
    return true;
}

void ApplyBotRankOnJoin(Player* joiner, uint32_t guildId)
{
    if (!joiner || !guildId || !BotRankEnabled())
        return;
    Guild* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || joiner->GetGuildId() != guildId)
        return;
    if (!IsOrderBot(joiner->GetGUID().GetCounter()))
        return;
    std::vector<RankRow> const ranks = ReadRanks(guildId);
    uint32_t const wanted = WantedBotRank(guildId, static_cast<uint32_t>(ranks.size()));
    if (!BotRankAppliesInGuild(GuildHasRealPlayerMember(*guild), wanted))
        return;
    uint32_t rankId = 0;
    if (!RankIdOfOrder(ranks, wanted, rankId))
        return;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    guild->ChangeMemberRank(trans, joiner->GetGUID(), GuildRankId(static_cast<uint8_t>(rankId)));
    CharacterDatabase.CommitTransaction(trans);
    TC_LOG_INFO("tortoisebots", "[W9] botrank guild={} join={} rank={}", guildId, joiner->GetName(), wanted);
}

bool MayGiveOrders(uint32_t guildId, Player* asker, std::string& outCode, std::string& outDetail)
{
    outCode.clear();
    outDetail.clear();
    if (!asker)
    {
        outCode = "forbidden";
        outDetail = "Only a real player can give orders.";
        return false;
    }
    bool const realPlayer = SessionIsRealPlayer(asker);
    bool const sameGuild = guildId != 0 && asker->GetGuildId() == guildId;
    bool const mayEdit = sameGuild && tbhost::guild::CanEditOfficerNotes(guildId, asker->GetGUID());
    if (MayGiveOrders(realPlayer, sameGuild, mayEdit))
        return true;
    if (!realPlayer)
    {
        outCode = "forbidden";
        outDetail = "Only a real player can give orders.";
    }
    else if (!sameGuild)
    {
        outCode = "not-guild";
        outDetail = "The bot is not in your guild.";
    }
    else
    {
        outCode = "no-rank-right";
        outDetail = "requires edit officer notes";
    }
    return false;
}

bool MayChangeBotRank(uint32_t guildId, Player* asker, std::string& outCode, std::string& outDetail)
{
    outCode.clear();
    outDetail.clear();
    if (!asker || !guildId || asker->GetGuildId() != guildId)
    {
        outCode = "not-guild";
        outDetail = "You are not in a guild.";
        return false;
    }
    if (!SessionIsRealPlayer(asker))
    {
        outCode = "forbidden";
        outDetail = "Only a real player can change the bots' rank.";
        return false;
    }
    if (!MayChangeBotRank(true,
            tbhost::guild::HasAnyRankRight(guildId, asker->GetGUID(), GR_RIGHT_PROMOTE),
            tbhost::guild::HasAnyRankRight(guildId, asker->GetGUID(), GR_RIGHT_DEMOTE)))
    {
        outCode = "forbidden";
        outDetail = "Your guild rank may not change ranks.";
        return false;
    }
    return true;
}

void LoadBotRankAtStartup()
{
    if (!BotRankEnabled())
    {
        TC_LOG_INFO("tortoisebots", "[W9] guild bot rank is off (TortoiseBots.World.GuildOrderBotRank = 0)");
        return;
    }
    State& s = Store();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded)
    {
        s.loaded = true;
        EnsureSchema(s, true);
        if (s.available)
        {
            QueryResult result = QueryGuarded(
                "SELECT `guild_id`, `bot_rank` FROM `tortoise_bots_guild_bot_rank`", kRankTable);
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    uint32_t const guild = fields[0].GetUInt32();
                    uint32_t const rank = fields[1].GetUInt32();
                    if (guild)
                        s.ranks[guild] = rank;
                } while (result->NextRow());
            }
        }
    }
    TC_LOG_INFO("tortoisebots", "[W9] loaded {} guild bot ranks", uint32_t(s.ranks.size()));
}

} // namespace guild_order_access
} // namespace tbhost
