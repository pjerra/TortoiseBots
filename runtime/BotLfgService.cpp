#include "BotLfgService.h"
#include "BotActivityLease.h"
#include "LftBotFillService.h"
#include "runtime/policy/lfg/BotLfgSelectionPolicy.h"
#include "runtime/policy/lfg/BotLfgLifecyclePolicy.h"
#include "runtime/policy/lfg/LftDungeonRangePolicy.h"
#include "runtime/policy/hire/LftRoleKitPolicy.h"
#include "runtime/policy/lfg/BotLfgAdvertPolicy.h"
#include "BotLfgWhisperInbox.h"

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "RemoteCommanderPolicy.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/AiFactory.h"
#include "../ai/playerbot/DungeonPlanMgr.h"
#include "../commands/BotCommandHandlers.h"
#include "../commands/CommandShared.h"
#include "../host/ModuleLog.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat/ChannelMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat/Chat.h"
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"
#include "LFT/LFTMgr.h"
#ifndef MANGOSSERVER_LFTMGR_H
#error "TortoiseBots bot LFG needs Penqle core #416 (LFTMgr.h)"
#endif

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots
{

namespace
{

struct RealPlayer
{
    uint8 team = 0;
    uint8 level = 0;
};

std::vector<RealPlayer> RealPlayersOnline()
{
    std::vector<RealPlayer> out;
    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (!session || !session->HasNetworkTransport())
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            continue;
        RealPlayer p;
        p.team = player->GetTeam() == ALLIANCE ? BotLfg::kAlliance : BotLfg::kHorde;
        p.level = player->GetLevel();
        out.push_back(p);
    }
    return out;
}

} // namespace

BotLfgService& BotLfgService::Instance()
{
    static BotLfgService instance;
    return instance;
}

void BotLfgService::Initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;
    m_shutDown = false;
    uint32 n = LoadPlans();
    TB_LOG_BASIC("TortoiseBots: bot LFG: %u dungeon plans can form groups (%s)", n, Enabled() ? "on" : "off");
}

bool BotLfgService::Enabled() const
{
    return sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.botLfgGroups &&
        RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::System, BotCommands::CurrentCommanderGates());
}

uint32 BotLfgService::LoadPlans()
{
    m_plans.clear();
    if (!sPlayerbotAIConfig.botLfgGroups || !ai::GetDungeonPlans().Sealed())
    {
        TB_LOG_BASIC("TortoiseBots: bot LFG: plans not read (needs AiPlayerbot.BotLfgGroups = 1 and the dungeon plans)");
        return 0;
    }
    std::unique_ptr<QueryResult> rows(
        WorldDatabase.Query("SELECT `plan`, `lft_code`, `teams` FROM `ai_bot_lfg_plan` ORDER BY `plan`"));
    if (!rows)
    {
        sLog.outError("TortoiseBots: bot LFG has no proven plans (ai_bot_lfg_plan did not answer)");
        return 0;
    }
    do
    {
        Field* fields = rows->Fetch();
        std::string plan = fields[0].GetCppString();
        std::string code = fields[1].GetCppString();
        uint8 teams = fields[2].GetUInt8();
        ai::DungeonPlan const* def = ai::GetDungeonPlans().Find(plan);
        if (!def || def->steps.empty())
        {
            sLog.outError("ai_bot_lfg_plan: plan '%s' skipped (not a loaded plan)", plan.c_str());
            continue;
        }
        LftLevelRange const* range = LftDungeonRange(code);
        if (!range)
        {
            sLog.outError("ai_bot_lfg_plan: plan '%s' skipped (no LFT level range for '%s')", plan.c_str(), code.c_str());
            continue;
        }
        if (teams < 1 || teams > 3)
        {
            sLog.outError("ai_bot_lfg_plan: plan '%s' skipped (teams must be 1, 2 or 3)", plan.c_str());
            continue;
        }
        PlanRow row;
        row.plan = plan;
        row.lftCode = code;
        row.teams = teams;
        row.mapId = def->map;
        row.minLevel = range->minLevel;
        row.maxLevel = range->maxLevel;
        for (uint32 i = 0; i < sAreaTriggerStore.GetNumRows(); ++i)
        {
            AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(i);
            if (!at || at->destination.mapId != row.mapId)
                continue;
            AreaTriggerEntry const* entry = sAreaTriggerStore.LookupEntry(i);
            if (!entry)
                continue;
            MapEntry const* entryMap = sMapStorage.LookupEntry<MapEntry>(entry->mapid);
            if (!entryMap || entryMap->Instanceable())
                continue;
            row.entranceKnown = true;
            row.entranceMap = entry->mapid;
            row.entranceX = entry->x;
            row.entranceY = entry->y;
            break;
        }
        m_plans.push_back(row);
    } while (rows->NextRow());
    return uint32(m_plans.size());
}

