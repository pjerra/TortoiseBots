// Commander port, package P5 (the fight debugger): see forever/docs/CMD-P5-NOTES.md. Built into both ForeverCore options: the engine-facing
// part (the scripts, the sampler, the feeds) is behind TORTOISEBOTS_SLICE1, and without it the file is the three no-ops.
//
// The books, the fight and event payloads and the outcomes are the shared Turtle code (runtime/CombatMonitorService.cpp,
// runtime/CombatMonitorPolicy.h, runtime/BoundFightPolicy.h): this file only feeds them from ForeverCore's public API and sends what they
// build. Nothing keeps a Unit or Player pointer past the call that read it: a card's bots are guids, the fight's boss is a guid, and the
// engaged units are resolved afresh per tick.
#include "host/cmd/TbCmdCombat.h"
#include "host/cmd/TbCmdCombatPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_Unit.h"
#include "dash/TbDashLink.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRun.h"
#include "runtime/BoundFightPolicy.h"
#include "runtime/CombatMonitorService.h"

#include "ai/playerbot/DungeonPlanMgr.h"
#include "playerbot/PlayerbotAIConfig.h"

#include "Creature.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace combat
{
namespace
{
namespace CM = ::TortoiseBots::CombatMonitor;
namespace C = ::TortoiseBots::cmd::pure;                 // the card read (TbCmdBindingPure.h)
namespace P = ::TortoiseBots::cmd::combat::pure;
namespace CT = ::TortoiseBots::CommanderTelemetry;

constexpr int kProtocolVersion = 12;   // the dashboard's model version (the roster sender and the dash-runs collector write the same)

// One group's feed state between ticks: guids and plain data only.
struct Feed
{
    ObjectGuid lead;                                    // the card's lead at the last usable sample
    std::vector<uint32> members;                        // the card's bots (guid counters), the cast poll's parties
    BoundFight::LeadAway away;                          // the lead-away grace of the open fight
    ObjectGuid boss;                                    // the fight's boss unit, resolved afresh each sample
    std::map<uint32, uint32> lastCast;                  // enemy guid counter -> the spell it was casting at the last poll
    std::map<uint32, std::vector<uint32>> auras;        // member -> its applied aura ids at the last sample (the timeline diff)
    bool aurasPrimed = false;                           // the fight's baseline was taken: edges start after it
    bool inFight = false;

    void NewFight()
    {
        boss = ObjectGuid();
        lastCast.clear();
        auras.clear();
        aurasPrimed = false;
    }
};

std::map<uint32, Feed>& Feeds()
{
    static std::map<uint32, Feed> feeds;
    return feeds;
}

uint32 g_lastSampleMs = 0;
uint32 g_lastCastMs = 0;
uint32 g_lastEventMs = 0;
uint64 g_samples = 0, g_sumUs = 0, g_maxUs = 0;
uint32 g_loggedAtMs = 0;

Player* InWorld(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

ObjectGuid PlayerGuid(uint32 counter)
{
    return ObjectGuid::Create<HighGuid::Player>(counter);
}

// The group's session counter (GroupIdW2), the book's in-memory key: never GetDbStoreId, which the core reuses across groups.
uint32 GroupKey(Group const* group)
{
    return group ? uint32(group->GetGUID().GetCounter()) : 0u;
}

uint32 GuidLow(ObjectGuid const& guid)
{
    return uint32(guid.GetCounter());
}

uint64 GuidKey(ObjectGuid const& guid)
{
    return guid.GetCounter();
}

std::string SpellName(SpellInfo const* info)
{
    return info && info->SpellName ? info->SpellName->Str[LOCALE_enUS] : std::string();
}

// The entry a script changed a creature away from when its guid was spawned under another one: the guid carries the spawn row's entry and
// ForeverCore keeps one entry per spawn (SpawnData::id), so that entry is the answer (Turtle also knew the row's id1..id4 alternatives).
uint32 ScriptChangedFrom(Creature const* creature)
{
    if (!creature || !creature->GetGUID().IsCreature())
        return 0;
    uint32 const spawn = creature->GetGUID().GetEntry();
    uint32 const current = creature->GetEntry();
    return spawn && spawn != current ? spawn : 0;
}

// What an entry reads as in a fight: a plan boss of the map, a dungeon-boss or instance-bind flag, or an elite classification.
P::Rank RankOfEntry(uint32 mapId, uint32 entry)
{
    if (!entry)
        return P::Rank::None;
    bool const planBoss = ai::GetDungeonPlans().IsBossEntry(mapId, entry);
    CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry);
    if (!info)
        return planBoss ? P::Rank::Boss : P::Rank::None;
    bool const boss = planBoss || (info->flags_extra & (CREATURE_FLAG_EXTRA_DUNGEON_BOSS | CREATURE_FLAG_EXTRA_INSTANCE_BIND));
    bool const elite = info->Classification == CreatureClassifications::Elite ||
                       info->Classification == CreatureClassifications::RareElite;
    return P::RankOf(boss, elite);
}

// Both the entry it reads now and the entry its guid was spawned as: a script-changed boss is still polled and still named the boss.
P::Rank RankOfUnit(uint32 mapId, Unit const* unit)
{
    if (!unit)
        return P::Rank::None;
    return std::max(RankOfEntry(mapId, unit->GetEntry()), RankOfEntry(mapId, unit->GetGUID().GetEntry()));
}

// The applied, non-passive auras of a unit, by spell id. ForeverCore keeps one AuraApplication per applied aura (Unit.h GetAppliedAuras).
struct AuraNow
{
    uint32 id = 0;
    uint8 stacks = 0;
};

std::vector<AuraNow> AppliedAuras(Unit const* unit)
{
    std::vector<AuraNow> auras;
    for (auto const& entry : unit->GetAppliedAuras())
    {
        Aura* aura = entry.second ? entry.second->GetBase() : nullptr;
        if (!aura || aura->IsPassive())
            continue;
        auras.push_back({ aura->GetId(), aura->GetStackAmount() });
    }
    std::sort(auras.begin(), auras.end(), [](AuraNow const& a, AuraNow const& b) { return a.id < b.id; });
    return auras;
}

// The units the party fights: every member's attackers and every unit it is in combat with (the core's own combat references: a boss the
// party only attacks is in them too). Bosses first, then elites, then the rest, and a guid is looked at once.
void CollectEngaged(Feed const& feed, Player* lead, std::vector<Unit*>& out, size_t cap, bool watchedOnly)
{
    struct Ref
    {
        ObjectGuid guid;
        P::Rank rank = P::Rank::None;
    };
    std::set<ObjectGuid> seen;
    std::vector<Ref> refs;
    auto consider = [&](ObjectGuid const& guid)
    {
        if (guid.IsEmpty() || !seen.insert(guid).second)
            return;
        refs.push_back({ guid, P::Rank::None });
    };
    for (uint32 member : feed.members)
    {
        Player* bot = InWorld(PlayerGuid(member));
        if (!bot || !bot->GetMap())
            continue;
        for (Unit* attacker : bot->getAttackers())
            if (attacker)
                consider(attacker->GetGUID());
        for (auto const& ref : bot->GetCombatManager().GetPvECombatRefs())
            consider(ref.first);
    }
    uint32 const mapId = lead->GetMapId();
    for (Ref& ref : refs)
        ref.rank = RankOfEntry(mapId, ref.guid.GetEntry());
    std::stable_sort(refs.begin(), refs.end(), [](Ref const& a, Ref const& b) { return uint8(a.rank) > uint8(b.rank); });
    out.clear();
    for (Ref const& ref : refs)
    {
        if (out.size() >= cap)
            break;
        if (watchedOnly && !P::CastWatched(ref.rank))
            break;   // sorted: nothing the poll reads is left
        if (Unit* unit = ObjectAccessor::GetUnit(*lead, ref.guid))
            out.push_back(unit);
    }
}

// The timeline's aura edges: ForeverCore's UnitScript has no apply/remove hook, so each member's watched-by-id aura set is sampled once a
// second and the service (which owns the watched list) is told only what changed. The fight's first sample is the baseline: auras already
// up when it started are not edges.
void SampleAuras(Feed& feed, uint32 nowMs)
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    if (!service.TimelineEnabled())
    {
        feed.auras.clear();
        feed.aurasPrimed = false;
        return;
    }
    for (uint32 member : feed.members)
    {
        Player* bot = InWorld(PlayerGuid(member));
        if (!bot)
            continue;
        std::vector<uint32> now;
        for (AuraNow const& aura : AppliedAuras(bot))
            now.push_back(aura.id);
        P::SortUnique(now);
        auto const had = feed.auras.find(member);
        if (feed.aurasPrimed)
        {
            std::vector<uint32> const before = had == feed.auras.end() ? std::vector<uint32>() : had->second;
            for (auto const& edge : P::AuraEdges(before, now))
                service.OnTimelineAura(member, edge.first, edge.second, nowMs);
        }
        feed.auras[member] = std::move(now);
    }
    feed.aurasPrimed = true;
}

// The run's context at the fight's start: the plan, the leg, the step and, on a boss step, the boss entry and the units it plans.
CM::FightContext ContextOf(Player* lead)
{
    CM::FightContext ctx;
    ctx.kind = "commander";
    run::RunRec* rec = run::Find(lead->GetGUID());
    if (!rec || !rec->run.plan)
        return ctx;
    DungeonRun::StatusFields const fields = DungeonRun::StatusParts(rec->run);
    ctx.plan = fields.plan;
    ctx.leg = fields.leg;
    ctx.step = uint32(fields.step);
    if (!fields.kind.empty())
        ctx.kind = fields.kind;
    if (ai::PlanStep const* step = DungeonRun::FactStep(rec->run))
    {
        if (step->kind == ai::PlanStepKind::Boss)
        {
            ctx.bossEntry = step->entry;
            if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(step->entry))
                ctx.bossName = info->Name;
        }
        ctx.planned = step->entries;
    }
    return ctx;
}

