// Commander port, packages P1 (the binding card) and P2 (binding lifecycle and persistence): see TbCmdBinding.h. The decisions are in
// TbCmdBindingLifePure.h and TbCmdBindingPure.h and are tested standalone; this file reads the live objects, runs the lifecycle hooks and writes the
// replies.
#include "host/cmd/TbCmdBinding.h"

#include "Chat.h"

#include <cctype>
#include <string>
#include <string_view>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "compat/botpch_forever.h"
#include "compat/TbCompat_Unit.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBindingStore.h"
#include "host/run/TbRun.h"

#include "CharacterCache.h"
#include "Config.h"
#include "GameTime.h"
#include "WorldSession.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"
#include "World.h"

#include "runtime/BotManager.h"

#include "playerbot/GuidPosition.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "PlayerbotAIStorage.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_set>

namespace TortoiseBots
{
namespace cmd
{
namespace
{
namespace P = ::TortoiseBots::cmd::pure;
namespace L = ::TortoiseBots::cmd::life;
namespace CT = ::TortoiseBots::CommanderTelemetry;
namespace RC = ::TortoiseBots::RemoteCommander;

constexpr uint32 kRoleRefreshMs = 10000;     // a bot's role is re-read from its brain this often (DescribeBrain builds lines)

// One follower whose manual follow target the binding pointed at the lead, with the value it had before (restored on release unless something else
// changed it meanwhile - the shared rule, T Commander.cpp:1022).
struct Follower
{
    ObjectGuid guid;
    ai::GuidPosition saved;
};

// What the module keeps BESIDE h3's table (decision D1): character guids and times only (decision D2), never a group number. `groupId` is the lead's
// live group counter (GroupIdW2), in memory only and refreshed from the lead (the in-memory key the spec allows; the persisted row has no group field).
struct Meta
{
    ObjectGuid lead;                        // the party leader `.tb master party <leader> <commander>` named, else the first bound bot
    int64 boundAt = 0;                      // unix seconds the card was first seen standing
    uint32 flags = 0;                       // L::Flag*
    uint32 groupId = 0;                     // GroupIdW2 of the lead's live group, never persisted
    std::vector<ObjectGuid> members;        // the stored member list (lead first): what a restart restores
    std::vector<Follower> followers;        // bound bots whose follow target the binding set
    int64 leadDeadSinceMs = 0;              // 0 = alive, or dead now
    int64 commanderGoneSinceMs = 0;         // 0 = in the world
    bool restored = true;                   // false: a stored row whose party is not back yet
};

struct State
{
    std::map<ObjectGuid, Meta> metas;               // by commander
    P::OrderStore log;                              // the cards' last orders, by card key; no whole-store commander cap
    struct Role { std::string role; uint32 atMs = 0; };
    std::map<ObjectGuid, Role> roles;
    int64 maintainAtMs = 0;                         // the last 1 Hz upkeep tick (Maintain is called on every world tick)
    bool releasing = false;                         // this module's own party cleanup is running: its group events are not new release reasons
};

State& Get()
{
    static State state;
    return state;
}

uint32 Ctr(ObjectGuid guid)
{
    return uint32(guid.GetCounter());
}

ObjectGuid PlayerGuid(uint64 counter)
{
    return ObjectGuid::Create<HighGuid::Player>(counter);
}

Player* InWorld(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

int64 NowUnix() { return int64(GameTime::GetGameTime()); }
int64 NowMs() { return int64(GameTime::GetGameTimeMS()); }

// The Turtle record's group id is the group's session counter (decision D2: GroupIdW2, `compat/TbCompat_W2.h`; never GetDbStoreId, which is reused).
uint32 GroupKey(Group const* group)
{
    return group ? uint32(group->GetGUID().GetCounter()) : 0u;
}

char const* ClassName(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "unknown";
    }
}

char const* PowerName(Powers power)
{
    switch (power)
    {
        case POWER_MANA: return "mana";
        case POWER_RAGE: return "rage";
        case POWER_FOCUS: return "focus";
        case POWER_ENERGY: return "energy";
        default: return "";
    }
}

// The roster's display values: rage and runic power are stored in tenths.
uint32 ShownPower(Powers power, int32 value)
{
    int32 const shown = power == POWER_RAGE || power == POWER_RUNIC_POWER ? value / 10 : value;
    return shown < 0 ? 0u : uint32(shown);
}

std::string RoleOf(Player* bot, uint32 nowMs)
{
    State::Role& cached = Get().roles[bot->GetGUID()];
    if (cached.atMs == 0 || nowMs - cached.atMs >= kRoleRefreshMs)
    {
        cached.role = P::RoleOfBrainLines(BotMgr::Instance().DescribeBrain(bot->GetGUID()));
        cached.atMs = nowMs ? nowMs : 1;
    }
    return cached.role;
}

std::string NameOf(ObjectGuid guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
        return player->GetName();
    std::string name;
    if (sCharacterCache->GetCharacterNameByGuid(guid, name))
        return name;
    return "offline #" + std::to_string(Ctr(guid));
}

P::BotView BotOf(Player* player, Group* group, bool lead, uint32 nowMs)
{
    P::BotView m;
    m.name = player->GetName();
    m.guid = Ctr(player->GetGUID());
    m.className = ClassName(player->GetClass());
    m.role = BotMgr::Instance().IsBot(player->GetGUID()) ? RoleOf(player, nowMs) : std::string();
    Powers const power = player->GetPowerType();
    m.powerType = PowerName(power);
    m.level = player->GetLevel();
    m.hp = uint32(player->GetHealth());
    m.maxHp = uint32(player->GetMaxHealth());
    m.power = ShownPower(power, player->GetPower(power));
    m.maxPower = ShownPower(power, player->GetMaxPower(power));
    m.alive = player->IsAlive();
    m.lead = lead;
    m.map = player->GetMapId();
    m.zone = player->GetZoneId();
    m.x = player->GetPositionX();
    m.y = player->GetPositionY();
    m.z = player->GetPositionZ();
    m.subgroup = group && group->isRaidGroup() ? int(group->GetMemberGroup(player->GetGUID())) : -1;
    return m;
}

// ---- the binding records: h3's table is authoritative, these are what a restart restores ----

std::vector<ObjectGuid> BoundTo(ObjectGuid commander)
{
    std::vector<ObjectGuid> bots;
    for (auto const& [bot, cmd] : h3::CommanderBindings())
        if (cmd == commander)
            bots.push_back(bot);
    return bots;
}

L::Stored RowOf(ObjectGuid commander, Meta const& m)
{
    L::Stored row;
    row.commander = commander.GetCounter();
    row.lead = m.lead.GetCounter();
    for (ObjectGuid member : m.members)
        row.members.push_back(member.GetCounter());
    row.flags = m.flags;
    row.boundAt = m.boundAt;
    return row;
}

// A follower's own follow target goes back, unless something else changed it while it was bound (T's rule).
void RestoreFollower(Follower const& follower, ObjectGuid lead)
{
    Player* bot = InWorld(follower.guid);
    if (!bot || lead.IsEmpty())
        return;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot->GetGUID());
    if (!ai || !ai->GetAiObjectContext())
        return;
    ai::Value<ai::GuidPosition>* value = ai->GetAiObjectContext()->GetValue<ai::GuidPosition>("manual follow target");
    if (!value)
        return;
    ObjectGuid const current = value->Get();
    if (!(current == lead))
        return;
    value->Set(follower.saved);
    if (ai::Value<Unit*>* follow = ai->GetAiObjectContext()->GetValue<Unit*>("follow target"))
        follow->Reset();
}

// One follower's follow target is pointed at the lead; its own value is kept once, the way T does it.
void PointFollowerAtLead(Meta& m, Player* bot, Player* lead)
{
    if (!bot || !lead || bot == lead)
        return;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot->GetGUID());
    if (!ai || !ai->GetAiObjectContext())
        return;
    ai::Value<ai::GuidPosition>* value = ai->GetAiObjectContext()->GetValue<ai::GuidPosition>("manual follow target");
    if (!value)
        return;
    auto const it = std::find_if(m.followers.begin(), m.followers.end(), [&](Follower const& f) { return f.guid == bot->GetGUID(); });
    if (it == m.followers.end())
    {
        Follower follower;
        follower.guid = bot->GetGUID();
        follower.saved = value->Get();
        ObjectGuid const hadTarget = follower.saved;
        if (hadTarget == lead->GetGUID())
            follower.saved = ai::GuidPosition();
        m.followers.push_back(std::move(follower));
    }
    value->Set(ai::GuidPosition(lead));
    if (ai::Value<Unit*>* follow = ai->GetAiObjectContext()->GetValue<Unit*>("follow target"))
        follow->Reset();
}

// The followers of a binding now: every bot h3 binds to the commander except the lead; a follower no longer bound (or the old lead after a lead change)
// gets its own follow target back.
void SyncFollowers(ObjectGuid commander, Meta& m, ObjectGuid previousLead)
{
    std::vector<ObjectGuid> const bound = BoundTo(commander);
    ObjectGuid const restoreLead = previousLead.IsEmpty() ? m.lead : previousLead;
    for (auto it = m.followers.begin(); it != m.followers.end();)
    {
        bool keep = it->guid != m.lead && std::find(bound.begin(), bound.end(), it->guid) != bound.end();
        if (!keep)
        {
            RestoreFollower(*it, restoreLead);
            it = m.followers.erase(it);
        }
        else
            ++it;
    }
    Player* lead = InWorld(m.lead);
    if (!lead)
        return;
    for (ObjectGuid bot : bound)
        if (bot != m.lead)
            if (Player* player = InWorld(bot))
                PointFollowerAtLead(m, player, lead);
}

void SaveRow(ObjectGuid commander, Meta const& m)
{
    L::Stored const row = RowOf(commander, m);
    if (row.members.empty() || !L::Valid(row))
        return;
    store::Save(row);
}

// ---- the release ----

// The group a release cleans up: the lead's own when it is the bound one, else the in-memory session counter, else any bound member's (T's
// ReleaseGroupSource; the counter is GroupIdW2, never the reused database id).
Group* ReleaseGroup(ObjectGuid commander, Meta const& m)
{
    Player* lead = InWorld(m.lead);
    Group* leadGroup = lead ? lead->GetGroup() : nullptr;
    Group* byId = m.groupId ? sGroupMgr->GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(m.groupId)) : nullptr;
    Group* viaMember = nullptr;
    for (ObjectGuid bot : BoundTo(commander))
        if (Player* player = InWorld(bot))
            if (Group* group = player->GetGroup())
                if (!m.groupId || Ctr(group->GetGUID()) == m.groupId)
                {
                    viaMember = group;
                    break;
                }
    RC::ReleaseGroupFacts facts;
    facts.leadGroupMatches = leadGroup && m.groupId && Ctr(leadGroup->GetGUID()) == m.groupId;
    facts.groupById = byId != nullptr;
    facts.groupViaMember = viaMember != nullptr;
    switch (RC::PickReleaseGroup(facts))
    {
        case RC::ReleaseGroupSource::Lead: return leadGroup;
        case RC::ReleaseGroupSource::ById: return byId;
        case RC::ReleaseGroupSource::Member: return viaMember;
        case RC::ReleaseGroupSource::None: break;
    }
    return nullptr;
}