std::vector<BotLfgService::Scanned> BotLfgService::ScanCandidates()
{
    std::vector<Scanned> out;
    std::unordered_map<std::string, uint32> skips;
    LftBotFillService& fill = LftBotFillService::Instance();
    for (Player* bot : BotManager::Instance().GetAllBots())
    {
        if (!bot)
            continue;
        BotLfg::CandidateFacts facts;
        facts.liveHeadless = bot->IsInWorld() && bot->GetSession() && bot->GetSession()->IsHeadless();
        facts.worldBot = BotCommands::IsLfgWorldBot(bot);
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            facts.playerMaster = ai->HasActivePlayerMaster();
        facts.hardcore = bot->IsHardcore();
        facts.grouped = bot->GetGroup() != nullptr;
        facts.bound = BotCommands::IsBotBound(bot);
        facts.leaseFree = BotActivityLeaseManager::Instance().IsAvailableForBackground(bot->GetGUIDLow());
        ObjectGuid guid = bot->GetObjectGuid();
        facts.lftQueued = sLFTMgr.IsQueued(guid) || sLFTMgr.IsInOffer(guid);
        facts.battleground = bot->InBattleGround() || bot->InBattleGroundQueue();
        facts.onInstanceMap = bot->GetMap() && bot->GetMap()->Instanceable();
        facts.alive = bot->IsAlive();
        facts.busy = bot->IsBeingTeleported() || bot->IsTaxiFlying() || bot->IsInCombat();
        facts.level = bot->GetLevel();
        BotLfg::Skip skip = BotLfg::CheckCandidate(facts);
        if (skip != BotLfg::Skip::None)
        {
            ++skips[BotLfg::SkipCode(skip)];
            continue;
        }
        uint8 mask = fill.GetBotRoleMask(bot);
        std::string tankWhy = (mask & kLftRoleTank) ? fill.RoleMismatchReason(bot, kLftRoleTank) : "no";
        Scanned s;
        s.bot = bot;
        s.guid = guid.GetRawValue();
        s.level = facts.level;
        s.team = bot->GetTeam() == ALLIANCE ? BotLfg::kAlliance : BotLfg::kHorde;
        s.tank = tankWhy.empty() || tankWhy == "tank-spec without shield equipped";
        s.shieldPending = !tankWhy.empty() && s.tank;
        s.healer = (mask & kLftRoleHealer) && fill.RoleMismatchReason(bot, kLftRoleHealer).empty();
        s.damage = (mask & kLftRoleDamage) && fill.RoleMismatchReason(bot, kLftRoleDamage).empty();
        if (!s.tank && !s.healer && !s.damage)
            continue;
        out.push_back(s);
    }
    std::string line =
        "TortoiseBots: bot LFG scan: " + std::to_string(uint32(out.size())) + " candidates; skipped:";
    if (skips.empty())
        line += " none";
    else
    {
        bool first = true;
        for (auto const& kv : skips)
        {
            line += (first ? " " : ", ") + std::string(kv.first) + "=" + std::to_string(kv.second);
            first = false;
        }
    }
    TB_LOG_DETAIL("%s", line.c_str());
    return out;
}

std::vector<BotLfg::Candidate> BotLfgService::CandidatesFor(std::vector<Scanned> const& scanned, uint8 team,
    uint8 minLevel, uint8 maxLevel, uint32 nearMap, float nearX, float nearY, bool nearKnown) const
{
    std::vector<BotLfg::Candidate> out;
    for (Scanned const& s : scanned)
    {
        if (s.team != team || !BotLfg::InBand(s.level, minLevel, maxLevel))
            continue;
        BotLfg::Candidate c;
        c.guid = s.guid;
        c.level = s.level;
        c.team = s.team;
        c.tank = s.tank;
        c.healer = s.healer;
        c.damage = s.damage;
        if (nearKnown && s.bot)
        {
            c.sameMap = s.bot->GetMapId() == nearMap;
            if (c.sameMap)
            {
                float dx = s.bot->GetPositionX() - nearX;
                float dy = s.bot->GetPositionY() - nearY;
                c.distance = std::sqrt(dx * dx + dy * dy);
            }
        }
        out.push_back(c);
    }
    BotLfg::SortCandidates(out);
    return out;
}

bool BotLfgService::ReadyForRole(Player* bot, BotLfg::Role role) const
{
    if (!bot)
        return false;
    LftBotFillService& fill = LftBotFillService::Instance();
    if (role == BotLfg::Role::Tank)
    {
        std::string why = fill.RoleMismatchReason(bot, kLftRoleTank);
        if (why.empty())
            return true;
        if (why == "tank-spec without shield equipped")
            return fill.EquipBestShieldFromBags(bot) && fill.RoleMismatchReason(bot, kLftRoleTank).empty();
        return false;
    }
    return true;
}

void BotLfgService::End(BotLfgGroup& group, BotLfgLifecycle::EndReason reason, bool release)
{
    StopAdvertising(group);
    release = release || RealPlayerInGroup(group);
    group.missingSinceMs.clear();
    for (BotLfgMember const& member : group.members)
    {
        Player* bot = ObjectAccessor::FindPlayer(member.guid);
        if (!release)
            if (bot && bot->GetGroup() && bot->GetGroup()->GetId() == group.groupId)
                bot->GetGroup()->RemoveMember(member.guid, 0);
        BotActivityLeaseManager::Instance().Release(member.guid.GetCounter(), BotActivity::Dungeon);
        if (member.forcedRole)
            LftBotFillService::Instance().ClearForcedRole(member.guid.GetCounter());
    }
    group.state = BotLfgLifecycle::State::Ended;
    PublishLeaders();
    Log(group, std::string(release ? "released: " : "ended: ") + BotLfgLifecycle::EndText(reason) +
        " (" + BotLfgLifecycle::EndCode(reason) + ")");
}