// The fight's boss: the run's plan boss when he is among the units being fought (a move-step pull and the walk back), else the boss-flagged
// engaged creature with the most maximum health. The first pick of a fight wins (the service keeps it), and the unit's guid is remembered
// here to sample his health and victim; a script-changed entry is a phase the next sample reports.
void PickBoss(Feed& feed, uint32 group, Player* lead, std::vector<Unit*> const& engaged, CM::FightContext const& ctx)
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    if (ctx.bossEntry)
        for (Unit* unit : engaged)
        {
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!creature)
                continue;
            if (creature->GetEntry() != ctx.bossEntry && ScriptChangedFrom(creature) != ctx.bossEntry)
                continue;
            service.AdoptBoss(group, ctx.bossEntry, ctx.bossName.c_str());
            feed.boss = creature->GetGUID();
            service.NoteBossGuid(group, GuidKey(feed.boss));
            return;
        }
    uint32 const mapId = lead->GetMapId();
    std::vector<BoundFight::BossCandidate> candidates;
    std::vector<ObjectGuid> guids;
    for (Unit* unit : engaged)
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        if (!creature || RankOfUnit(mapId, creature) != P::Rank::Boss)
            continue;
        BoundFight::BossCandidate candidate;
        candidate.entry = creature->GetEntry();
        candidate.guid = GuidKey(creature->GetGUID());
        candidate.name = creature->GetName();
        candidate.maxHealth = uint32(creature->GetMaxHealth());
        candidate.boss = true;
        candidates.push_back(std::move(candidate));
        guids.push_back(creature->GetGUID());
    }
    int const picked = BoundFight::PickBoss(candidates);
    if (picked < 0)
        return;
    service.NoteBoss(group, candidates[size_t(picked)].entry, candidates[size_t(picked)].name.c_str(),
                     candidates[size_t(picked)].guid);
    feed.boss = guids[size_t(picked)];
}