// An in-party binding's end: with the commander gone the bots leave the party (so no other player becomes their master) and the lead goes to a living
// player of it; with him there the lead goes back to him. A classic binding has nothing to do.
void ApplyPartyAction(ObjectGuid commander, Meta const& m, L::End end)
{
    Group* group = ReleaseGroup(commander, m);
    if (!group)
        return;
    Player* commanderPlayer = InWorld(commander);
    RC::ReleaseCommanderFacts facts;
    facts.inParty = true;
    facts.reason = L::PolicyReason(end);
    facts.found = [&] { std::string name; return sCharacterCache->GetCharacterNameByGuid(commander, name); }();
    facts.inWorld = commanderPlayer != nullptr;
    facts.hasSession = commanderPlayer && commanderPlayer->GetSession() && HasNetworkTransportCompat(commanderPlayer->GetSession());
    facts.isMember = group->IsMember(commander);
    facts.leadFound = InWorld(m.lead) && group->IsMember(m.lead);
    facts.playerLed = (m.flags & L::FlagPlayerLed) != 0;
    switch (RC::DecideReleaseParty(facts))
    {
        case RC::ReleasePartyAction::BotsLeave:
        {
            if (group->IsLeader(m.lead))
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    if (slot.guid == commander || BotMgr::Instance().IsBot(slot.guid))
                        continue;
                    Player* player = ObjectAccessor::FindPlayer(slot.guid);
                    if (player && player->IsInWorld() && player->GetSession() && HasNetworkTransportCompat(player->GetSession()))
                    {
                        group->ChangeLeader(slot.guid);
                        break;
                    }
                }
            std::vector<ObjectGuid> leaving;
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (BotMgr::Instance().IsBot(slot.guid))
                    leaving.push_back(slot.guid);
            for (ObjectGuid const& guid : leaving)
                if (group->IsMember(guid))
                    group->RemoveMember(guid);
            break;
        }
        case RC::ReleasePartyAction::LeadToCommander:
            if (group->IsLeader(m.lead) && commanderPlayer && group->IsMember(commander))
                group->ChangeLeader(commander);
            break;
        case RC::ReleasePartyAction::None:
            break;
    }
}