bool BotLfgService::TryForm(PlanRow const* only, uint8 onlyTeam, bool gm, std::string& code, std::string& message)
{
    (void)gm;
    if (m_groups.size() >= sPlayerbotAIConfig.botLfgMaxGroups)
    {
        code = "max-groups";
        message = "The most bot LFG groups (AiPlayerbot.BotLfgMaxGroups) are already formed.";
        return false;
    }
    std::vector<Scanned> scanned = ScanCandidates();
    std::unordered_map<uint64, Player*> byGuid;
    for (Scanned const& s : scanned)
        byGuid[s.guid] = s.bot;
    std::vector<RealPlayer> players = RealPlayersOnline();
    std::vector<BotLfg::PlanOption> options;
    std::vector<PlanRow const*> rows;
    std::vector<std::vector<BotLfg::Candidate>> lists;
    for (PlanRow const& row : m_plans)
    {
        if (only && only->plan != row.plan)
            continue;
        for (uint8 team : {BotLfg::kAlliance, BotLfg::kHorde})
        {
            if (!BotLfg::TeamAllowed(row.teams, team))
                continue;
            if (onlyTeam != 0 && team != onlyTeam)
                continue;
            BotLfg::PlanOption option;
            option.plan = row.plan;
            option.team = team;
            for (BotLfgGroup const& g : m_groups)
                if (g.state != BotLfgLifecycle::State::Ended && g.plan == row.plan && g.team == team)
                    option.busy = true;
            std::vector<BotLfg::Candidate> c = CandidatesFor(scanned, team, row.minLevel, row.maxLevel,
                row.entranceMap, row.entranceX, row.entranceY, row.entranceKnown);
            option.feasible = BotLfg::ComposeFull(c).ok;
            option.candidates = uint32(c.size());
            for (RealPlayer const& p : players)
                if (p.team == team && BotLfg::InBand(p.level, row.minLevel, row.maxLevel))
                    option.playerInBand = true;
            std::string key = row.plan + ":" + std::to_string(team);
            auto found = m_lastFormedMs.find(key);
            option.everFormed = found != m_lastFormedMs.end() && found->second.formed;
            option.sinceFormedMs = option.everFormed ? m_nowMs - found->second.atMs : 0;
            options.push_back(option);
            rows.push_back(&row);
            lists.push_back(std::move(c));
        }
    }
    int best = BotLfg::PickPlan(options);
    if (best < 0)
    {
        bool allBusy = only != nullptr && !options.empty();
        for (BotLfg::PlanOption const& o : options)
            allBusy = allBusy && o.busy;
        if (allBusy)
        {
            code = "busy";
            message = "That dungeon already has a bot LFG group of that faction.";
        }
        else
        {
            code = "no-bots";
            message = "No dungeon has five free bots of one faction in its level band right now.";
        }
        return false;
    }
    PlanRow const* row = rows[size_t(best)];
    uint8 team = options[size_t(best)].team;
    std::string key = row->plan + ":" + std::to_string(team);
    BotLfg::Role open = BotLfg::OpenRoleFor(m_formedCount);
    std::vector<BotLfg::Candidate> c = lists[size_t(best)];
    BotLfg::Composition comp;
    bool ready = false;
    for (int attempt = 0; attempt < 3 && !ready; ++attempt)
    {
        comp = BotLfg::ComposeWithOpen(c, open);
        if (!comp.ok)
            break;
        ready = true;
        for (BotLfg::Pick const& pick : comp.picks)
        {
            auto found = byGuid.find(c[pick.index].guid);
            Player* bot = found != byGuid.end() ? found->second : nullptr;
            if (bot && ReadyForRole(bot, pick.role))
                continue;
            ready = false;
            if (pick.role == BotLfg::Role::Tank)
                c[pick.index].tank = false;
            else if (pick.role == BotLfg::Role::Healer)
                c[pick.index].healer = false;
            else
                c[pick.index].damage = false;
        }
    }
    if (!ready)
    {
        code = "form-failed";
        message = "The picked bots could not take their roles.";
        return false;
    }
    LftBotFillService& fill = LftBotFillService::Instance();
    std::vector<Player*> bots;
    bots.reserve(comp.picks.size());
    for (BotLfg::Pick const& pick : comp.picks)
        bots.push_back(byGuid[c[pick.index].guid]);
    std::vector<uint32> held;
    for (Player* bot : bots)
    {
        uint32 low = bot->GetGUIDLow();
        if (!BotActivityLeaseManager::Instance().TryAcquire(low, BotActivity::Dungeon, 0))
        {
            for (uint32 done : held)
                BotActivityLeaseManager::Instance().Release(done, BotActivity::Dungeon);
            code = "form-failed";
            message = "The picked bots could not take their roles.";
            return false;
        }
        held.push_back(low);
    }
    std::vector<bool> forced(bots.size(), false);
    for (size_t i = 0; i < bots.size(); ++i)
    {
        uint8 bit = comp.picks[i].role == BotLfg::Role::Tank ? kLftRoleTank :
            (comp.picks[i].role == BotLfg::Role::Healer ? kLftRoleHealer : kLftRoleDamage);
        LftRoleKitInputs in{bit, fill.GetBotRoleMask(bots[i]), fill.GetCombatKitRoleMask(bots[i])};
        if (LftKitNeedsRoleAssignment(in))
        {
            fill.ApplyForcedRole(bots[i], bit);
            forced[i] = true;
        }
    }
    BotLfgGroup g;
    g.id = m_nextId++;
    g.plan = row->plan;
    g.lftCode = row->lftCode;
    g.mapId = row->mapId;
    g.team = team;
    g.minLevel = row->minLevel;
    g.maxLevel = row->maxLevel;
    g.openRole = open;
    g.state = BotLfgLifecycle::State::Forming;
    g.stateSinceMs = m_nowMs;
    int leaderAt = comp.leader >= 0 ? comp.leader : 0;
    Player* leader = bots[size_t(leaderAt)];
    g.leader = leader->GetObjectGuid();
    for (size_t i = 0; i < bots.size(); ++i)
    {
        BotLfgMember member;
        member.guid = bots[i]->GetObjectGuid();
        member.role = comp.picks[i].role;
        member.forcedRole = forced[i];
        g.members.push_back(member);
    }
    auto rollback = [&](std::string const& why)
    {
        if (Group* owned = leader->GetGroup())
            for (Player* bot : bots)
            {
                if (bot == leader)
                    continue;
                ObjectGuid guid = bot->GetObjectGuid();
                if (owned->IsMember(guid))
                    owned->RemoveMember(guid, 0);
            }
        if (Group* owned = leader->GetGroup())
            if (owned->IsMember(leader->GetObjectGuid()))
                owned->RemoveMember(leader->GetObjectGuid(), 0);
        for (uint32 done : held)
            BotActivityLeaseManager::Instance().Release(done, BotActivity::Dungeon);
        for (size_t i = 0; i < bots.size(); ++i)
            if (forced[i])
                fill.ClearForcedRole(bots[i]->GetGUIDLow());
        Log(g, why);
    };
    for (Player* bot : bots)
    {
        if (bot == leader)
            continue;
        if (!BotCommands::JoinBotToLeadGroup(leader, bot))
        {
            rollback("form failed: a picked bot could not be invited");
            code = "form-failed";
            message = "The picked bots could not be invited into one group.";
            return false;
        }
    }
    Group* grp = leader->GetGroup();
    if (!grp || grp->GetMembersCount() != uint32(bots.size()) || !grp->IsLeader(leader->GetObjectGuid()))
    {
        rollback("form failed: the group did not come together");
        code = "form-failed";
        message = "The picked bots could not be invited into one group.";
        return false;
    }
    g.groupId = grp->GetId();
    m_groups.push_back(g);
    ++m_formedCount;
    m_lastFormedMs[key] = FormStamp{m_nowMs, true};
    BotLfgGroup& stored = m_groups.back();
    std::string roster = "formed: leader " + std::string(leader->GetName()) + " (" +
        BotLfg::RoleName(comp.picks[size_t(leaderAt)].role) + ")";
    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (int(i) == leaderAt)
            continue;
        roster += ", " + std::string(bots[i]->GetName()) + " (" + BotLfg::RoleName(comp.picks[i].role) + ")";
    }
    roster += " ; open: " + std::string(BotLfg::RoleName(open)) + "; band " +
        std::to_string(uint32(stored.minLevel)) + "-" + std::to_string(uint32(stored.maxLevel));
    Log(stored, roster);
    Transition(stored, BotLfgLifecycle::State::Waiting);
    code = "ok";
    message = "group " + std::to_string(stored.id);
    return true;
}

