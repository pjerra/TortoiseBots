// The `lootlog` addon verb (TBF #134 piece i-loot): see TbAddonLootLog.h.
// Ports TortoiseBots/commands/Inventory.cpp HandleLootLog (scope, pieces,
// window, caps, wire lines); the feed is the bot_events.csv rows the slice
// actions already write (StoreLootAction, LootMoney, LootRoll votes).
#include "host/TbAddonLootLog.h"
#include "host/TbAddonLootLogPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"
#include "host/TbAddonChannel.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/LogFileRotation.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <zlib.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace lootlog
{
namespace
{
// One bot_events.csv piece, plain or packed, read oldest to newest like TB's
// LootPieceLines (commands/Inventory.cpp): Skip discards the first offset
// bytes of uncompressed text, Next returns one line and the offset past it.
class LootPieceLines
{
public:
    explicit LootPieceLines(std::string const& path) : gz_(path.size() >= 3 &&
        path.compare(path.size() - 3, 3, ".gz") == 0)
    {
        if (gz_)
            file_ = gzopen(path.c_str(), "rb");
        else
            plain_.open(path, std::ios::in | std::ios::binary);
    }
    ~LootPieceLines()
    {
        if (gz_)
        {
            if (file_)
                gzclose(file_);
        }
    }
    bool IsOpen() const { return gz_ ? file_ != nullptr : plain_.is_open(); }
    bool Skip(uint64_t offset)
    {
        char discard[8 * 1024];
        while (offset > 0)
        {
            size_t want = offset > sizeof(discard) ? sizeof(discard) : static_cast<size_t>(offset);
            long got = Read(discard, want);
            if (got <= 0)
                return false;
            consumed_ += static_cast<uint64_t>(got);
            offset -= static_cast<uint64_t>(got);
        }
        return true;
    }
    bool Next(std::string& line, uint64_t& after)
    {
        line.clear();
        char c;
        bool any = false;
        for (;;)
        {
            long got = Read(&c, 1);
            if (got <= 0)
                break;
            any = true;
            ++consumed_;
            if (c == '\n')
                break;
            if (c != '\r')
                line += c;
        }
        after = consumed_;
        return any;
    }
private:
    long Read(char* buffer, size_t want)
    {
        if (gz_)
            return file_ ? gzread(file_, buffer, static_cast<unsigned>(want)) : -1;
        if (!plain_.is_open())
            return -1;
        plain_.read(buffer, static_cast<std::streamsize>(want));
        return static_cast<long>(plain_.gcount());
    }
    bool gz_ = false;
    gzFile file_ = nullptr;
    std::ifstream plain_;
    uint64_t consumed_ = 0;
};

std::uint32_t CallerBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

bool BotAllowed(player::Caller const& caller, std::uint32_t bnet, Player* requester, Player* bot, player::Tier tier)
{
    return bot && bot != requester && bot->IsInWorld() &&
        player::Decide(caller, FactsFor(requester, bot, bnet), tier) == player::Verdict::Allowed;
}

// TB's HandleLootLog scope: the requester's group members' bots, or the
// requester's own bots when ungrouped. Grouped is read-only group history
// (no ownership filter, like TB); ungrouped names only bots the caller may
// command, so one account cannot page through another's bots.
std::set<std::string> CollectNames(Player* requester, player::Caller const& caller, std::uint32_t bnet)
{
    std::set<std::string> names;
    if (Group* group = requester->GetGroup())
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (slot.guid == requester->GetGUID())
                continue;
            Player* member = ObjectAccessor::FindConnectedPlayer(slot.guid);
            if (!member || !member->IsInWorld())
                continue;
            if (!BotMgr::Instance().IsBot(member->GetGUID()))
                continue;
            names.insert(member->GetName());
        }
        return names;
    }
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!BotAllowed(caller, bnet, requester, bot, player::Tier::Tactical))
            continue;
        names.insert(bot->GetName());
    }
    return names;
}

struct LootPage
{
    std::vector<ai::LootHistoryRow> rows;
    std::string nextCursor;
    bool expired = false;
};