// The card keys that stand now (defined with the card read below): what a release must leave in the order log.
std::unordered_set<uint64> StandingKeys();

void ReleaseCommander(ObjectGuid commander, L::End end)
{
    State& s = Get();
    auto it = s.metas.find(commander);
    if (it == s.metas.end())
        return;
    Meta const m = std::move(it->second);
    s.metas.erase(it);
    bool const wasReleasing = s.releasing;
    s.releasing = true;
    if (m.flags & L::FlagInParty)
        ApplyPartyAction(commander, m, end);
    for (Follower const& follower : m.followers)
        RestoreFollower(follower, m.lead);
    for (ObjectGuid bot : BoundTo(commander))
    {
        // A released bot must not keep its former commander as its master (review fix: it kept following and teleporting to him; the `.tb master
        // none` command clears the master the same way).
        TortoiseBots::BotManager::Instance().ClearBotMaster(bot);
        h3::SetCommander(bot, ObjectGuid::Empty);
    }
    store::Erase(commander.GetCounter());
    s.releasing = wasReleasing;
    // The released card's order log goes with it at once, whether the dashboard reads the cards or not.
    s.log.Retain(StandingKeys());
    sLog.outString("TortoiseBots: the commander binding of %s was released: %s (%s)", NameOf(commander).c_str(), L::EndText(end), L::EndCode(end));
}

// ---- the live facts and the 1 Hz upkeep ----

// The lead's live group is the binding's group (spec section 4): a group re-created around the lead and a bound follower keeps the binding, and only the
// in-memory counter is refreshed - nothing is compared with a stored group number.
void RefreshGroupId(Meta& m, Player* lead)
{
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (!group)
        return;
    bool holdsFollower = m.followers.empty();
    for (Follower const& follower : m.followers)
        if (group->IsMember(follower.guid))
        {
            holdsFollower = true;
            break;
        }
    if (holdsFollower)
        m.groupId = Ctr(group->GetGUID());
}

L::Facts FactsFor(ObjectGuid commander, Meta& m, Player* commanderPlayer, Player* lead)
{
    L::Facts facts;
    facts.enabled = BotMgr::Instance().Enabled();
    facts.leadInWorld = lead != nullptr;
    if (lead)
    {
        facts.leadAlive = lead->IsAlive();
        if (!facts.leadAlive && m.leadDeadSinceMs)
            facts.leadDeadMs = std::max<int64>(0, NowMs() - m.leadDeadSinceMs);
        Group* group = lead->GetGroup();
        facts.leadGroup = group != nullptr;
        facts.leadGroupHoldsFollower = m.followers.empty();
        if (group)
        {
            for (Follower const& follower : m.followers)
                if (group->IsMember(follower.guid))
                {
                    facts.leadGroupHoldsFollower = true;
                    break;
                }
            facts.leadIsLeader = group->IsLeader(lead->GetGUID());
            facts.groupIsRaidOrBg = group->isRaidGroup() || group->isBGGroup();
            facts.groupIsBg = group->isBGGroup();
            facts.memberCount = group->GetMembersCount();
            facts.commanderInGroup = commanderPlayer && group->IsMember(commanderPlayer->GetGUID());
            // A real player is a member IN THE WORLD who is no host bot: a saved group can hold a bot character that has not logged in yet
            // (BotMgr only knows the bots that are in), and that member cannot act in the party either.
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (slot.guid != commander && !BotMgr::Instance().IsBot(slot.guid))
                {
                    Player* member = ObjectAccessor::FindPlayer(slot.guid);
                    if (member && member->IsInWorld())
                    {
                        facts.realPlayerInGroup = true;
                        break;
                    }
                }
        }
    }
    facts.leadDeadGraceMs = store::LeadDeadGraceMs();
    facts.commanderOnline = commanderPlayer != nullptr;
    facts.commanderGraceMs = store::CommanderGraceMs();
    if (!facts.commanderOnline && m.commanderGoneSinceMs)
        facts.commanderGoneMs = std::max<int64>(0, NowMs() - m.commanderGoneSinceMs);
    RefreshGroupId(m, lead);
    return facts;
}

// May this character own bots at all? This host has no durable ownership table (BotManager::GetOwnedCharacter answers false), so the two
// authoritative relations it does keep decide: a real network player session (never a headless or bot session), or a host bot of this module (the
// master table a bot's followers read). Anything else named by a stored row is a stale, corrupted or hand-inserted commander and is refused.
bool AuthorizedCommander(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    if (HasNetworkTransportCompat(player->GetSession()))
        return true;
    return BotMgr::Instance().IsBot(player->GetGUID());
}

// The stored row is dead: nothing of it is restored, and the row goes with it (the same "do less" rule as a refused row).
void RefuseRow(ObjectGuid commander, char const* why)
{
    sLog.outError("TortoiseBots: stored commander binding of %s: refused (%s)", NameOf(commander).c_str(), why);
    Get().metas.erase(commander);
    store::Erase(commander.GetCounter());
}