// The boss's state for the fight's outcome (an evade ends it with him alive and home) and, with the timeline on, his health and victim.
void SampleBoss(Feed& feed, uint32 group, Player* lead, uint32 nowMs)
{
    if (feed.boss.IsEmpty())
        return;
    Unit* boss = ObjectAccessor::GetUnit(*lead, feed.boss);
    if (!boss)
        return;
    CombatMonitorService& service = CombatMonitorService::Instance();
    Creature* creature = boss->ToCreature();
    bool atHome = false;
    if (creature)
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        GetRespawnCoordCompat(creature, x, y, z);
        atHome = boss->GetDistance2d(x, y) <= CM::kBossHomeYd;
    }
    service.NoteBossState(group, boss->IsAlive(), boss->IsInCombat(), creature && creature->IsInEvadeMode(),
                          boss->IsAlive() && boss->GetHealth() >= boss->GetMaxHealth(), atHome);
    if (boss->GetEntry() != service.BossEntry(group))
        service.NoteBossForm(group, boss->GetEntry());
    if (!service.TimelineEnabled())
        return;
    Unit* victim = boss->GetVictim();
    service.SampleTimeline(group, boss->IsAlive() ? uint32(boss->GetHealth()) : 0, uint32(boss->GetMaxHealth()),
                           victim ? GuidLow(victim->GetGUID()) : 0, victim ? victim->GetName() : "", nowMs);
}