LootPage ReadLootPage(std::set<std::string> const& names, ai::LootHistoryCursor const& cursor)
{
    LootPage page;
    std::string const livePath = sPlayerbotAIConfig.logsDirPath() + "bot_events.csv";
    std::vector<std::string> plain, compressed;
    ai::LogFileRotation::Pieces(livePath, plain, compressed);
    std::vector<std::string> pieces = compressed;
    pieces.insert(pieces.end(), plain.begin(), plain.end());
    std::sort(pieces.begin(), pieces.end());
    std::error_code liveError;
    if (std::filesystem::exists(livePath, liveError) && !liveError)
        pieces.push_back(livePath);
    time_t const now = time(nullptr);
    time_t const cutoff = now - ai::LootHistoryPolicy::kLootHistoryWindowSeconds;
    std::vector<std::string> candidates;
    for (std::string const& piece : pieces)
    {
        if (piece == livePath)
        {
            candidates.push_back(piece);
            continue;
        }
        std::error_code mtimeError;
        auto mtime = std::filesystem::last_write_time(piece, mtimeError);
        if (mtimeError)
            continue;
        auto age = std::filesystem::file_time_type::clock::now() - mtime;
        if (age > std::chrono::hours(24))
            continue;
        candidates.push_back(piece);
    }
    if (!cursor.piece.empty())
    {
        bool found = false;
        for (std::string const& piece : candidates)
            if (std::filesystem::path(piece).filename().string() == cursor.piece)
                found = true;
        if (!found)
        {
            page.expired = true;
            page.nextCursor = "expired";
            return page;
        }
    }
    bool capped = false;
    bool resumeArmed = !cursor.piece.empty();
    uint64_t const scanCap = 4ull * 1024ull * 1024ull;
    for (auto it = candidates.rbegin(); it != candidates.rend() && !capped; ++it)
    {
        std::string const& piece = *it;
        std::string const filename = std::filesystem::path(piece).filename().string();
        if (resumeArmed && filename != cursor.piece)
            continue;
        LootPieceLines lines(piece);
        if (!lines.IsOpen())
        {
            resumeArmed = false;
            continue;
        }
        uint64_t resume = (resumeArmed && filename == cursor.piece) ? cursor.offset : 0;
        resumeArmed = false;
        if (resume > 0 && !lines.Skip(resume))
            continue;
        std::string line;
        uint64_t after = resume;
        while (lines.Next(line, after))
        {
            ai::LootHistoryRow row;
            if (!ai::LootHistoryPolicy::ParseLootHistoryRow(line, row))
                continue;
            if (names.find(row.bot) == names.end())
                continue;
            time_t when = ai::LootHistoryPolicy::ParseLootTimestamp(row.timestamp);
            if (when == 0 || when < cutoff)
                continue;
            page.rows.push_back(row);
            if (page.rows.size() >= ai::LootHistoryPolicy::kLootHistoryMaxRows || after - resume >= scanCap)
            {
                capped = true;
                if (piece != livePath)
                {
                    ai::LootHistoryCursor next;
                    next.piece = filename;
                    next.offset = after;
                    page.nextCursor = ai::LootHistoryPolicy::EncodeLootCursor(next);
                }
                break;
            }
        }
    }
    return page;
}

void HandleLootLogVerb(Player* requester, std::string_view args)
{
    if (!requester || !requester->GetSession())
        return;
    char const* const intent = "lootlog";
    ParsedLootLogArgs const parsed = ParseLootLogArgs(args);
    if (!parsed.valid)
    {
        addon::Err(requester, intent, "invalid", LootLogUsage());
        return;
    }
    WorldSession* const session = requester->GetSession();
    player::Caller caller;
    caller.gm = session->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    std::uint32_t const bnet = CallerBnet(requester);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();
    std::set<std::string> const names = CollectNames(requester, caller, bnet);
    if (names.empty())
    {
        addon::Err(requester, intent, "no-bot", "No live group bots found.");
        return;
    }
    LootPage const page = ReadLootPage(names, parsed.cursor);
    addon::Send(requester, FormatLootBegin(page.rows.size(), page.nextCursor));
    for (ai::LootHistoryRow const& row : page.rows)
        addon::Send(requester, FormatLootLine(RowFromHistory(row)));
    addon::Send(requester, FormatLootEnd(page.nextCursor));
}

} // namespace

void RegisterAddonVerbsLootLog()
{
    addon::RegisterVerb("lootlog", &HandleLootLogVerb);
    addon::AdvertiseCap("loot-log");
}

bool HandleLootLogChat(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    Player* requester = handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        if (handler)
            handler->SendSysMessage("TortoiseBots: .tb lootlog requires an in-game player session.");
        return false;
    }
    HandleLootLogVerb(requester, std::string_view(args));
    return true;
}

} // namespace lootlog
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace lootlog
{
void RegisterAddonVerbsLootLog() { }

bool HandleLootLogChat(ChatHandler* handler, Trinity::ChatCommands::Tail)
{
    if (handler)
        handler->SendSysMessage("error code=engine-not-built");
    return false;
}
} // namespace lootlog
} // namespace TortoiseBots

#endif
