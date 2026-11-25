// The addon roster verbs (TBF #130 piece f-roster). `roster` and `list` send
// the full TB-shaped reply (roster stream, ALTBOTS, the f-info CC_ASSIGN
// stream, the empty CLAIMED stream, BOTSTATE, then the
// VERSION/PROTO/LATEST/VERBS/CAPS/TRANSPORT trailer); `alts` sends the
// away-alt summary only. The rows are the same bots `.bot who` shows the
// caller (RosterVisible over the pure rule); a GM sees what a player sees.
// `list` aliases `roster` here: TB's plain-text HandleList stays a chat-side
// shape and is not ported.
#include "host/TbAddonRoster.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "host/TbAddonChannel.h"
#include "host/TbAddonInfo.h"
#include "host/TbAddonRosterPure.h"
#include "host/player/TbPlayerAuthPure.h"
#include "runtime/HireLifecycle.h"
#include "TbBotMgr.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRunHooks.h"
#include "host/TbH3.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "DB2Stores.h"
#include "Define.h"
#include "GitRevision.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace roster
{
namespace
{
// The caller's Battle.net account, read fresh per command like the `.bot`
// entry does: a game account counts as owned only while it is still one of
// the caller's Battle.net account's game accounts now.
struct OwnerScope
{
    std::uint32_t bnet = 0;
    bool Owns(std::uint32_t gameAccount) const
    {
        if (!bnet || !gameAccount)
            return false;
        auto it = memo.find(gameAccount);
        if (it == memo.end())
            it = memo.emplace(gameAccount, Battlenet::AccountMgr::GetIdByGameAccount(gameAccount)).first;
        return it->second == bnet;
    }
private:
    mutable std::unordered_map<std::uint32_t, std::uint32_t> memo;
};

OwnerScope ScopeOf(Player* player)
{
    OwnerScope scope;
    WorldSession* session = player ? player->GetSession() : nullptr;
    if (!session)
        return scope;
    scope.bnet = session->GetBattlenetAccountId();
    if (!scope.Owns(session->GetAccountId()))
        return OwnerScope{};
    return scope;
}

bool HasTbPermission(Player* player)
{
    WorldSession* session = player ? player->GetSession() : nullptr;
    return session && session->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
}

player::BotFacts FactsOf(Player* requester, Player* bot, OwnerScope const& scope)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (scope.Owns(gameAccount))
        facts.account = scope.bnet;
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

std::string ClassName(uint8 classId)
{
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
    if (entry && entry->Name[LOCALE_enUS])
        return entry->Name[LOCALE_enUS];
    return "-";
}

std::string AreaName(uint32 areaId)
{
    char const* name = nullptr;
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId))
        name = area->AreaName[LOCALE_enUS];
    return (name && *name) ? name : "";
}

std::string RosterLocation(uint32 mapId, uint32 zoneId, uint32 areaId)
{
    uint32 const displayAreaId = areaId ? areaId : zoneId;
    std::string const name = displayAreaId ? AreaName(displayAreaId) : "";
    if (!name.empty())
        return name;
    if (!mapId && !zoneId && !areaId)
        return "-";
    return "map:" + std::to_string(mapId) + ",zone:" + std::to_string(zoneId) + ",area:" + std::to_string(areaId);
}