// One sample of a standing card: the members and their combat state, the focus's tracked debuffs, the run's context, and then the boss and
// his timeline. A card whose lead left its group feeds nothing; past the grace the open fight closes as unfinished.
void SampleCard(C::CardView const& card, uint32 nowMs)
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    Feed& feed = Feeds()[card.groupId];
    uint32 const leadGuid = card.bots.empty() ? 0 : card.bots.front().guid;
    Player* lead = leadGuid ? InWorld(PlayerGuid(leadGuid)) : nullptr;
    Group* group = lead ? lead->GetGroup() : nullptr;
    bool const usable = BoundFight::LeadUsable(lead != nullptr, lead && lead->IsInWorld(), GroupKey(group), card.groupId);
    if (feed.away.Overdue(usable, nowMs))
    {
        service.CloseUnfinished(card.groupId, nowMs);
        feed.NewFight();
        feed.inFight = false;
    }
    if (!usable)
    {
        feed.lead = ObjectGuid();
        return;
    }
    feed.lead = lead->GetGUID();
    feed.members.clear();
    std::vector<CM::MemberInfo> members;
    members.reserve(card.bots.size());
    bool anyInCombat = false;
    size_t alive = 0;
    for (C::BotView const& bot : card.bots)
    {
        CM::MemberInfo member;
        member.guid = bot.guid;
        member.name = bot.name;
        member.cls = bot.className;
        member.role = P::RoleOf(bot.role);
        member.subgroup = bot.subgroup;
        members.push_back(std::move(member));
        feed.members.push_back(bot.guid);
        if (Player* player = InWorld(PlayerGuid(bot.guid)))
        {
            anyInCombat = anyInCombat || player->IsInCombat();
            if (player->IsAlive())
                ++alive;
        }
    }
    CM::FightContext const ctx = ContextOf(lead);
    std::vector<Unit*> engaged;
    CollectEngaged(feed, lead, engaged, P::kWatchCap, false);
    // The tracked debuffs of one focus unit: the run's boss among the engaged when he is there, else the one with the most health.
    std::vector<std::string> debuffKeys;
    std::vector<P::FocusCandidate> focusCandidates;
    focusCandidates.reserve(engaged.size());
    for (Unit* unit : engaged)
        focusCandidates.push_back({ unit->GetEntry(), uint32(unit->GetMaxHealth()) });
    int const focus = P::PickFocus(focusCandidates, ctx.bossEntry);
    if (focus >= 0)
        for (AuraNow const& aura : AppliedAuras(engaged[size_t(focus)]))
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(aura.id, DIFFICULTY_NONE);
            if (!info || !info->SpellName)
                continue;
            for (std::string const& key : CM::DebuffKeys(info->SpellName->Str[LOCALE_enUS], aura.stacks))
                debuffKeys.push_back(key);
        }
    bool const wasInFight = feed.inFight;
    service.NoteBinding(card.groupId, card.commanderName, card.commanderGuid, lead->GetMapId());
    service.Refresh(card.groupId, std::move(members), anyInCombat, P::AllDead(card.bots.size(), alive), ctx, debuffKeys,
                    nowMs);
    feed.inFight = service.InFight(card.groupId);
    if (!feed.inFight)
    {
        if (wasInFight)
            feed.NewFight();
        return;
    }
    if (!wasInFight)
        feed.NewFight();
    SampleAuras(feed, nowMs);
    if (service.BossEntry(card.groupId) == 0)
        PickBoss(feed, card.groupId, lead, engaged, ctx);
    SampleBoss(feed, card.groupId, lead, nowMs);
}