bool BotLfgService::FormNow(std::string const& plan, uint8 team, std::string& code, std::string& message)
{
    if (!Enabled())
    {
        code = "disabled";
        message = "Bot LFG groups are off (AiPlayerbot.BotLfgGroups = 1 turns them on).";
        return false;
    }
    PlanRow const* row = nullptr;
    if (!plan.empty())
    {
        for (PlanRow const& candidate : m_plans)
            if (candidate.plan == plan)
            {
                row = &candidate;
                break;
            }
        if (!row)
        {
            code = "no-plan";
            message = "No dungeon of that name can form bot LFG groups (see ai_bot_lfg_plan).";
            return false;
        }
    }
    return TryForm(row, team, true, code, message);
}

bool BotLfgService::AddMember(BotLfgGroup& group, Player* leader, Player* bot, BotLfg::Role role)
{
    if (!leader || !bot || !ReadyForRole(bot, role))
        return false;
    uint32 low = bot->GetGUIDLow();
    if (!BotActivityLeaseManager::Instance().TryAcquire(low, BotActivity::Dungeon, 0))
        return false;
    bool forced = false;
    LftBotFillService& fill = LftBotFillService::Instance();
    uint8 bit = role == BotLfg::Role::Tank ? kLftRoleTank :
        (role == BotLfg::Role::Healer ? kLftRoleHealer : kLftRoleDamage);
    LftRoleKitInputs in{bit, fill.GetBotRoleMask(bot), fill.GetCombatKitRoleMask(bot)};
    if (LftKitNeedsRoleAssignment(in))
    {
        fill.ApplyForcedRole(bot, bit);
        forced = true;
    }
    if (!BotCommands::JoinBotToLeadGroup(leader, bot))
    {
        BotActivityLeaseManager::Instance().Release(low, BotActivity::Dungeon);
        if (forced)
            fill.ClearForcedRole(low);
        return false;
    }
    BotLfgMember member;
    member.guid = bot->GetObjectGuid();
    member.role = role;
    member.forcedRole = forced;
    group.members.push_back(member);
    return true;
}

bool BotLfgService::FillNow(uint32 id, std::string& code, std::string& message)
{
    BotLfgGroup* found = nullptr;
    for (BotLfgGroup& g : m_groups)
        if (g.state != BotLfgLifecycle::State::Ended && g.id == id)
            found = &g;
    if (!found)
    {
        code = "no-group";
        message = "No bot LFG group has that id.";
        return false;
    }
    if (found->state != BotLfgLifecycle::State::Waiting)
    {
        code = "not-waiting";
        message = "Only a waiting group takes a bot.";
        return false;
    }
    found->forceFill = true;
    BotLfg::Role const role = found->openRole;
    size_t const before = found->members.size();
    TickGroup(*found, Enabled());
    std::string botName;
    if (found->filledBy == BotLfgLifecycle::FillKind::Bot && found->members.size() > before)
        if (Player* bot = ObjectAccessor::FindPlayer(found->members.back().guid))
            botName = bot->GetName();
    std::string playerName;
    if (found->filledBy == BotLfgLifecycle::FillKind::Player)
    {
        if (Player* joined = ObjectAccessor::FindPlayer(found->player))
            playerName = joined->GetName();
        else if (Player* leader = ObjectAccessor::FindPlayer(found->leader))
        {
            if (Group* grp = leader->GetGroup())
            {
                for (Group::MemberSlot const& slot : grp->GetMemberSlots())
                {
                    if (BotManager::Instance().IsBot(slot.guid))
                        continue;
                    if (Player* player = ObjectAccessor::FindPlayer(slot.guid))
                    {
                        playerName = player->GetName();
                        break;
                    }
                }
            }
        }
    }
    code = "ok";
    message = BotLfgLifecycle::FillNowAnswer(found->state, found->filledBy, botName, playerName,
        BotLfg::RoleName(role));
    if (found->state == BotLfgLifecycle::State::Ended)
        m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
            [](BotLfgGroup const& g) { return g.state == BotLfgLifecycle::State::Ended; }), m_groups.end());
    return true;
}