void RestoreOne(ObjectGuid commander)
{
    State& s = Get();
    auto const it = s.metas.find(commander);
    if (it == s.metas.end())
        return;
    Meta& m = it->second;
    if (m.lead.IsEmpty())
        return;     // a card record made by Read (a run's tank), not a binding this module restores
    Player* commanderPlayer = InWorld(commander);
    if (!commanderPlayer || !AuthorizedCommander(commanderPlayer))
        return;
    // THE AUTHORIZATION (review fix): every stored member is checked against what the host says now. A member bound to another commander, mastered
    // by another character, or not a host bot at all is never joined, grouped or bound by this row; it is dropped from the row instead. A lead the
    // host no longer gives this commander ends the row whole.
    std::vector<uint64_t> storedMembers;
    std::vector<L::Owner> owners;
    for (ObjectGuid member : m.members)
    {
        storedMembers.push_back(member.GetCounter());
        if (Player* player = InWorld(member))
            owners.push_back({ member.GetCounter(), h3::CommanderGuid(member).GetCounter(), BotMgr::Instance().MasterOf(member).GetCounter(),
                BotMgr::Instance().IsBot(member) });
    }
    L::Claims const claims = L::Claim(commander.GetCounter(), storedMembers, owners);
    if (std::find(claims.foreign.begin(), claims.foreign.end(), m.lead.GetCounter()) != claims.foreign.end())
    {
        RefuseRow(commander, "its lead is commanded elsewhere or is no host bot");
        return;
    }
    if (!claims.foreign.empty())
    {
        m.members.erase(std::remove_if(m.members.begin(), m.members.end(), [&](ObjectGuid member) {
            return std::find(claims.foreign.begin(), claims.foreign.end(), member.GetCounter()) != claims.foreign.end();
        }), m.members.end());
        SaveRow(commander, m);
    }
    std::vector<uint64_t> online;
    for (ObjectGuid member : m.members)
        if (InWorld(member))
            online.push_back(member.GetCounter());
    L::Stored const row = RowOf(commander, m);
    L::RestorePlan const plan = L::PlanRestore(row, true, online);
    if (!plan.ready)
        return;
    Player* lead = InWorld(m.lead);
    if (!lead)
        return;
    bool joined = true;
    for (uint64_t counter : plan.join)
    {
        ObjectGuid const bot = PlayerGuid(counter);
        Group* leadGroup = lead->GetGroup();
        if (leadGroup && leadGroup->IsMember(bot))
            continue;
        std::string const error = BotMgr::Instance().GroupWith(bot, m.lead);
        if (!error.empty())
        {
            joined = false;
            sLog.outError("TortoiseBots: stored commander binding of %s not restored yet: %s could not join %s: %s",
                NameOf(commander).c_str(), NameOf(bot).c_str(), NameOf(m.lead).c_str(), error.c_str());
        }
    }
    if (!joined)
        return;
    for (uint64_t counter : plan.bind)
    {
        ObjectGuid const bot = PlayerGuid(counter);
        if (!InWorld(bot) || h3::CommanderGuid(bot) == commander)
            continue;
        if (!h3::CommanderGuid(bot).IsEmpty())
            continue;       // handed to another commander between the check and the bind: never taken
        if (!TortoiseBots::BotManager::Instance().BindBotMaster(bot, commander))
            continue;
        h3::SetCommander(bot, commander);
    }
    // An in-party binding puts the commander back into the party: without it the release rule reads him gone from the party and ends the binding
    // on the next tick (review fix). A party that refuses him ends it honestly with the same reason.
    if ((m.flags & L::FlagInParty) && commanderPlayer)
        if (Group* group = InWorld(m.lead) ? InWorld(m.lead)->GetGroup() : nullptr)
            if (!group->IsMember(commanderPlayer->GetGUID()) && group->IsCreated())
                group->AddMember(commanderPlayer);
    // The binding stands once the party it was made on exists again: with the lead alone in the world (no group yet) the restore waits for the next
    // stored member's login, which forms the group - a party cannot be made of one bot, and the release rule would end a groupless binding.
    Player* restoredLead = InWorld(m.lead);
    m.restored = (restoredLead && restoredLead->GetGroup()) || m.members.size() <= 1;
    SyncFollowers(commander, m, ObjectGuid::Empty);
    RefreshGroupId(m, InWorld(m.lead));
    sLog.outString("TortoiseBots: the commander binding of %s was restored (lead %s, %u bot(s))", NameOf(commander).c_str(), NameOf(m.lead).c_str(),
        uint32(BoundTo(commander).size()));
}

void TryRestore()
{
    if (!store::Enabled())
        return;
    std::vector<ObjectGuid> commanders;
    for (auto const& [commander, m] : Get().metas)
    {
        if (m.lead.IsEmpty())
            continue;
        if (!m.restored)
        {
            commanders.push_back(commander);
            continue;
        }
        // A member of the stored row that is back in the world and UNBOUND (it was offline at the restore): join it now. A member bound to some
        // commander - this one or, after a `.tb master`, another - is never touched here: taking it back is the review's steal.
        for (ObjectGuid member : m.members)
            if (InWorld(member) && h3::CommanderGuid(member).IsEmpty())
            {
                commanders.push_back(commander);
                break;
            }
    }
    for (ObjectGuid commander : commanders)
        RestoreOne(commander);
}
}

// ---- P2 hooks ----

void Maintain()
{
    State& s = Get();
    if (s.metas.empty())
        return;
    // The world tick calls this 20 times a second; a fourth of the releases it decides and every restore step only need one tick a second (review
    // fix: an unrestored party re-ran GroupWith and its log line on every world tick).
    static constexpr int64 kMaintainMs = 1000;
    int64 const nowMs = NowMs();
    if (s.maintainAtMs && nowMs >= s.maintainAtMs && nowMs - s.maintainAtMs < kMaintainMs)
        return;
    s.maintainAtMs = nowMs;
    TryRestore();
    std::vector<ObjectGuid> commanders;
    for (auto const& [commander, m] : s.metas)
        // A record with no lead is a card the read made for a run's tank (no `.tb master` named its lead): its bindings belong to the run, and
        // releasing them here would clear the run's h3 pairs (review fix).
        if (m.restored && !m.lead.IsEmpty())
            commanders.push_back(commander);
    for (ObjectGuid commander : commanders)
    {
        auto const it = s.metas.find(commander);
        if (it == s.metas.end())
            continue;
        Meta& m = it->second;
        Player* commanderPlayer = InWorld(commander);
        Player* lead = InWorld(m.lead);
        if (commanderPlayer)
            m.commanderGoneSinceMs = 0;
        if (lead && lead->IsAlive())
            m.leadDeadSinceMs = 0;
        L::Facts const facts = FactsFor(commander, m, commanderPlayer, lead);
        L::End const end = L::Decide(RowOf(commander, m), facts);
        if (end != L::End::None)
        {
            ReleaseCommander(commander, end);
            continue;
        }
    }
}

void LoadBindings()
{
    store::LoadConfig();
    State& s = Get();
    if (!store::Enabled())
        return;
    for (L::Stored const& row : store::Rows())
    {
        ObjectGuid const commander = PlayerGuid(row.commander);
        if (auto const it = s.metas.find(commander); it != s.metas.end() && it->second.restored)
            continue;   // a live binding stands; a config reload never turns it back into a pending row
        Meta& m = s.metas[commander];
        m.lead = PlayerGuid(row.lead);
        m.boundAt = row.boundAt;
        m.flags = row.flags;
        m.members.clear();
        for (uint64_t member : row.members)
            m.members.push_back(PlayerGuid(member));
        m.restored = false;
    }
    TryRestore();
}