// The 1 Hz pass: every standing card, then the books of the cards that are gone (a released binding, a dismissed party).
void Sample()
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    std::chrono::steady_clock::time_point const started = std::chrono::steady_clock::now();
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::vector<C::CardView> const cards = cmd::StandingCards();
    std::set<uint32> standing;
    for (C::CardView const& card : cards)
    {
        if (!P::Feedable(card.groupId))
            continue;
        standing.insert(card.groupId);
        SampleCard(card, nowMs);
    }
    for (auto it = Feeds().begin(); it != Feeds().end();)
    {
        if (standing.count(it->first))
        {
            ++it;
            continue;
        }
        service.Forget(it->first);
        it = Feeds().erase(it);
    }
    int64 const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    ++g_samples;
    g_sumUs += uint64(us);
    g_maxUs = std::max(g_maxUs, uint64(us));
    if (!g_loggedAtMs)
        g_loggedAtMs = nowMs;
    if (nowMs - g_loggedAtMs >= 60000)
    {
        if (!Feeds().empty())
            TC_LOG_INFO("tortoisebots", "bound fights 60s: {} samples, avg {} us max {} us, {} group(s)",
                        g_samples, g_samples ? (g_sumUs / g_samples) : 0, g_maxUs, Feeds().size());
        g_loggedAtMs = nowMs;
        g_samples = g_sumUs = g_maxUs = 0;
    }
}

// The elite and boss cast starts of the groups in a fight, once per kCastPollMs: at most kWatchCap units per group, ranked from the guid's
// entry (no unit resolved for what is not read), and one event per cast that started (the last spell read is remembered per unit).
void PollEnemyCasts()
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    if (!service.AnyInFight())
        return;
    for (auto& entry : Feeds())
    {
        uint32 const group = entry.first;
        Feed& feed = entry.second;
        if (!service.InFight(group))
            continue;
        Player* lead = InWorld(feed.lead);
        if (!lead || !lead->IsInWorld())
            continue;
        std::vector<Unit*> watch;
        CollectEngaged(feed, lead, watch, P::kWatchCap, true);
        uint32 const mapId = lead->GetMapId();
        std::set<uint32> current;
        for (Unit* unit : watch)
        {
            uint32 const key = GuidLow(unit->GetGUID());
            current.insert(key);
            Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell)
                spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            SpellInfo const* info = spell && !spell->IsTriggered() ? spell->GetSpellInfo() : nullptr;
            uint32 const spellId = info ? info->Id : 0;
            auto const last = feed.lastCast.find(key);
            if (!spellId || (last != feed.lastCast.end() && last->second == spellId))
            {
                feed.lastCast[key] = spellId;
                continue;
            }
            feed.lastCast[key] = spellId;
            Unit* target = spell->m_targets.GetUnitTarget();
            service.NoteEnemyCast(group, unit->GetEntry(), GuidKey(unit->GetGUID()), unit->GetName(), SpellName(info).c_str(),
                                  P::CastRank(RankOfUnit(mapId, unit)), spellId, target ? GuidKey(target->GetGUID()) : 0,
                                  target ? target->GetName() : "");
        }
        for (auto it = feed.lastCast.begin(); it != feed.lastCast.end();)
            it = current.count(it->first) ? std::next(it) : feed.lastCast.erase(it);
    }
}