bool BotLfgService::EndNow(uint32 id, std::string& code, std::string& message)
{
    BotLfgGroup* found = nullptr;
    for (BotLfgGroup& g : m_groups)
        if (g.state != BotLfgLifecycle::State::Ended && g.id == id)
            found = &g;
    if (!found)
    {
        code = "no-group";
        message = "No bot LFG group has that id.";
        return false;
    }
    End(*found, BotLfgLifecycle::EndReason::GmDisband, RealPlayerInGroup(*found));
    code = "ok";
    message = "group " + std::to_string(found->id);
    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
        [](BotLfgGroup const& g) { return g.state == BotLfgLifecycle::State::Ended; }), m_groups.end());
    return true;
}
void BotLfgService::Transition(BotLfgGroup& group, BotLfgLifecycle::State to)
{
    StopAdvertising(group);
    group.state = to;
    group.stateSinceMs = m_nowMs;
    Log(group, std::string("now ") + BotLfgLifecycle::StateName(to));
    PublishLeaders();
}

bool BotLfgService::TrackOutOfWorld(BotLfgGroup& group, ObjectGuid guid, Player* bot)
{
    uint64 raw = guid.GetRawValue();
    if (bot && bot->IsInWorld())
    {
        group.missingSinceMs.erase(raw);
        return false;
    }
    auto seen = group.missingSinceMs.find(raw);
    if (seen == group.missingSinceMs.end())
        group.missingSinceMs.emplace(raw, m_nowMs);
    return BotLfgLifecycle::MissingGraceExpired(group.missingSinceMs[raw], m_nowMs);
}

bool BotLfgService::MemberMissing(BotLfgGroup& group, Player* bot, ObjectGuid guid, Group* grp)
{
    if (bot && bot->IsInWorld() && grp && bot->GetGroup() == grp &&
        BotActivityLeaseManager::Instance().GetActivity(guid.GetCounter()) == BotActivity::Dungeon)
        return false;
    if (bot && bot->IsInWorld())
        return true;
    return TrackOutOfWorld(group, guid, bot);
}

bool BotLfgService::LeaderMissing(BotLfgGroup& group, Player* leader, Group* grp, bool ours)
{
    if (!leader)
        return TrackOutOfWorld(group, group.leader, leader);
    if (BotActivityLeaseManager::Instance().GetActivity(leader->GetGUIDLow()) != BotActivity::Dungeon || !ours ||
        !grp->IsLeader(group.leader))
        return true;
    return TrackOutOfWorld(group, group.leader, leader);
}

void BotLfgService::TickGroups(bool enabled)
{
    for (BotLfgGroup& group : m_groups)
        if (group.state != BotLfgLifecycle::State::Ended)
            TickGroup(group, enabled);
}