void NoteGroupAdd(Group* group, ObjectGuid guid)
{
    State& s = Get();
    // The world's own shutdown takes the bots out of their groups: that is not a release, the row is what the next start restores.
    if (World::IsStopped() || s.releasing || !group)
        return;
    std::vector<ObjectGuid> releases;
    for (auto const& [commander, m] : s.metas)
    {
        if (!m.restored)
            continue;
        Player* lead = InWorld(m.lead);
        bool const sameGroup = (m.groupId && Ctr(group->GetGUID()) == m.groupId) || (lead && lead->GetGroup() == group);
        if (!sameGroup || guid == commander || guid == m.lead || BotMgr::Instance().IsBot(guid))
            continue;
        if (m.flags & L::FlagInParty)
            continue;
        releases.push_back(commander);
    }
    for (ObjectGuid commander : releases)
        ReleaseCommander(commander, L::End::PlayerJoined);
}

void NoteGroupRemove(Group* group, ObjectGuid guid)
{
    State& s = Get();
    // The world's own shutdown takes the bots out of their groups: that is not a release, the row is what the next start restores.
    if (World::IsStopped() || s.releasing || !group)
        return;
    std::vector<ObjectGuid> releases;
    std::vector<ObjectGuid> changed;
    for (auto& [commander, m] : s.metas)
    {
        if (!m.restored)
            continue;
        Player* lead = InWorld(m.lead);
        bool const sameGroup = (m.groupId && Ctr(group->GetGUID()) == m.groupId) || (lead && lead->GetGroup() == group);
        if (!sameGroup)
            continue;
        if (guid == m.lead)
            releases.push_back(commander);
        else
            for (auto it = m.followers.begin(); it != m.followers.end(); ++it)
                if (it->guid == guid)
                {
                    // A follower that left the party gets its own follow target back; the rest of the binding stands.
                    RestoreFollower(*it, m.lead);
                    m.followers.erase(it);
                    changed.push_back(commander);
                    break;
                }
    }
    for (ObjectGuid commander : releases)
        ReleaseCommander(commander, L::End::GroupChanged);
    for (ObjectGuid commander : changed)
        if (auto it = s.metas.find(commander); it != s.metas.end())
            SaveRow(commander, it->second);
}

void NoteGroupLeader(Group* group, ObjectGuid newLeader)
{
    State& s = Get();
    // The world's own shutdown takes the bots out of their groups: that is not a release, the row is what the next start restores.
    if (World::IsStopped() || s.releasing || !group)
        return;
    std::vector<ObjectGuid> releases;
    for (auto const& [commander, m] : s.metas)
    {
        if (!m.restored)
            continue;
        Player* lead = InWorld(m.lead);
        bool const sameGroup = (m.groupId && Ctr(group->GetGUID()) == m.groupId) || (lead && lead->GetGroup() == group);
        if (!sameGroup || newLeader == m.lead || (m.flags & L::FlagPlayerLed))
            continue;
        releases.push_back(commander);
    }
    for (ObjectGuid commander : releases)
        ReleaseCommander(commander, L::End::LeaderChanged);
}

void NoteGroupDisband(Group* group)
{
    State& s = Get();
    // The world's own shutdown takes the bots out of their groups: that is not a release, the row is what the next start restores.
    if (World::IsStopped() || s.releasing || !group)
        return;
    std::vector<ObjectGuid> releases;
    for (auto const& [commander, m] : s.metas)
    {
        if (!m.restored)
            continue;
        Player* lead = InWorld(m.lead);
        bool const sameGroup = (m.groupId && Ctr(group->GetGUID()) == m.groupId) || (lead && lead->GetGroup() == group);
        if (sameGroup)
            releases.push_back(commander);
    }
    for (ObjectGuid commander : releases)
        ReleaseCommander(commander, L::End::GroupChanged);
}

void NoteDeath(Player* player)
{
    if (!player)
        return;
    for (auto& [commander, m] : Get().metas)
        if (m.restored && m.lead == player->GetGUID() && !m.leadDeadSinceMs)
            m.leadDeadSinceMs = NowMs();
}

void NoteLogin(Player* player)
{
    if (!player)
        return;
    State& s = Get();
    for (auto& [commander, m] : s.metas)
    {
        if (commander == player->GetGUID())
            m.commanderGoneSinceMs = 0;
        if (m.lead == player->GetGUID() && player->IsAlive())
            m.leadDeadSinceMs = 0;
    }
    TryRestore();
    // A saved party is attached to a character at login without an AddMember event, so a real player coming back into a bound party is caught here
    // (a classic binding ends; the lead bot's own members are bots).
    if (!BotMgr::Instance().IsBot(player->GetGUID()))
    {
        std::vector<ObjectGuid> releases;
        for (auto const& [commander, m] : s.metas)
        {
            if (!m.restored || (m.flags & L::FlagInParty) || commander == player->GetGUID())
                continue;
            Player* lead = InWorld(m.lead);
            Group* group = lead ? lead->GetGroup() : nullptr;
            if (group && group->IsMember(player->GetGUID()))
                releases.push_back(commander);
        }
        for (ObjectGuid commander : releases)
            ReleaseCommander(commander, L::End::PlayerJoined);
    }
}

void NoteLogout(Player* player)
{
    if (!player || World::IsStopped())
        return;     // a server shutdown keeps every binding and its row: that is what a restore is for
    State& s = Get();
    std::vector<ObjectGuid> releases;
    for (auto& [commander, m] : s.metas)
    {
        if (commander == player->GetGUID())
            m.commanderGoneSinceMs = m.commanderGoneSinceMs ? m.commanderGoneSinceMs : NowMs();   // the bots wait for their master's grace
        else if (m.lead == player->GetGUID())
            releases.push_back(commander);
    }
    for (ObjectGuid commander : releases)
        ReleaseCommander(commander, L::End::LeadLeft);
}

bool IsCommandedBy(Player* commander, Player* bot)
{
    if (!commander || !bot)
        return false;
    State const& s = Get();
    auto const it = s.metas.find(commander->GetGUID());
    if (it == s.metas.end() || !it->second.restored)
        return false;
    // The record is only half the answer: h3's table must bind this very bot to this very commander too (review fix: a party member commanded by
    // someone else, or a real player in the lead's party, answered true).
    if (h3::CommanderGuid(bot->GetGUID()) != commander->GetGUID())
        return false;
    Player* lead = InWorld(it->second.lead);
    Group* group = lead ? lead->GetGroup() : nullptr;
    return group && group->IsMember(bot->GetGUID());
}

bool IsInOwnBindingGroup(Player* commander, Player* bot)
{
    return IsCommandedBy(commander, bot);
}