// The finished fights (COMBAT_FIGHT) of every book, one datagram each, through the dashboard link the dash-runs collector uses.
void DrainFights()
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    std::vector<std::string> payloads;
    service.TakePayloads(payloads, kProtocolVersion, DashRuns::link::Session(),
                         [] { return DashRuns::link::NextSeq(); }, int64(std::time(nullptr)));
    for (std::string const& payload : payloads)
        DashRuns::link::Send(payload);
}

// The live stream (COMBAT_EVENTS): what the books queued and a keepalive while a fight is open, at most one datagram per group per
// kEventGapMs. The payload's own budget may leave events queued: they go out on the next drain, in order.
void DrainEvents(uint32 nowMs)
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    std::vector<std::string> payloads;
    std::chrono::system_clock::time_point const wall = std::chrono::system_clock::now();
    int64 const tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count();
    service.TakeEventPayloads(payloads, kProtocolVersion, DashRuns::link::Session(),
                              [] { return DashRuns::link::NextSeq(); }, int64(std::time(nullptr)), tsMs, nowMs,
                              P::kEventGapMs);
    for (std::string const& payload : payloads)
        DashRuns::link::Send(payload);
}

// The core's hooks, one of each the fight records need: damage and healing dealt, and a death. There is no enter-combat or aura hook on this
// core; the sample's in-combat state starts a fight and its aura diff is the timeline's signal.
class CombatUnitScript final : public UnitScript
{
public:
    CombatUnitScript() : UnitScript("tortoisebots_combat_monitor") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        CombatMonitorService& service = CombatMonitorService::Instance();
        if (!service.Enabled() || !attacker || !victim || attacker == victim)
            return;
        Player* attackerOwner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        Player* victimOwner = victim->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!attackerOwner && !victimOwner)
            return;
        uint32 enemyEntry = 0;
        uint64 enemyGuid = 0;
        char const* enemyName = "";
        if (attackerOwner && !victimOwner)
        {
            enemyEntry = victim->GetEntry();
            enemyGuid = GuidKey(victim->GetGUID());
            enemyName = victim->GetName().c_str();
            service.OnDamage(GuidLow(attackerOwner->GetGUID()), 0, damage, attacker->GetName().c_str(), enemyEntry,
                             enemyGuid, enemyName);
            return;
        }
        if (!attackerOwner && victimOwner)
        {
            enemyEntry = attacker->GetEntry();
            enemyGuid = GuidKey(attacker->GetGUID());
            enemyName = attacker->GetName().c_str();
            service.OnDamage(0, GuidLow(victimOwner->GetGUID()), damage, attacker->GetName().c_str(), enemyEntry,
                             enemyGuid, enemyName);
            return;
        }
        // Member on member (a charm, Mind Control): not the fight's numbers on either side.
    }

    void OnHeal(Unit* healer, Unit* /*receiver*/, uint32& /*gain*/) override
    {
        // The outcome hook below carries the raw heal and the part that landed; this one only exists so the service's numbers come from one
        // path (OnHealOutcome) and never twice.
    }

    void OnHealOutcome(Unit* healer, Unit* /*target*/, uint32 /*spellId*/, uint32 raw, uint32 effective) override
    {
        CombatMonitorService& service = CombatMonitorService::Instance();
        if (!service.Enabled() || !healer || !raw)
            return;
        Player* owner = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!owner)
            return;
        service.OnHeal(GuidLow(owner->GetGUID()), effective, raw > effective ? raw - effective : 0);
    }

    void OnUnitDeath(Unit* victim, Unit* killer) override
    {
        CombatMonitorService& service = CombatMonitorService::Instance();
        if (!service.Enabled() || !victim)
            return;
        Player* killerOwner = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        char const* killerName = killer ? killer->GetName().c_str() : "";
        if (Player* player = victim->ToPlayer())
        {
            service.OnDeath(GuidLow(player->GetGUID()), killerOwner ? GuidLow(killerOwner->GetGUID()) : 0, killerName,
                            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), 0, 0);
            return;
        }
        if (Creature* creature = victim->ToCreature())
            service.OnDeath(0, killerOwner ? GuidLow(killerOwner->GetGUID()) : 0, killerName, 0.0f, 0.0f, 0.0f,
                            creature->GetEntry(), ScriptChangedFrom(creature));
    }
};