// The visible bots, oldest TB-row material first: owned alts and bots in the
// caller's group the caller may command, sorted by name like `.bot who`.
std::vector<Player*> VisibleBots(Player* requester, player::Caller const& caller, OwnerScope const& scope)
{
    std::vector<Player*> bots;
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
            continue;
        if (!RosterVisible(caller, FactsOf(requester, bot, scope)))
            continue;
        bots.push_back(bot);
    }
    std::sort(bots.begin(), bots.end(), [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
    return bots;
}

bool InRequesterGroup(Player* requester, Player* bot)
{
    Group* group = requester ? requester->GetGroup() : nullptr;
    return group && group->IsMember(bot->GetGUID());
}

RosterRow ToRosterRow(Player* bot, bool grouped)
{
    RosterRow row;
    row.guidLow = bot->GetGUID().GetCounter();
    row.name = bot->GetName();
    row.classId = bot->GetClass();
    row.state = RosterStateOf(true);
    row.grouped = grouped;
    row.location = RosterLocation(bot->GetMapId(), bot->GetZoneId(), bot->GetAreaId());
    row.className = ClassName(bot->GetClass());
    return row;
}

std::string TransportFor(Player* requester)
{
    // TB's AddonCommandChannel exactly: the pre-BG group first, then the
    // current one; a BG group (or none) means chat.
    Group* group = requester ? requester->GetOriginalGroup() : nullptr;
    bool const hadOriginal = group != nullptr;
    if (!group)
        group = requester ? requester->GetGroup() : nullptr;
    bool const bg = !hadOriginal && group && group->isBGGroup();
    bool const channelOn = sWorld->getBoolConfig(CONFIG_ADDON_CHANNEL);
    return TransportVerdict(channelOn, group != nullptr, bg);
}

void SendTrailer(Player* requester)
{
    addon::Send(requester, VersionLine(GitRevision::GetFullVersion()));
    addon::Send(requester, ProtoLine());
    addon::Send(requester, LatestLine());
    // Counted like ROSTER: the token list exceeds one 255-byte message, so
    // the channel would truncate a single line mid-token.
    std::vector<std::string> const verbs = addon::AdvertisedVerbs();
    addon::Send(requester, VerbsBegin(verbs.size()));
    for (std::string const& chunk : VerbsChunks(verbs))
        addon::Send(requester, chunk);
    addon::Send(requester, std::string(kVerbsEnd));
    addon::Send(requester, CapsLine(CapsFor(addon::AdvertisedCaps(), HasTbPermission(requester))));
    addon::Send(requester, TransportLine(TransportFor(requester)));
}

void HandleRoster(Player* requester, std::string_view /*args*/)
{
    if (!requester)
        return;
    player::Caller caller;
    caller.gm = HasTbPermission(requester);
    caller.account = requester->GetSession() ? requester->GetSession()->GetBattlenetAccountId() : 0;
    caller.guid = requester->GetGUID().GetCounter();
    OwnerScope const scope = ScopeOf(requester);
    caller.account = scope.bnet;

    std::vector<Player*> const bots = VisibleBots(requester, caller, scope);
    // A hire is not a roster row (TortoiseBots parity): its Logout would delete the character, so the
    // addon must not offer the roster's lifecycle buttons for it. It stays a bot target through
    // BOTSTATE below, so the party/bot panels keep working.
    std::vector<Player*> rosterBots;
    rosterBots.reserve(bots.size());
    for (Player* bot : bots)
        if (!HireLifecycle::Instance().IsHired(bot->GetGUID()))
            rosterBots.push_back(bot);
    std::vector<RosterRow> rows;
    rows.reserve(rosterBots.size());
    for (Player* bot : rosterBots)
        rows.push_back(ToRosterRow(bot, InRequesterGroup(requester, bot)));

    addon::Send(requester, RosterBegin(rows.size()));
    for (RosterRow const& row : rows)
        addon::Send(requester, RosterLine(row));
    addon::Send(requester, "TBM:ROSTER_END");

    // TBF has no autonomous-alt store yet; the line still rides along (empty)
    // so older parsers see TB's shape. Free bots are the closest TBF has to
    // alts playing on their own.
    std::vector<std::string> away;
    for (Player* bot : bots)
        if (BotMgr::Instance().IsFreeBot(bot->GetGUID()))
            away.push_back(bot->GetName());
    std::sort(away.begin(), away.end());
    addon::Send(requester, AltBotsLine(away));

    // The CC stream rides here (TB Info.cpp:501-505); f-info owns the lines.
    addoninfo::AppendCcAssign(requester);

    // No claimed-guild-bot store on TBF yet; same well-formed empty pair.
    addon::Send(requester, ClaimedBegin(0));
    addon::Send(requester, "TBM:CLAIMED_END");

    std::vector<BotStateLine> states;
    for (Player* bot : bots)
    {
        if (!InRequesterGroup(requester, bot))
            continue;
        BotStateLine state;
        state.name = bot->GetName();
        state.fields = "move=" + BotMoveWord(BotMgr::Instance().IsFreeBot(bot->GetGUID()),
            h3::LastOrder(bot->GetGUID()).verb);
        states.push_back(state);
    }
    addon::Send(requester, BotStateBegin(states.size()));
    for (BotStateLine const& state : states)
        addon::Send(requester, BotStateText(state));
    addon::Send(requester, "TBM:BOTSTATE_END");

    SendTrailer(requester);
}

void HandleAlts(Player* requester, std::string_view /*args*/)
{
    if (!requester)
        return;
    player::Caller caller;
    caller.gm = HasTbPermission(requester);
    caller.account = requester->GetSession() ? requester->GetSession()->GetBattlenetAccountId() : 0;
    caller.guid = requester->GetGUID().GetCounter();
    OwnerScope const scope = ScopeOf(requester);
    caller.account = scope.bnet;

    // TB reports alts playing on their own with progress sentences; TBF has
    // no away store, so each free owned bot reports one plain line.
    std::vector<AltSumLine> lines;
    for (Player* bot : VisibleBots(requester, caller, scope))
    {
        if (!BotMgr::Instance().IsFreeBot(bot->GetGUID()))
            continue;
        AltSumLine line;
        line.name = bot->GetName();
        line.text = bot->GetName() + " is playing on its own (free mode)";
        lines.push_back(line);
    }
    for (AltSumLine const& line : lines)
        addon::Send(requester, AltSumLineText(line));
    addon::Send(requester, AltSumEnd(lines.size()));
}
} // namespace

void RegisterAddonVerbsRoster()
{
    addon::RegisterVerb("roster", &HandleRoster);
    addon::RegisterVerb("list", &HandleRoster);
    addon::RegisterVerb("alts", &HandleAlts);
}
} // namespace roster
} // namespace TortoiseBots

#else   // no engine: the verbs stay unregistered and the channel answers unsupported

namespace TortoiseBots
{
namespace roster
{
void RegisterAddonVerbsRoster() { }
} // namespace roster
} // namespace TortoiseBots

#endif