// ---- The card read (P1) ----

namespace
{
// One card with everything the replies and the dashboard need.
struct HostCard
{
    P::Card card;
    P::CardView view;
    run::RunRec const* run = nullptr;
    bool inParty = false, raid = false, playerLed = false;
    uint32 botsBound = 0;       // guids bound, online or not (view.bots holds the ones in the world)
};

// The commander key a bot's orders are logged under: the card it is shown on. A bot bound to a run's tank shows on the card of the tank's own commander
// when it has one (the run keeps its members' old commander for the end), else on the tank's system card.
ObjectGuid CardKeyOf(ObjectGuid commander)
{
    if (commander.IsEmpty())
        return commander;
    run::RunRec const* rec = run::Find(commander);
    if (rec && !run::Ended(*rec))
    {
        ObjectGuid const above = h3::CommanderGuid(commander);
        return above.IsEmpty() ? commander : above;
    }
    return commander;
}

// The card an order for this bot is logged under. A bot bound to a commander shows on that commander's card (a run's fold included); a run's leader the
// run never rebound (a system run) stands on the run's own card, keyed by itself.
ObjectGuid CardKeyOfBot(ObjectGuid bot)
{
    ObjectGuid const commander = h3::CommanderGuid(bot);
    if (!commander.IsEmpty())
        return CardKeyOf(commander);
    run::RunRec const* rec = run::Find(bot);
    return rec && !run::Ended(*rec) ? bot : ObjectGuid::Empty;
}

// The card keys that stand now: every valid binding's card and every run that goes on. The same keys BuildCards returns, so the state kept beside h3's
// table is pruned from the whole collection and never from a capped view. A stored row whose party is not back yet is not pruned: its restore is pending.
std::unordered_set<uint64> StandingKeys()
{
    std::unordered_set<uint64> keep;
    for (auto const& binding : h3::CommanderBindings())
    {
        ObjectGuid const key = CardKeyOf(binding.second);
        if (!key.IsEmpty())
            keep.insert(Ctr(key));
    }
    for (run::RunRec* rec : run::All())
        if (!run::Ended(*rec))
            keep.insert(Ctr(CardKeyOf(rec->leader)));
    for (auto const& [commander, m] : Get().metas)
        if (!m.restored)
            keep.insert(Ctr(commander));
    return keep;
}

// A released binding (or a rebind that leaves a commander no bot) takes its record, its order log and its repeat suppression with it at once, not only at
// the next read. A dropped binding's stored row goes too: left behind, the next start would restore a binding nobody stands behind any more (a commander
// displaced by another `.tb master`, review fix).
void PruneToStanding(State& s)
{
    std::unordered_set<uint64> const keep = StandingKeys();
    for (auto it = s.metas.begin(); it != s.metas.end();)
    {
        if (keep.count(Ctr(it->first)))
        {
            ++it;
            continue;
        }
        // Only a card with nothing behind it loses its row: a commander another commander's card folded in (a chain: T commands bots and C commands T)
        // has no card key of its own, but its h3 pairs still stand and the next restart restores them from that row.
        if (!it->second.lead.IsEmpty() && BoundTo(it->first).empty())
            store::Erase(Ctr(it->first));
        it = s.metas.erase(it);
    }
    s.log.Retain(keep);
}

std::vector<HostCard> Read(int64 nowUnix, uint32 nowMs)
{
    State& s = Get();
    std::vector<P::Pair> pairs;
    for (auto const& [bot, commander] : h3::CommanderBindings())
        pairs.push_back({ Ctr(bot), Ctr(commander) });

    std::vector<run::RunRec const*> runRecs;
    std::vector<P::RunIn> runs;
    for (run::RunRec* rec : run::All())
    {
        if (run::Ended(*rec))
            continue;
        P::RunIn r;
        r.leader = Ctr(rec->leader);
        r.leaderCommander = Ctr(h3::CommanderGuid(rec->leader));
        r.index = runRecs.size();
        if (Player* lead = InWorld(rec->leader))
            if (Group* group = lead->GetGroup())
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                    P::AddUnique(r.members, Ctr(slot.guid));
        // While the leader loads or teleports the live group is not readable: the run's own bound members still say who is on its card. BuildCards keeps
        // only the candidates h3's table binds to the leader, so neither list can add a human player or an unbound bot.
        for (auto const& member : rec->bound)
            P::AddUnique(r.members, Ctr(member.first));
        runRecs.push_back(rec);
        runs.push_back(std::move(r));
    }

    // A card seen for the first time gets its boundAt now (T: "the unix second the binding was first seen standing").
    auto const touch = [&](uint32 commander)
    {
        if (!commander)
            return;
        Meta& m = s.metas[PlayerGuid(commander)];
        if (!m.boundAt)
            m.boundAt = nowUnix;
    };
    for (P::Pair const& p : pairs)
        touch(p.commander);
    for (P::RunIn const& r : runs)
        touch(r.leaderCommander ? r.leaderCommander : r.leader);
    PruneToStanding(s);

    std::vector<P::MetaIn> metas;
    for (auto const& [commander, m] : s.metas)
        if (m.restored)
            metas.push_back({ Ctr(commander), Ctr(m.lead), m.boundAt });
    std::map<uint32, uint32> groupLeader;
    for (P::Pair const& p : pairs)
        if (Player* bot = InWorld(PlayerGuid(p.bot)))
            if (Group* group = bot->GetGroup())
                groupLeader[p.bot] = Ctr(group->GetLeaderGUID());

    std::vector<P::Card> const cards = P::BuildCards(pairs, runs, metas, groupLeader);

    for (auto it = s.roles.begin(); it != s.roles.end();)
        it = nowMs - it->second.atMs > 60000 ? s.roles.erase(it) : std::next(it);

    std::vector<HostCard> out;
    for (P::Card const& c : cards)
    {
        HostCard h;
        h.card = c;
        h.run = c.run >= 0 ? runRecs[size_t(c.run)] : nullptr;
        h.botsBound = uint32(c.bots.size());
        uint32 const flags = [&] {
            auto const it = s.metas.find(PlayerGuid(c.commander));
            return it == s.metas.end() ? 0u : it->second.flags;
        }();
        h.inParty = (flags & L::FlagInParty) != 0;
        h.raid = (flags & L::FlagRaid) != 0;
        h.playerLed = (flags & L::FlagPlayerLed) != 0;
        P::CardView& v = h.view;
        ObjectGuid const commanderGuid = PlayerGuid(c.commander);
        Player* commander = InWorld(commanderGuid);
        v.commanderGuid = c.commander;
        v.commanderName = NameOf(commanderGuid);
        v.commanderOnline = commander != nullptr;
        if (commander)
        {
            v.commanderPlaced = true;
            v.commanderMap = commander->GetMapId();
            v.commanderX = commander->GetPositionX();
            v.commanderY = commander->GetPositionY();
            v.commanderZ = commander->GetPositionZ();
        }
        Player* lead = InWorld(PlayerGuid(c.lead));
        Group* group = lead ? lead->GetGroup() : nullptr;
        v.groupId = GroupKey(group);
        v.leadName = NameOf(PlayerGuid(c.lead));
        v.boundAt = c.boundAt;
        for (uint32 guid : c.bots)
            if (Player* bot = InWorld(PlayerGuid(guid)))
                v.bots.push_back(BotOf(bot, bot->GetGroup(), guid == c.lead, nowMs));
        v.orders = s.log.Recent(c.commander);
        out.push_back(std::move(h));
    }
    return out;
}

void NoteLog(ObjectGuid commander, CT::OrderRecord rec)
{
    if (commander.IsEmpty())
        return;
    Get().log.Note(Ctr(commander), std::move(rec));
}
}