// A bot's cast: heals, taunts and interrupts are the events the fight log replays, and the heal flag splits heal casts from the rest.
class CombatCastScript final : public PlayerScript
{
public:
    CombatCastScript() : PlayerScript("tortoisebots_combat_monitor_casts") { }

    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        CombatMonitorService& service = CombatMonitorService::Instance();
        if (!service.Enabled() || !player || !spell || spell->IsTriggered())
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        bool const heal = info->HasEffect(SPELL_EFFECT_HEAL) || info->HasAura(SPELL_AURA_PERIODIC_HEAL);
        bool const taunt = info->HasEffect(SPELL_EFFECT_ATTACK_ME) || info->HasAura(SPELL_AURA_MOD_TAUNT);
        bool const interrupt = info->HasEffect(SPELL_EFFECT_INTERRUPT_CAST);
        char const kind = taunt ? 't' : (interrupt ? 'i' : 0);
        uint64 target = 0;
        char const* targetName = "";
        if (kind)
        {
            Unit* unitTarget = spell->m_targets.GetUnitTarget();
            if (unitTarget)
            {
                target = GuidKey(unitTarget->GetGUID());
                targetName = unitTarget->GetName().c_str();
            }
        }
        service.OnCast(GuidLow(player->GetGUID()), SpellName(info).c_str(), heal, kind, target, targetName);
    }
};
}

void Update(uint32 /*diff*/)
{
    CombatMonitorService& service = CombatMonitorService::Instance();
    // The Turtle gate: the config key and the emitter. On this core the emitter is Sagiroth's sender, reached through the dash-runs link
    // (decision D6); with it off there is nowhere to send, so the monitor is off too.
    bool const on = sPlayerbotAIConfig.combatMonitor && DashRuns::link::Active();
    service.SyncConfig(on);
    if (!on)
    {
        if (!Feeds().empty())
            Feeds().clear();
        return;
    }
    if (!BotMgr::Instance().Enabled())
        return;
    uint32 const nowMs = GameTime::GetGameTimeMS();
    if (P::Due(nowMs, g_lastCastMs, P::kCastPollMs))
    {
        g_lastCastMs = nowMs;
        PollEnemyCasts();
    }
    if (P::Due(nowMs, g_lastSampleMs, P::kSampleMs))
    {
        g_lastSampleMs = nowMs;
        Sample();
        DrainFights();
    }
    if (P::Due(nowMs, g_lastEventMs, P::kEventsMs))
    {
        g_lastEventMs = nowMs;
        DrainEvents(nowMs);
    }
}

void FillWindow(CommanderTelemetry::BindingInfo& binding)
{
    if (!CombatMonitorService::Instance().Enabled() || !binding.groupId)
        return;
    binding.combatJson = CombatMonitorService::Instance().WindowJson(binding.groupId);
}

void RegisterScripts()
{
    new CombatUnitScript();
    new CombatCastScript();
}

} // namespace combat
} // namespace cmd
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace cmd
{
namespace combat
{
void Update(uint32) {}
void FillWindow(CommanderTelemetry::BindingInfo&) {}
void RegisterScripts() {}
}
}
}

#endif