void BotLfgService::TickGroup(BotLfgGroup& group, bool enabled)
{
    if (group.state == BotLfgLifecycle::State::Ended)
        return;
    Player* leader = ObjectAccessor::FindPlayer(group.leader);
    Group* grp = leader ? leader->GetGroup() : nullptr;
    bool const ours = grp && grp->GetId() == group.groupId;
    BotLfgLifecycle::Facts facts;
    facts.state = group.state;
    facts.elapsedMs = m_nowMs - group.stateSinceMs;
    facts.fillAfterMs = sPlayerbotAIConfig.botLfgFillAfterMinutes * 60000;
    facts.forceFill = group.forceFill;
    facts.enabled = enabled;
    uint64 key = BotCommands::GroupBindingKey(group.groupId);
    facts.boundByOther = ours && key && key != group.bindingKey;
    facts.realPlayerInGroup = RealPlayerInGroup(group);
    facts.leaderLost = LeaderMissing(group, leader, grp, ours);
    BotLfg::Role lostRole = BotLfg::Role::None;
    ObjectGuid lostGuid;
    for (BotLfgMember const& member : group.members)
    {
        if (member.guid == group.leader)
            continue;
        if (!MemberMissing(group, ObjectAccessor::FindPlayer(member.guid), member.guid, grp))
            continue;
        ++facts.lostMembers;
        if (facts.lostMembers == 1)
        {
            lostRole = member.role;
            lostGuid = member.guid;
        }
    }
    uint32 nearMap = group.mapId;
    float nearX = 0.0f, nearY = 0.0f;
    bool nearKnown = false;
    if (leader && leader->IsInWorld())
    {
        nearMap = leader->GetMapId();
        nearX = leader->GetPositionX();
        nearY = leader->GetPositionY();
        nearKnown = true;
    }
    std::vector<Scanned> scanned;
    std::unordered_map<uint64, Player*> byGuid;
    Player* fillBot = nullptr;
    Player* replaceBot = nullptr;
    bool fillDue = BotLfgLifecycle::FillDue(group.state, facts.elapsedMs, facts.fillAfterMs, group.forceFill);
    if (facts.lostMembers > 0 && !facts.leaderLost)
    {
        scanned = ScanCandidates();
        for (Scanned const& s : scanned)
            byGuid[s.guid] = s.bot;
        std::vector<BotLfg::Candidate> c = CandidatesFor(scanned, group.team, group.minLevel, group.maxLevel,
            nearMap, nearX, nearY, nearKnown);
        std::vector<bool> taken(c.size(), false);
        int at = BotLfg::PickForRole(c, lostRole, taken);
        if (at >= 0)
        {
            auto found = byGuid.find(c[size_t(at)].guid);
            if (found != byGuid.end())
                replaceBot = found->second;
            facts.replacementFound = replaceBot != nullptr;
        }
    }
    else if (!facts.leaderLost && fillDue && facts.lostMembers == 0)
    {
        scanned = ScanCandidates();
        for (Scanned const& s : scanned)
            byGuid[s.guid] = s.bot;
        std::vector<BotLfg::Candidate> c = CandidatesFor(scanned, group.team, group.minLevel, group.maxLevel,
            nearMap, nearX, nearY, nearKnown);
        std::vector<bool> taken(c.size(), false);
        int at = BotLfg::PickForRole(c, group.openRole, taken);
        if (at >= 0)
        {
            auto found = byGuid.find(c[size_t(at)].guid);
            if (found != byGuid.end())
                fillBot = found->second;
            facts.fillCandidateFound = fillBot != nullptr;
        }
    }
    facts.invitePending = InviteHeld(group);
    BotLfgLifecycle::Decision decision = BotLfgLifecycle::DecideStep(facts);
    switch (decision.step)
    {
        case BotLfgLifecycle::Step::ToWaiting:
            Transition(group, BotLfgLifecycle::State::Waiting);
            break;
        case BotLfgLifecycle::Step::ToReady:
        {
            group.filledBy = BotLfgLifecycle::FillKind::Player;
            for (Group::MemberSlot const& slot : grp->GetMemberSlots())
            {
                if (BotManager::Instance().IsBot(slot.guid))
                    continue;
                group.player = slot.guid;
                break;
            }
            if (Player* joined = ObjectAccessor::FindPlayer(group.player))
                Log(group, std::string("a player joined: ") + joined->GetName());
            else
                Log(group, "a player joined");
            Transition(group, BotLfgLifecycle::State::Ready);
            break;
        }
        case BotLfgLifecycle::Step::FillWithBot:
            if (fillBot && AddMember(group, leader, fillBot, group.openRole))
            {
                group.filledBy = BotLfgLifecycle::FillKind::Bot;
                group.forceFill = false;
                if (group.openRole == BotLfg::Role::Tank)
                {
                    grp->ChangeLeader(fillBot->GetObjectGuid());
                    group.leader = fillBot->GetObjectGuid();
                }
                Log(group, std::string("filled by a bot: ") + fillBot->GetName() + " (" +
                    BotLfg::RoleName(group.openRole) + ")");
                Transition(group, BotLfgLifecycle::State::Ready);
            }
            else
                TB_LOG_DETAIL("TortoiseBots: bot LFG group %u: fill failed, staying waiting", group.id);
            break;
        case BotLfgLifecycle::Step::Replace:
        {
            for (std::vector<BotLfgMember>::iterator it = group.members.begin(); it != group.members.end(); ++it)
            {
                if (!(it->guid == lostGuid))
                    continue;
                if (grp && grp->IsMember(it->guid))
                    grp->RemoveMember(it->guid, 0);
                BotActivityLeaseManager::Instance().Release(it->guid.GetCounter(), BotActivity::Dungeon);
                if (it->forcedRole)
                    LftBotFillService::Instance().ClearForcedRole(it->guid.GetCounter());
                group.members.erase(it);
                break;
            }
            if (replaceBot && AddMember(group, leader, replaceBot, lostRole))
                Log(group, std::string("replaced a member: ") + replaceBot->GetName() + " (" +
                    BotLfg::RoleName(lostRole) + ")");
            else
                End(group, BotLfgLifecycle::EndReason::MemberLost,
                    BotLfgLifecycle::ReleaseOnEnd(facts.realPlayerInGroup, BotLfgLifecycle::EndReason::MemberLost));
            break;
        }
        case BotLfgLifecycle::Step::End:
            End(group, decision.reason, decision.release);
            break;
        case BotLfgLifecycle::Step::None:
            break;
    }
    if (!facts.invitePending && !group.invitee.IsEmpty() && !facts.realPlayerInGroup)
    {
        Player* waiting = ObjectAccessor::FindPlayer(group.invitee);
        Group* pending = waiting ? waiting->GetGroupInvite() : nullptr;
        if (pending && pending->GetId() == group.groupId)
            pending->RemoveInvite(waiting);
        if (waiting)
            Log(group, std::string("invite to ") + waiting->GetName() + " lapsed");
        else
            Log(group, "invite lapsed");
        group.invitee = ObjectGuid();
        group.inviteAtMs = 0;
    }
}

bool BotLfgService::RealPlayerInGroup(BotLfgGroup const& group) const
{
    if (group.groupId == 0)
        return false;
    Group* grp = sObjectMgr.GetGroupById(group.groupId);
    if (!grp)
        return false;
    for (Group::MemberSlot const& slot : grp->GetMemberSlots())
        if (!BotManager::Instance().IsBot(slot.guid))
            return true;
    return false;
}

bool BotLfgService::Advertising(BotLfgGroup const& group) const
{
    return group.state == BotLfgLifecycle::State::Waiting && sPlayerbotAIConfig.botLfgChat;
}