// ---- hooks ----

void NoteMaster(std::vector<Player*> const& bots, Player* leader, Player* commander)
{
    (void)bots;
    State& s = Get();
    int64 const now = NowUnix();
    if (!commander)
    {
        // The command released the bots it addressed; a commander that has no bot left loses its record and its row now (the party action runs while
        // the group is still there).
        std::vector<ObjectGuid> addressed;
        for (Player* bot : bots)
            if (bot)
                addressed.push_back(bot->GetGUID());
        std::vector<ObjectGuid> commanders;
        for (auto const& [cmd, m] : s.metas)
            commanders.push_back(cmd);
        for (ObjectGuid cmd : commanders)
        {
            auto const it = s.metas.find(cmd);
            if (it == s.metas.end())
                continue;
            Meta& m = it->second;
            // A card the read made for a run's tank (no `.tb master` named its lead) is not a binding of this module: its h3 pairs belong to the run.
            if (m.lead.IsEmpty() && m.members.empty())
                continue;
            // Only the bots this command really released leave the row: the stored row follows h3's table, so a restart does not bring one back, but a
            // member that is merely offline (or another commander's) stays for its own restore.
            size_t const before = m.members.size();
            m.members.erase(std::remove_if(m.members.begin(), m.members.end(),
                [&](ObjectGuid member) {
                    return std::find(addressed.begin(), addressed.end(), member) != addressed.end() && h3::CommanderGuid(member) != cmd;
                }), m.members.end());
            if (m.members.empty())
            {
                // h3 has no bot of this commander left: nothing stands behind the record, whatever the row said.
                ReleaseCommander(cmd, L::End::Unbound);
                continue;
            }
            // A released lead hands the binding to a follower that is still bound (the old code kept it in the row and in m.lead).
            ObjectGuid const previousLead = m.lead;
            if (h3::CommanderGuid(m.lead) != cmd)
                m.lead = BoundTo(cmd).empty() ? m.members.front() : BoundTo(cmd).front();
            if (m.lead != previousLead)
                SyncFollowers(cmd, m, previousLead);       // the followers pointed at the released lead get their own targets back
            if (m.members.size() != before || m.lead != previousLead)
                SaveRow(cmd, m);
        }
        return;
    }
    // Only what h3 holds now counts: a bot the host refused is not bound, whatever the command asked.
    std::vector<ObjectGuid> const bound = BoundTo(commander->GetGUID());
    if (bound.empty())
        return;
    Meta& m = s.metas[commander->GetGUID()];
    // A pending row whose party never came back is REPLACED by this explicit bind: its old (offline) lead is not part of what just bound, and keeping
    // it would make the new row invalid and release the new binding at once (review fix).
    if (!m.restored)
    {
        m.lead = ObjectGuid::Empty;
        m.members.clear();
        m.followers.clear();
        m.leadDeadSinceMs = 0;
        m.commanderGoneSinceMs = 0;
        m.boundAt = 0;
    }
    ObjectGuid const previousLead = m.lead;
    if (!m.boundAt)
        m.boundAt = now;
    if (leader && h3::CommanderGuid(leader->GetGUID()) == commander->GetGUID())
        m.lead = leader->GetGUID();
    if (m.lead.IsEmpty())
        m.lead = bound.front();
    Group* group = InWorld(m.lead) ? InWorld(m.lead)->GetGroup() : nullptr;
    m.flags = L::FlagsOf(group && group->IsMember(commander->GetGUID()), group && group->isRaidGroup(), false, false);
    m.groupId = GroupKey(group);
    m.restored = true;
    m.commanderGoneSinceMs = 0;
    m.members = bound;
    // A lead change first puts the followers' own targets back (they were pointed at the old lead), then the new lead is pointed at.
    SyncFollowers(commander->GetGUID(), m, previousLead);
    SaveRow(commander->GetGUID(), m);
    PruneToStanding(s);
}

void NoteOrder(std::vector<std::pair<Player*, std::string>> const& results, std::string const& verb, std::string const& arg)
{
    std::map<uint32, uint32> cardOfBot;
    std::vector<P::OrderOutcome> outcomes;
    for (auto const& [bot, refusal] : results)
    {
        ObjectGuid const key = CardKeyOfBot(bot->GetGUID());
        if (key.IsEmpty())
            continue;
        uint32 const botGuid = Ctr(bot->GetGUID());
        cardOfBot[botGuid] = Ctr(key);
        outcomes.push_back({ botGuid, refusal });
    }
    if (outcomes.empty())
        return;
    int64 const now = NowUnix();
    for (auto const& [card, tally] : P::TallyByCard(outcomes, cardOfBot))
    {
        bool const ok = tally.queued > 0;
        std::string text = "queued " + std::to_string(tally.queued) + " of " + std::to_string(tally.queued + tally.refused);
        if (tally.refused)
            text += ", refused " + std::to_string(tally.refused);
        NoteLog(PlayerGuid(card), P::MakeOrder(now, verb, arg, ok, tally.firstRefusal, ok ? text : std::string()));
    }
}

void NoteRunOrder(ObjectGuid leader, std::string const& order, bool accepted, std::string const& code, std::string const& text)
{
    ObjectGuid const above = h3::CommanderGuid(leader);
    std::string verb, detail;
    P::SplitRunOrder(order, verb, detail);
    NoteLog(above.IsEmpty() ? leader : above, P::MakeOrder(NowUnix(), verb, detail, accepted, code, text));
}