bool BotLfgService::RealPlayerOfTeamOnline(uint8 team) const
{
    for (RealPlayer const& player : RealPlayersOnline())
        if (player.team == team)
            return true;
    return false;
}

void BotLfgService::AdvertiseGroups()
{
    if (!sPlayerbotAIConfig.botLfgChat)
    {
        PublishLeaders();
        return;
    }
    for (BotLfgGroup& group : m_groups)
    {
        if (group.state != BotLfgLifecycle::State::Waiting)
            continue;
        Player* leader = ObjectAccessor::FindPlayer(group.leader);
        if (!leader)
            continue;
        if (!RealPlayerOfTeamOnline(group.team))
            continue;
        if (!BotLfgAdvert::PostDue(m_nowMs, group.posts > 0, group.lastPostMs, m_channelPosted[group.team],
            m_channelPostMs[group.team]))
            continue;
        PostInChannel(group, leader);
    }
    PublishLeaders();
}

void BotLfgService::PostInChannel(BotLfgGroup& group, Player* leader)
{
    std::string const text = BotLfgAdvert::PostText(group.posts, BotLfgAdvert::DungeonName(group.plan),
        group.openRole, group.minLevel, group.maxLevel);
    ChannelMgr* mgr = channelMgr(leader->GetTeam());
    Channel* channel = mgr ? mgr->GetOrCreateChannel(BotLfgAdvert::kChannel) : nullptr;
    if (!channel)
        return;
    channel->Join(leader->GetObjectGuid(), "", false);
    channel->AsyncSay(leader->GetObjectGuid(), text.c_str(), LANG_UNIVERSAL);
    ++group.posts;
    group.lastPostMs = m_nowMs;
    m_channelPostMs[group.team] = m_nowMs;
    m_channelPosted[group.team] = true;
    Log(group, "posted in LookingForGroup: " + text);
}

void BotLfgService::StopAdvertising(BotLfgGroup& group)
{
    BotLfgWhisperInbox::Instance().WithdrawLeader(group.leader.GetRawValue());
    Player* invitee = group.invitee.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(group.invitee);
    Group* invited = invitee ? invitee->GetGroupInvite() : nullptr;
    if (invited && invited->GetId() == group.groupId)
        invited->RemoveInvite(invitee);
    group.invitee = ObjectGuid();
    group.inviteAtMs = 0;
    group.repliedAtMs.clear();
    group.hintedAtMs.clear();
}

void BotLfgService::PublishLeaders() const
{
    std::vector<uint64_t> leaders;
    for (BotLfgGroup const& group : m_groups)
        if (Advertising(group))
            leaders.push_back(group.leader.GetRawValue());
    BotLfgWhisperInbox::Instance().PublishLeaders(leaders);
}

void BotLfgService::HandleWhispers()
{
    for (LfgWhisper const& whisper : BotLfgWhisperInbox::Instance().Drain())
        HandleWhisper(whisper);
    if (size_t dropped = BotLfgWhisperInbox::Instance().TakeDropped())
        TB_LOG_DETAIL("TortoiseBots: bot LFG: %u whispers dropped (inbox full)", uint32(dropped));
}

BotLfgGroup* BotLfgService::FindByLeader(ObjectGuid leader)
{
    for (BotLfgGroup& group : m_groups)
    {
        if (group.state == BotLfgLifecycle::State::Ended)
            continue;
        if (group.leader == leader)
            return &group;
    }
    return nullptr;
}

void BotLfgService::Reply(Player* leader, Player* to, std::string const& text) const
{
    if (text.empty())
        return;
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_UNIVERSAL, leader->GetChatTag(),
        leader->GetObjectGuid(), leader->GetName());
    to->GetSession()->SendPacket(&data);
}

bool BotLfgService::InviteHeld(BotLfgGroup const& group) const
{
    Player* invitee = group.invitee.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(group.invitee);
    Group* invited = invitee ? invitee->GetGroupInvite() : nullptr;
    return BotLfgAdvert::InvitePending(!group.invitee.IsEmpty(), invited && invited->GetId() == group.groupId,
        m_nowMs, group.inviteAtMs);
}