void AppendBindings(std::vector<pure::CT::BindingInfo>& out, std::vector<uint32> const& runLeadersShown)
{
    for (HostCard const& h : Read(NowUnix(), uint32(NowMs())))
    {
        if (std::find(runLeadersShown.begin(), runLeadersShown.end(), h.card.lead) != runLeadersShown.end())
            continue;
        // The datagram's cap, not the card collection's: `.tb commander status` shows every card and the state beside the table is pruned from all of them.
        if (out.size() >= CT::kMaxCommandersReported)
            return;
        out.push_back(P::BindingOf(h.view));
    }
}

void FillRunOrders(std::vector<pure::CT::BindingInfo>& out)
{
    std::vector<HostCard> const cards = Read(NowUnix(), uint32(NowMs()));
    for (CT::BindingInfo& binding : out)
    {
        if (!binding.run.present || !binding.orders.empty())
            continue;
        for (HostCard const& h : cards)
            if (h.view.commanderGuid == binding.commanderGuid)
            {
                binding.orders = h.view.orders;
                break;
            }
    }
}

// ---- `.tb commander status [<commander>] [#token]` ----
// commander-begin #t
// binding cmd= cguid= online= lead= group= bound= bots= system= inparty= raid= playerled= boundat= orders= [map= pos=] #t
// member cmd= bot= guid= class= role= level= hp= maxhp= power= maxpower= ptype= alive= lead= map= zone= pos= [sub=] #t       (one per bound bot in the world, the lead first)
// order cmd= at= verb= detail= ok= result= #t                                                                              (oldest first, at most 8)
// run cmd= lead= plan= state= ... #t                                                                                       (the `.tb run status` record of a card that runs)
// commander-end count= records= bindings= #t
bool HandleCommander(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    using namespace h3;
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "commander"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "commander", "missing-verb", "status");
    if (Lower(args.w[0]) != "status")
        return Fail(handler, args, "commander", "unknown-verb", args.w[0]);
    if (args.w.size() > 2)
        return Fail(handler, args, "commander", "unexpected-argument", args.w[2]);
    int64 const nowUnix = NowUnix();
    uint32 const nowMs = uint32(NowMs());
    std::vector<HostCard> cards = Read(nowUnix, nowMs);
    if (args.w.size() == 2)
    {
        std::string const wanted = Lower(args.w[1]);
        cards.erase(std::remove_if(cards.begin(), cards.end(), [&](HostCard const& h) { return Lower(h.view.commanderName) != wanted; }), cards.end());
        if (cards.empty())
            return Fail(handler, args, "commander", "no-binding", args.w[1] + " commands no bots");
    }
    Reply reply(handler, args, "commander");
    reply.Begin();
    for (HostCard const& h : cards)
    {
        P::CardView const& v = h.view;
        Rec b("binding");
        b.S("cmd", v.commanderName).N("cguid", v.commanderGuid).B("online", v.commanderOnline).S("lead", v.leadName);
        if (v.groupId)
            b.N("group", v.groupId);
        b.N("bound", h.botsBound).N("bots", v.bots.size()).B("system", h.card.system).B("inparty", h.inParty).B("raid", h.raid).B("playerled", h.playerLed);
        b.N("boundat", v.boundAt).N("orders", v.orders.size());
        if (v.commanderPlaced)
            b.N("map", v.commanderMap).Coord("pos", v.commanderX, v.commanderY, v.commanderZ);
        reply.Line(b);
        for (P::BotView const& m : v.bots)
        {
            Rec r("member");
            r.S("cmd", v.commanderName).S("bot", m.name).N("guid", m.guid).S("class", m.className).S("role", m.role).N("level", m.level);
            r.N("hp", m.hp).N("maxhp", m.maxHp).N("power", m.power).N("maxpower", m.maxPower).S("ptype", m.powerType).B("alive", m.alive).B("lead", m.lead);
            r.N("map", m.map).N("zone", m.zone).Coord("pos", m.x, m.y, m.z);
            if (m.subgroup >= 0)
                r.N("sub", m.subgroup);
            reply.Line(r);
        }
        for (CT::OrderRecord const& o : v.orders)
        {
            Rec r("order");
            r.S("cmd", v.commanderName).N("at", o.at).S("verb", o.verb).T("detail", o.detail).B("ok", o.ok).T("result", o.result);
            reply.Line(r);
        }
        if (h.run)
        {
            Rec r = run::RunRecord(*h.run, nowMs);
            r.S("cmd", v.commanderName);
            reply.Line(r);
        }
    }
    Rec end("commander");
    end.N("bindings", cards.size());
    reply.End(end);
    return true;
}
}
}

#else   // not built with the slice: `.tb commander` says so, the hooks do nothing

namespace TortoiseBots
{
namespace cmd
{
bool HandleCommander(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = "error code=engine-not-built cmd=commander msg=" + msg;
    // The echo token: the last word when it starts with '#', 2-33 characters of [A-Za-z0-9_.:-] (as TbH3.cpp Split).
    std::string_view const tailText(args);
    size_t end = tailText.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tailText[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tailText[start - 1])))
        --start;
    std::string_view const word = tailText.substr(start, end - start);
    bool good = word.size() >= 2 && word.size() <= 33 && word[0] == '#';
    for (size_t i = 1; good && i < word.size(); ++i)
        good = std::isalnum(static_cast<unsigned char>(word[i])) || word[i] == '_' || word[i] == '.' || word[i] == ':' || word[i] == '-';
    if (good)
        line += " " + std::string(word);
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}

void NoteMaster(std::vector<Player*> const&, Player*, Player*) {}
void NoteOrder(std::vector<std::pair<Player*, std::string>> const&, std::string const&, std::string const&) {}
void NoteRunOrder(ObjectGuid, std::string const&, bool, std::string const&, std::string const&) {}
void NoteGroupAdd(Group*, ObjectGuid) {}
void NoteGroupRemove(Group*, ObjectGuid) {}
void NoteGroupLeader(Group*, ObjectGuid) {}
void NoteGroupDisband(Group*) {}
void NoteDeath(Player*) {}
void NoteLogin(Player*) {}
void NoteLogout(Player*) {}
void Maintain() {}
void LoadBindings() {}
bool IsCommandedBy(Player*, Player*) { return false; }
bool IsInOwnBindingGroup(Player*, Player*) { return false; }
void AppendBindings(std::vector<pure::CT::BindingInfo>&, std::vector<uint32> const&) {}
void FillRunOrders(std::vector<pure::CT::BindingInfo>&) {}
}
}

#endif