void BotLfgService::HandleWhisper(LfgWhisper const& whisper)
{
    BotLfgGroup* group = FindByLeader(ObjectGuid(whisper.leader));
    Player* leader = ObjectAccessor::FindPlayer(ObjectGuid(whisper.leader));
    Player* sender = ObjectAccessor::FindPlayer(ObjectGuid(whisper.sender));
    if (!group || !leader || !sender)
        return;
    if (!(sender->GetSession() && sender->GetSession()->HasNetworkTransport() &&
            !BotManager::Instance().IsBot(sender->GetObjectGuid())))
        return;
    if (sender->GetGroup() && sender->GetGroup()->GetId() == group->groupId)
        return;
    BotLfgAdvert::WhisperFacts facts;
    facts.sameTeam = (sender->GetTeam() == ALLIANCE ? BotLfg::kAlliance : BotLfg::kHorde) == group->team;
    uint64 key = whisper.sender;
    auto replied = group->repliedAtMs.find(key);
    facts.repliedRecently =
        replied != group->repliedAtMs.end() && m_nowMs - replied->second < BotLfgAdvert::kReplyGapMs;
    auto hinted = group->hintedAtMs.find(key);
    facts.hintedRecently =
        hinted != group->hintedAtMs.end() && m_nowMs - hinted->second < BotLfgAdvert::kHintGapMs;
    facts.inviteRequest = BotLfgAdvert::IsInviteRequest(whisper.text);
    facts.waiting = Advertising(*group);
    facts.invitePending = InviteHeld(*group);
    facts.senderInvited = group->invitee == sender->GetObjectGuid() && facts.invitePending;
    auto sent = group->invitesSent.find(key);
    facts.senderRefusedResend = sent != group->invitesSent.end() &&
        sent->second >= BotLfgAdvert::kMaxInvitesPerGroup && !facts.senderInvited;
    facts.hardcore = sender->IsHardcore();
    facts.grouped = sender->GetGroup() != nullptr;
    facts.level = sender->GetLevel();
    facts.minLevel = group->minLevel;
    facts.maxLevel = group->maxLevel;
    std::map<uint32, int32> tabs = AiFactory::GetPlayerSpecTabs(sender);
    uint32 points = uint32(tabs[0] + tabs[1] + tabs[2]);
    facts.fitsRole = BotLfgAdvert::PlayerFitsRole(sender->GetClass(),
        LftBotFillService::Instance().GetBotRoleMask(sender), points, group->openRole);
    BotLfgAdvert::Answer answer = BotLfgAdvert::DecideWhisper(facts);
    if (answer == BotLfgAdvert::Answer::Ignore)
        return;
    if (answer == BotLfgAdvert::Answer::Invite)
    {
        WorldPacket invite;
        invite << sender->GetName();
        invite << uint32(0);
        leader->GetSession()->HandleGroupInviteOpcode(invite);
        Group* pending = sender->GetGroupInvite();
        if (pending && pending->GetId() == group->groupId)
        {
            group->invitee = sender->GetObjectGuid();
            group->inviteAtMs = m_nowMs;
            auto counted = group->invitesSent.find(key);
            if (counted == group->invitesSent.end() || counted->second < BotLfgAdvert::kMaxInvitesPerGroup)
                ++group->invitesSent[key];
            Log(*group, std::string("invited ") + sender->GetName() + " (whisper)");
        }
        else
            answer = BotLfgAdvert::Answer::InviteFailed;
    }
    Reply(leader, sender,
        BotLfgAdvert::ReplyText(answer, BotLfgAdvert::DungeonName(group->plan), group->openRole, group->minLevel,
            group->maxLevel));
    group->repliedAtMs[key] = m_nowMs;
    if (answer == BotLfgAdvert::Answer::Hint)
        group->hintedAtMs[key] = m_nowMs;
    TB_LOG_DETAIL("TortoiseBots: bot LFG group %u: whisper from %s: %s", group->id, sender->GetName(),
        BotLfgAdvert::AnswerCode(answer));
}

void BotLfgService::Log(BotLfgGroup const& group, std::string const& text) const
{
    TB_LOG_BASIC("%s", BotLfgLifecycle::LogLine(group.id, group.plan, BotLfg::TeamName(group.team), text).c_str());
}

void BotLfgService::Update(uint32 diff)
{
    if (!m_initialized)
        return;
    HandleWhispers();
    m_elapsedMs += diff;
    if (m_elapsedMs < BotLfgLifecycle::kTickMs)
        return;
    m_nowMs += m_elapsedMs;
    m_sinceFormMs += m_elapsedMs;
    m_elapsedMs = 0;
    bool const enabled = Enabled();
    if (enabled && !m_wasEnabled && m_plans.empty())
        LoadPlans();
    m_wasEnabled = enabled;
    TickGroups(enabled);
    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
        [](BotLfgGroup const& g) { return g.state == BotLfgLifecycle::State::Ended; }), m_groups.end());
    if (enabled)
        AdvertiseGroups();
    else
        PublishLeaders();
    if (!enabled)
        return;
    uint32 players = uint32(RealPlayersOnline().size());
    uint32 target = BotLfg::TargetGroups(uint32(BotManager::Instance().GetAllBots().size()),
        sPlayerbotAIConfig.botLfgGroupsPer100Bots, sPlayerbotAIConfig.botLfgMaxGroups, players,
        sPlayerbotAIConfig.botLfgOnlyWithPlayersOnline);
    if (!BotLfg::FormDue(uint32(m_groups.size()), target, m_sinceFormMs))
        return;
    m_sinceFormMs = 0;
    std::string code, message;
    if (!TryForm(nullptr, 0, false, code, message))
        TB_LOG_DETAIL("TortoiseBots: bot LFG: no group formed (%s: %s)", code.c_str(), message.c_str());
}

void BotLfgService::Shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    for (BotLfgGroup& g : m_groups)
    {
        if (g.state == BotLfgLifecycle::State::Ended)
            continue;
        End(g, BotLfgLifecycle::EndReason::Shutdown, RealPlayerInGroup(g));
    }
    m_groups.clear();
    BotLfgWhisperInbox::Instance().PublishLeaders({});
    m_initialized = false;
}

BotLfgGroup const* BotLfgService::FindById(uint32 id) const
{
    for (BotLfgGroup const& g : m_groups)
        if (g.state != BotLfgLifecycle::State::Ended && g.id == id)
            return &g;
    return nullptr;
}

BotLfgGroup const* BotLfgService::FindByMember(ObjectGuid guid) const
{
    for (BotLfgGroup const& g : m_groups)
    {
        if (g.state == BotLfgLifecycle::State::Ended)
            continue;
        for (BotLfgMember const& member : g.members)
            if (member.guid == guid)
                return &g;
    }
    return nullptr;
}

std::vector<BotLfgGroup const*> BotLfgService::Groups() const
{
    std::vector<BotLfgGroup const*> out;
    for (BotLfgGroup const& g : m_groups)
        if (g.state != BotLfgLifecycle::State::Ended)
            out.push_back(&g);
    return out;
}

} // namespace TortoiseBots
