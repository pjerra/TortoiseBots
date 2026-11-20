// Commander port, package P4: the map layers and the live position report (see TbCmdFeeds.h and forever/docs/CMD-P4-NOTES.md).
//
// The collectors are the Turtle ones (T commands/Commander.cpp:15653-16636, :17052-17123) rewritten against what ForeverCore holds: the hostiles near the
// lead come from one grid sweep (the Turtle build read the bots' perception cache), the objects from one world-object sweep, the bosses from a spawn
// cache built once over the creature spawn data and read by spawn id. Everything is a field read on units the core already has; nothing here scans the
// world or the database per tick, and the two clocks (the collector's 2 s cycle and the position clock) are the only entry points.
#include "host/cmd/TbCmdFeeds.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "compat/botpch_forever.h"
#include "compat/TbCompat_Unit.h"
#include "dash/TbDashLink.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRun.h"

#include "playerbot/PlayerbotAI.h"

#include "Config.h"
#include "Creature.h"
#include "CreatureData.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Util.h"

#include "runtime/BossWindowSignal.h"
#include "runtime/ReadyCheckPolicy.h"
#include "runtime/SpellShape.h"

#include "ai/playerbot/DungeonPlanMgr.h"
#include "ai/playerbot/DungeonPlanPolicy.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <list>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace
{
namespace CT = ::TortoiseBots::CommanderTelemetry;
namespace FD = ::TortoiseBots::cmd::feeds;
namespace RC = ::TortoiseBots::ReadyCheck;
namespace BW = ::TortoiseBots::BossWindow;
namespace SS = ::TortoiseBots::SpellShape;
namespace DashLink = ::TortoiseBots::DashRuns::link;

constexpr uint32 kCostLogMs = 60000;

// The 64-bit id the wire carries for a unit or object (the Turtle raw guid's place): the low word of ForeverCore's 128-bit guid. It is opaque to the
// dashboard; the module resolves it back with ObjectGuid's own bytes only while the guid is in its hands.
uint64 RawOf(ObjectGuid guid)
{
    return guid.GetRawValue(0);
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

std::string SpellNameOf(SpellInfo const* info)
{
    if (!info)
        return std::string();
    char const* name = (*info->SpellName)[sWorld.GetDefaultDbcLocale()];
    return name ? std::string(name) : std::string();
}

char DispelLetter(uint32 dispel)
{
    switch (dispel)
    {
        case DISPEL_MAGIC: return 'm';
        case DISPEL_CURSE: return 'c';
        case DISPEL_DISEASE: return 'd';
        case DISPEL_POISON: return 'p';
        default: return 0;
    }
}

// One aura as the telemetry selectors read it, from an applied aura (ForeverCore keeps one application per aura; the Turtle holder is its base).
CT::AuraRead AuraReadOf(AuraApplication const* app)
{
    CT::AuraRead read;
    if (!app || !app->GetBase())
        return read;
    Aura const* aura = app->GetBase();
    SpellInfo const* proto = aura->GetSpellInfo();
    if (!proto)
        return read;
    read.aura.id = proto->Id;
    read.aura.name = SpellNameOf(proto);
    read.aura.stacks = aura->GetStackAmount();
    int32 const left = aura->GetDuration();
    read.aura.leftMs = left > 0 ? uint32(left) : 0;
    read.aura.dispelType = DispelLetter(proto->Dispel);
    read.passive = aura->IsPassive();
    read.negative = !app->IsPositive();
    read.aura.spreading = read.negative && read.aura.dispelType != 0 && PlayerbotAI::IsSpreadingAura(GetSpellEntryCompat(proto));
    return read;
}

std::vector<CT::AuraRead> ReadsOf(Unit const* unit)
{
    std::vector<CT::AuraRead> reads;
    for (auto const& entry : unit->GetAppliedAuras())
        if (AuraApplication const* app = entry.second)
            reads.push_back(AuraReadOf(app));
    return reads;
}

// The facing and the cast a boss or elite is running (the dashboard's overlay row). A breath cast triggered never shows as the current spell, so a
// boss in a breath window reports the breath from the committed rows (runtime/BossWindowSignal.h) the way the Turtle collector did. Read from what
// the unit already holds: no scan.
struct BreathClock
{
    BW::Countdowns windows;
};

BreathClock& Breath()
{
    static BreathClock clock;
    return clock;
}

void FillBreathWindow(Unit* unit, BW::Key const& key, CT::MobInfo& mob)
{
    BW::Facts facts;
    facts.map = unit->GetMapId();
    facts.entry = unit->GetEntry();
    facts.x = unit->GetPositionX();
    facts.y = unit->GetPositionY();
    facts.stopped = unit->IsStopped();
    facts.hasTarget = !unit->GetTarget().IsEmpty();
    BW::BreathPoint const* point = BW::OpenPoint(facts, [unit](uint32 aura) { return unit->HasAura(aura); });
    if (!point)
    {
        Breath().windows.Forget(key);
        return;
    }
    uint32 leftMs = 0;
    if (!Breath().windows.Left(key, WorldTimer::getMSTime(), point->windowMs, leftMs))
        return;
    mob.cast = SpellNameOf(sSpellMgr.GetSpellInfo(point->spell, DIFFICULTY_NONE));
    mob.castId = point->spell;
    mob.castLeftMs = leftMs;
    mob.castTotalMs = point->windowMs;
    mob.channel = false;
    mob.castTarget = 0;
    if (SS::Shape const* shape = SS::For(mob.castId))
    {
        mob.hasShape = true;
        mob.shape = *shape;
    }
}

void FillMobFacingAndCast(Unit* unit, CT::MobInfo& mob)
{
    mob.hasO = true;
    mob.o = unit->GetOrientation();
    BW::Key const windowKey{ unit->GetMapId(), unit->GetInstanceId(), RawOf(unit->GetGUID()) };
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* spell = unit->GetCurrentSpell(type);
        if (!spell || !spell->GetSpellInfo())
            continue;
        Breath().windows.Forget(windowKey);
        mob.castId = spell->GetSpellInfo()->Id;
        mob.castLeftMs = uint32(std::max(0, spell->GetRemainingCastTime()));
        mob.channel = type == CURRENT_CHANNELED_SPELL;
        mob.castTotalMs = mob.channel ? uint32(std::max(0, spell->GetSpellInfo()->GetDuration())) : uint32(std::max(0, spell->GetCastTime()));
        mob.castTarget = RawOf(spell->m_targets.GetUnitTargetGUID());
        if (SS::Shape const* shape = SS::For(mob.castId))
        {
            mob.hasShape = true;
            mob.shape = *shape;
        }
        return;
    }
    FillBreathWindow(unit, windowKey, mob);
}

char GoKindLetter(GameobjectTypes type)
{
    switch (type)
    {
        case GAMEOBJECT_TYPE_DOOR: return FD::ObjectTypeLetter(FD::GoKind::Door);
        case GAMEOBJECT_TYPE_CHEST: return FD::ObjectTypeLetter(FD::GoKind::Chest);
        case GAMEOBJECT_TYPE_BUTTON: return FD::ObjectTypeLetter(FD::GoKind::Button);
        case GAMEOBJECT_TYPE_GOOBER: return FD::ObjectTypeLetter(FD::GoKind::Goober);
        case GAMEOBJECT_TYPE_RITUAL: return FD::ObjectTypeLetter(FD::GoKind::Ritual);
        default: return 0;   // a kind the dashboard does not draw: not listed
    }
}

// ---- the boss spawn cache: one pass over the core's creature spawn data (the Turtle build made the same pass at startup) ----
struct BossCache
{
    bool built = false;
    uint64 generation = 0;    // the plan table generation it was built from (a reload rebuilds it once)
    uint32 buildMs = 0;
    std::unordered_map<uint32, std::vector<CT::BossEntrySpawns>> byMap;
};

BossCache& Bosses()
{
    static BossCache cache;
    return cache;
}

CT::BossKillLatch& BossKills()
{
    static CT::BossKillLatch kills;
    return kills;
}

void BuildBossCache()
{
    BossCache& cache = Bosses();
    ai::DungeonPlanTable const& plans = ai::GetDungeonPlans();
    if (!plans.Sealed())
        return;
    auto const started = std::chrono::steady_clock::now();
    cache.byMap.clear();
    std::unordered_map<uint32, std::vector<ai::PlanStep const*>> planSteps;
    std::unordered_map<uint64, size_t> planOrder;   // (map, entry) -> position among its map's plan boss steps
    for (ai::DungeonPlan const* plan : plans.All())
        for (ai::PlanStep const& step : plan->steps)
            if (step.kind == ai::PlanStepKind::Boss && step.entry)
            {
                uint64 const key = (uint64(plan->map) << 32) | step.entry;
                if (planOrder.emplace(key, planSteps[plan->map].size()).second)
                    planSteps[plan->map].push_back(&step);
            }
    std::unordered_map<uint32, std::vector<CT::BossSpawnRow>> rowsByMap;
    std::unordered_set<uint64> spawned;
    for (auto const& [spawnId, data] : sObjectMgr.GetAllCreatureData())
    {
        uint32 const entry = data.id;
        if (!entry)
            continue;
        CreatureTemplate const* info = sObjectMgr.GetCreatureTemplate(entry);
        uint64 const key = (uint64(data.mapId) << 32) | entry;
        bool const boss = plans.IsBossEntry(data.mapId, entry) || (info && (info->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND));
        if (!boss)
            continue;
        CT::BossSpawnRow row;
        row.dbGuid = uint32(spawnId);
        row.info.entry = entry;
        row.info.name = info ? info->Name : std::string();
        row.info.x = data.spawnPoint.GetPositionX();
        row.info.y = data.spawnPoint.GetPositionY();
        row.info.inPlan = planOrder.count(key) != 0;
        rowsByMap[data.mapId].push_back(std::move(row));
        spawned.insert(key);
    }
    // A plan boss with no spawn row (summoned or scripted) still belongs to the roster, at the step's place.
    for (auto& [map, steps] : planSteps)
    {
        std::vector<CT::BossSpawnRow>& rows = rowsByMap[map];
        for (ai::PlanStep const* step : steps)
        {
            if (spawned.count((uint64(map) << 32) | step->entry))
                continue;
            CreatureTemplate const* info = sObjectMgr.GetCreatureTemplate(step->entry);
            CT::BossSpawnRow row;
            row.info.entry = step->entry;
            row.info.name = info ? info->Name : std::string();
            row.info.x = step->x;
            row.info.y = step->y;
            row.info.inPlan = true;
            rows.push_back(std::move(row));
        }
    }
    uint32 overCap = 0;
    for (auto& [map, rows] : rowsByMap)
    {
        uint32 const m = map;
        auto const rank = [&](CT::BossSpawnRow const& r)
        {
            auto const it = planOrder.find((uint64(m) << 32) | r.info.entry);
            return it == planOrder.end() ? planOrder.size() + 1 : it->second;
        };
        std::stable_sort(rows.begin(), rows.end(), [&](CT::BossSpawnRow const& a, CT::BossSpawnRow const& b)
        {
            size_t const ra = rank(a), rb = rank(b);
            return ra != rb ? ra < rb : a.dbGuid < b.dbGuid;
        });
        std::vector<CT::BossEntrySpawns>& roster = cache.byMap[m];
        roster = CT::GroupBossSpawns(rows);
        if (roster.size() > CT::kMaxBossesPerBinding)
        {
            ++overCap;
            sLog.outString("TortoiseBots: boss roster of map %u has %u bosses, over the %u a binding may send: its list is never sent",
                m, uint32(roster.size()), uint32(CT::kMaxBossesPerBinding));
        }
    }
    cache.built = true;
    cache.generation = ai::DungeonPlanTableGeneration().load(std::memory_order_acquire);
    cache.buildMs = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    sLog.outString("TortoiseBots: commander boss spawn cache built in %u ms for %u maps%s", cache.buildMs, uint32(cache.byMap.size()),
        overCap ? " (some rosters over the per-binding cap)" : "");
}

std::vector<CT::BossEntrySpawns> const* BossRoster(uint32 mapId)
{
    BossCache const& cache = Bosses();
    if (!cache.built)
        return nullptr;
    auto const it = cache.byMap.find(mapId);
    return it == cache.byMap.end() ? nullptr : &it->second;
}

// ---- the collector's own state ----
struct WatchedBinding
{
    std::string commander;         // the name the COMMANDERS datagram of the last report carried (the position report files it the same way)
    uint32 leadGuid = 0;
    std::vector<uint32> bots;      // guid counters, the lead first
    std::vector<ObjectGuid> mobs;  // the units of the last full report (guids, never pointers), resolved by the position clock
    bool combat = false;
};

struct State
{
    bool enabled = true;
    float mobRange = FD::kDefaultMobRange;
    float objectRange = FD::kDefaultObjectRange;
    uint32 posMs = FD::kDefaultPosMs;
    uint32 sincePosMs = 0;
    size_t posStart = 0;
    std::unordered_map<uint32, WatchedBinding> watched;   // by commander guid counter
    struct Ready
    {
        uint32 atMs = 0;
        uint32 keySpellsAtMs = 0;
        std::vector<uint32> keySpells;
        uint32 buffs = 0;
        uint32 cons = 0;
        uint8 cdReady = 0;
        uint8 cdKnown = 0;
        uint32 cdLongestS = 0;
    };
    std::unordered_map<uint64, Ready> ready;
    uint64 fillCycles = 0, fillSumUs = 0, posCycles = 0, posSumUs = 0;
    uint32 fillMaxUs = 0, posMaxUs = 0;
    uint32 logMs = 0;
};

State& Get()
{
    static State state;
    return state;
}

void ApplyReady(State::Ready const& entry, CT::BotExtra& extra)
{
    extra.hasReady = true;
    extra.readyBuffs = entry.buffs;
    extra.readyCons = entry.cons;
    extra.cdReady = entry.cdReady;
    extra.cdKnown = entry.cdKnown;
    extra.cdLongestS = entry.cdLongestS;
}

void ReadReadyAuras(Player* bot, State::Ready& entry)
{
    std::vector<RC::AuraFact> facts;
    for (auto const& applied : bot->GetAppliedAuras())
    {
        AuraApplication const* app = applied.second;
        Aura const* holder = app ? app->GetBase() : nullptr;
        SpellInfo const* proto = holder ? holder->GetSpellInfo() : nullptr;
        if (!holder || !proto || holder->IsPassive())
            continue;
        bool const positive = app->IsPositive();
        bool const fromPlayer = holder->GetCasterGUID().IsPlayer();
        for (SpellEffectInfo const& effect : proto->GetEffects())
        {
            if (effect.ApplyAuraName == SPELL_AURA_NONE)
                continue;
            RC::AuraFact fact;
            fact.auraType = uint32(effect.ApplyAuraName);
            fact.misc = effect.MiscValue;
            fact.family = proto->SpellFamilyName;
            // The elixir mask (flask, elixir, well fed) is a Turtle SpellMgr table; ForeverCore has none, so a consumable is only recognised by its
            // potion family (RC::ConsumableBits) and the ready row under-reports consumables rather than inventing them.
            fact.elixirMask = 0;
            fact.positive = positive;
            fact.fromPlayer = fromPlayer;
            facts.push_back(fact);
        }
    }
    entry.buffs = RC::BuffBits(facts);
    entry.cons = RC::ConsumableBits(facts);
}

void ReadReadyKeySpells(Player* bot, State::Ready& entry)
{
    std::vector<RC::SpellCd> spells;
    for (auto const& known : bot->GetSpellMap())
    {
        if (known.second.state == PLAYERSPELL_REMOVED || known.second.disabled)
            continue;
        SpellInfo const* proto = sSpellMgr.GetSpellInfo(known.first, DIFFICULTY_NONE);
        if (!proto)
            continue;
        RC::SpellCd cd;
        cd.spell = known.first;
        cd.chainFirst = sSpellMgr.GetFirstSpellInChain(known.first);
        cd.recoveryMs = proto->RecoveryTime;
        cd.categoryRecoveryMs = proto->CategoryRecoveryTime;
        cd.passive = proto->IsPassive();
        cd.family = proto->SpellFamilyName;
        spells.push_back(cd);
    }
    entry.keySpells = RC::KeySpells(spells);
}

void ReadReadyCooldowns(Player* bot, State::Ready& entry)
{
    uint8 ready = 0;
    uint8 known = 0;
    uint32 longest = 0;
    for (uint32 spell : entry.keySpells)
    {
        ++known;
        SpellInfo const* proto = sSpellMgr.GetSpellInfo(spell, DIFFICULTY_NONE);
        if (!proto)
            continue;
        if (bot->GetSpellHistory()->HasCooldown(proto))
            longest = std::max(longest, uint32(std::max<int64>(0, bot->GetSpellHistory()->GetRemainingCooldown(proto).count())));
        else
            ++ready;
    }
    entry.cdReady = ready;
    entry.cdKnown = known;
    entry.cdLongestS = longest;
}

// The ready check's facts, read out of combat: the auras and the cooldown state every RC::kReadyRefreshMs, which spells are key cooldowns every
// RC::kKeySpellsRefreshMs. In combat the cached values ride as they are (a fight costs no aura or spell read).
void ReadReadyFacts(Player* bot, CT::BotExtra& extra, uint32 nowMs)
{
    State& s = Get();
    uint64 const raw = RawOf(bot->GetGUID());
    auto it = s.ready.find(raw);
    if (bot->IsInCombat())
    {
        if (it != s.ready.end())
            ApplyReady(it->second, extra);
        return;
    }
    bool const fresh = it == s.ready.end();
    if (fresh)
        it = s.ready.emplace(raw, State::Ready()).first;
    State::Ready& entry = it->second;
    if (fresh || nowMs - entry.atMs >= RC::kReadyRefreshMs)
    {
        ReadReadyAuras(bot, entry);
        if (fresh || nowMs - entry.keySpellsAtMs >= RC::kKeySpellsRefreshMs)
        {
            ReadReadyKeySpells(bot, entry);
            entry.keySpellsAtMs = nowMs;
        }
        ReadReadyCooldowns(bot, entry);
        entry.atMs = nowMs;
    }
    ApplyReady(entry, extra);
}

// The boss debuffs one bot carries and whether it is charmed: the negative auras from outside its own group. `memberLow` is the binding's own bots
// and commander plus its own group, all by low guid.
void FillBotDebuffs(Player* bot, CT::BotExtra& extra, std::unordered_set<uint32> const& bindingLow)
{
    std::unordered_set<uint32> memberLow = bindingLow;
    memberLow.insert(Ctr(bot->GetGUID()));
    if (Group* group = bot->GetGroup())
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            memberLow.insert(Ctr(slot.guid));
    std::vector<CT::AuraRead> reads;
    for (auto const& entry : bot->GetAppliedAuras())
    {
        AuraApplication const* app = entry.second;
        Aura const* holder = app ? app->GetBase() : nullptr;
        if (!holder || holder->IsPassive())
            continue;
        ObjectGuid const caster = holder->GetCasterGUID();
        if (!CT::IsBossDebuff(!app->IsPositive(), caster.IsCreature(), Ctr(caster), memberLow))
            continue;
        CT::AuraRead read = AuraReadOf(app);
        read.hostileCaster = true;
        reads.push_back(std::move(read));
    }
    extra.debuffs = CT::SelectBotDebuffs(reads);
    extra.mc = bot->IsCharmed();
}

// One hostile near the lead as the card's mob row, or null when the sweep's unit left the world meanwhile.
bool FillMob(Player* lead, Unit* unit, CT::MobInfo& mob)
{
    if (!unit || !unit->IsInWorld())
        return false;
    mob.id = RawOf(unit->GetGUID());
    mob.entry = unit->GetEntry();
    mob.name = unit->GetName();
    mob.hpPct = uint32(unit->GetHealthPct());
    mob.x = unit->GetPositionX();
    mob.y = unit->GetPositionY();
    if (Unit* victim = unit->GetVictim())
        mob.victim = RawOf(victim->GetGUID());
    mob.reaction = lead->IsHostileTo(unit) ? 'h' : lead->IsFriendlyTo(unit) ? 'f' : 'n';
    Creature* creature = unit->ToCreature();
    CreatureTemplate const* info = creature ? creature->GetCreatureTemplate() : nullptr;
    uint32 const rank = info ? uint32(info->Classification) : 0;
    bool const binds = info && (info->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND);
    mob.kind = unit->IsCritter() ? 'k' : creature && creature->IsCivilian() ? 'v' : creature && creature->IsGuard() ? 'g'
        : (unit->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE) ? 'u' : 0);
    mob.level = unit->GetLevel();
    mob.rank = CT::RankKind(rank, binds, ai::GetDungeonPlans().IsBossEntry(unit->GetMapId(), mob.entry));
    mob.cc = CT::CcKind(unit->HasUnitState(UNIT_STATE_STUNNED), unit->HasUnitState(UNIT_STATE_CONFUSED),
        unit->HasUnitState(UNIT_STATE_FLEEING), unit->HasUnitState(UNIT_STATE_ROOT));
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        mob.cast = SpellNameOf(spell->GetSpellInfo());
    return true;
}

void FillMobState(Unit* unit, CT::MobInfo& mob)
{
    mob.auras = CT::SelectUnitAuras(ReadsOf(unit));
    Creature* creature = unit->ToCreature();
    std::vector<int32> immuneMasks;
    for (AuraEffect const* effect : unit->GetAuraEffectsByType(SPELL_AURA_SCHOOL_IMMUNITY))
        if (effect)
            immuneMasks.push_back(effect->GetMiscValue());
    uint32 const immuneMask = CT::FoldImmuneSchools(immuneMasks);
    mob.flags = CT::UnitFlags(unit->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE),
        unit->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_IMMUNE_TO_PC) || CT::ImmuneToAllSchools(immuneMask),
        unit->IsHovering() || unit->IsFlying(), unit->GetStandState() == UNIT_STAND_STATE_DEAD, creature && creature->IsInEvadeMode());
}

// The bosses of the lead's dungeon: the roster of its map with every known boss's state in the lead's own instance. A spawned boss reads its
// creature (alive, or a corpse, or the respawn time the map holds for the spawn); the instance script's encounter state (GetBossState's source)
// marks a boss the module cannot see alive as killed once its encounter is DONE.
void FillBosses(Player* lead, CT::BindingInfo& binding)
{
    Map* map = lead->GetMap();
    if (!map || !map->IsDungeon())
        return;
    std::vector<CT::BossEntrySpawns> const* roster = BossRoster(lead->GetMapId());
    if (!roster)
        return;   // the cache was not built: not reported, never an empty list that would read as "no bosses"
    binding.bossesReported = true;
    binding.bossesMap = lead->GetMapId();
    int64 const now = int64(time(nullptr));
    uint32 const instanceId = map->GetInstanceId();
    InstanceMap* instanceMap = map->ToInstanceMap();
    InstanceScript const* script = instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    CT::BossKillLatch& kills = BossKills();
    kills.Expire(now);
    binding.bosses.clear();
    binding.bosses.reserve(roster->size());
    for (CT::BossEntrySpawns const& boss : *roster)
    {
        char state = 'u';
        bool encounterDone = false;
        for (uint32 dbGuid : boss.dbGuids)
        {
            Creature* creature = map->GetCreature(ObjectGuid::Create<HighGuid::Creature>(uint16(map->GetId()), boss.info.entry, dbGuid));
            int64 const respawnAt = int64(map->GetCreatureRespawnTime(dbGuid));
            state = CT::StrongerBossState(state, CT::BossSpawnState(creature != nullptr, creature && creature->IsAlive(), respawnAt, now));
            if (creature && script && !encounterDone)
                if (DungeonEncounterEntry const* encounter = script->GetBossDungeonEncounter(creature))
                    encounterDone = script->IsEncounterCompleted(encounter->ID);
        }
        binding.bosses.push_back(boss.info);
        binding.bosses.back().state = kills.Apply(instanceId, boss.info.entry, FD::BossState(state, encounterDone), now);
    }
}

void FillObjects(Player* lead, CT::BindingInfo& binding)
{
    Map* map = lead->GetMap();
    if (!map)
        return;
    std::list<GameObject*> found;
    Trinity::AllGameObjectsWithEntryInRange check(lead, 0, Get().objectRange);
    Trinity::GameObjectListSearcher<Trinity::AllGameObjectsWithEntryInRange> searcher(lead, found, check);
    Cell::VisitAllObjects(lead, searcher, Get().objectRange);
    std::vector<FD::Near> near;
    std::unordered_map<uint64, GameObject*> byGuid;
    float const px = lead->GetPositionX(), py = lead->GetPositionY();
    for (GameObject* go : found)
    {
        if (!go || !go->IsInWorld() || !GoKindLetter(go->GetGoType()))
            continue;
        float const dx = go->GetPositionX() - px, dy = go->GetPositionY() - py;
        near.push_back({ RawOf(go->GetGUID()), dx * dx + dy * dy });
        byGuid.emplace(RawOf(go->GetGUID()), go);
    }
    FD::KeepNearest(near, FD::kObjectCap);
    binding.objectsReported = true;
    binding.objects.reserve(near.size());
    for (FD::Near const& n : near)
    {
        auto const it = byGuid.find(n.id);
        if (it == byGuid.end())
            continue;
        GameObject* go = it->second;
        CT::ObjectInfo info;
        info.id = n.id;
        info.entry = go->GetEntry();
        info.name = go->GetName();
        info.x = go->GetPositionX();
        info.y = go->GetPositionY();
        info.type = GoKindLetter(go->GetGoType());
        info.state = FD::ObjectState(go->GetGoState() == GO_STATE_ACTIVE, go->getLootState() == GO_READY);
        binding.objects.push_back(std::move(info));
    }
}

// One binding's map layers. `lead` is its lead bot in the world; `binding.bots` are its bound bots in the world (lead first).
void FillBinding(Player* lead, CT::BindingInfo& binding, uint32 nowMs, WatchedBinding& watch)
{
    Map* map = lead->GetMap();
    if (!map)
        return;
    // The hostiles near the lead: one grid sweep, nearest first, capped (spec: 24 per binding per cycle).
    std::list<Unit*> found;
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(lead, lead, Get().mobRange);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(lead, found, check);
    Cell::VisitAllObjects(lead, searcher, Get().mobRange);
    std::vector<FD::Near> near;
    std::unordered_map<uint64, Unit*> byGuid;
    float const px = lead->GetPositionX(), py = lead->GetPositionY();
    for (Unit* unit : found)
    {
        if (!unit || !unit->IsInWorld())
            continue;
        float const dx = unit->GetPositionX() - px, dy = unit->GetPositionY() - py;
        near.push_back({ RawOf(unit->GetGUID()), dx * dx + dy * dy });
        byGuid.emplace(RawOf(unit->GetGUID()), unit);
    }
    FD::KeepNearest(near, FD::kMobCap);
    binding.mobsReported = true;
    binding.mobsMap = lead->GetMapId();
    binding.mobs.clear();
    binding.mobs.reserve(near.size());
    watch.mobs.clear();
    for (FD::Near const& n : near)
    {
        auto const it = byGuid.find(n.id);
        if (it == byGuid.end())
            continue;
        CT::MobInfo mob;
        if (!FillMob(lead, it->second, mob))
            continue;
        binding.mobs.push_back(std::move(mob));
        watch.mobs.push_back(it->second->GetGUID());
    }
    // The overlay rows (facing, cast, shape, auras, flags): bosses first, then elites, at most the datagram's own overlay cap.
    size_t overlays = 0;
    for (char rank : { 'b', 'e' })
        for (CT::MobInfo& mob : binding.mobs)
        {
            if (overlays >= CT::kMaxMobOverlaysPerPayload)
                break;
            if (mob.rank != rank)
                continue;
            auto const it = byGuid.find(mob.id);
            if (it == byGuid.end() || !it->second)
                continue;
            FillMobFacingAndCast(it->second, mob);
            FillMobState(it->second, mob);
            ++overlays;
        }

    FillObjects(lead, binding);
    FillBosses(lead, binding);

    // What each bound bot is doing: its state, cast and target (field reads), its crowd control, drink/food, the boss debuffs it carries and
    // the ready check (cached). The Turtle view snapshot's action verdict is not ported (the view is cut, D5), so `action` stays empty.
    std::unordered_set<uint32> bindingLow;
    bindingLow.reserve(binding.bots.size() + 1);
    for (CT::BotInfo const& bot : binding.bots)
        bindingLow.insert(bot.guid);
    bindingLow.insert(binding.commanderGuid);
    watch.bots.clear();
    watch.leadGuid = binding.bots.empty() ? 0 : uint32(binding.bots.front().guid);
    for (CT::BotInfo& bot : binding.bots)
    {
        watch.bots.push_back(uint32(bot.guid));
        Player* player = InWorld(PlayerGuid(bot.guid));
        if (!player)
            continue;
        bot.id = RawOf(player->GetGUID());
        ObjectGuid const target = player->GetTarget();
        bot.target = RawOf(target);
        CT::BotExtra& extra = bot.extra;
        bot.hasExtra = true;
        extra.id = bot.id;
        extra.state = !player->IsAlive() ? 'd' : player->IsInCombat() ? 'c' : 'n';
        for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
            if (Spell* spell = player->GetCurrentSpell(type))
            {
                extra.cast = SpellNameOf(spell->GetSpellInfo());
                extra.castTarget = RawOf(spell->m_targets.GetUnitTargetGUID());
                break;
            }
        extra.cc = CT::CcKind(player->HasUnitState(UNIT_STATE_STUNNED), player->HasUnitState(UNIT_STATE_CONFUSED),
            player->HasUnitState(UNIT_STATE_FLEEING), player->HasUnitState(UNIT_STATE_ROOT));
        extra.moving = IsMovingCompat(player);
        if (player->IsAlive() && !player->IsInCombat())
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player))
                extra.rest = ai->HasAura("drink", player) ? 'd' : ai->HasAura("food", player) ? 'e' : 0;
        if (!target.IsEmpty())
            if (Unit* unit = ObjectAccessor::GetUnit(*player, target))
                extra.targetName = unit->GetName();
        FillBotDebuffs(player, extra, bindingLow);
        ReadReadyFacts(player, extra, nowMs);
        if (player->IsInCombat())
            watch.combat = true;
    }
}

}

void LoadFeeds()
{
    State& s = Get();
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Commander.Feeds", true);
    s.mobRange = FD::ClampRange(float(sConfigMgr->GetIntDefault("TortoiseBots.Commander.MobRange", int32(FD::kDefaultMobRange))),
        FD::kDefaultMobRange);
    s.objectRange = FD::ClampRange(float(sConfigMgr->GetIntDefault("TortoiseBots.Commander.ObjectRange", int32(FD::kDefaultObjectRange))),
        FD::kDefaultObjectRange);
    s.posMs = FD::ClampPosMs(sConfigMgr->GetIntDefault("TortoiseBots.Commander.PosMs", int32(FD::kDefaultPosMs)));
    if (ai::GetDungeonPlans().Sealed() && !Bosses().built)
        BuildBossCache();
}

bool FeedsActive()
{
    State const& s = Get();
    return s.enabled && BotMgr::Instance().Enabled();
}

void FillFeeds(std::vector<CT::BindingInfo>& out)
{
    State& s = Get();
    if (!FeedsActive() || out.empty())
        return;
    // A plan reload (`.tb run reloadplans`) replaces the plan table: the boss roster is built again from the new rows, once, here rather than in
    // the reload command (that file belongs to another package).
    if (ai::GetDungeonPlans().Sealed() && ai::DungeonPlanTableGeneration().load(std::memory_order_acquire) != Bosses().generation)
        BuildBossCache();
    auto const started = std::chrono::steady_clock::now();
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::unordered_set<uint32> standing;
    for (CT::BindingInfo& binding : out)
    {
        Player* lead = nullptr;
        for (CT::BotInfo const& bot : binding.bots)
            if (bot.lead)
            {
                lead = InWorld(PlayerGuid(bot.guid));
                break;
            }
        if (!lead && !binding.bots.empty())
            lead = InWorld(PlayerGuid(binding.bots.front().guid));
        if (!lead)
            continue;   // no lead in the world: the layers stay unreported (null), never an empty list
        WatchedBinding& watch = s.watched[binding.commanderGuid];
        watch.commander = binding.commander;
        watch.combat = false;
        FillBinding(lead, binding, nowMs, watch);
        watch.leadGuid = Ctr(lead->GetGUID());
        standing.insert(binding.commanderGuid);
    }
    for (auto it = s.watched.begin(); it != s.watched.end();)
        it = standing.count(it->first) ? std::next(it) : s.watched.erase(it);
    std::unordered_set<uint64> bots;
    for (CT::BindingInfo const& binding : out)
        for (CT::BotInfo const& bot : binding.bots)
            bots.insert(bot.id);
    for (auto it = s.ready.begin(); it != s.ready.end();)
        it = bots.count(it->first) ? std::next(it) : s.ready.erase(it);

    uint32 const us = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    ++s.fillCycles;
    s.fillSumUs += us;
    s.fillMaxUs = std::max(s.fillMaxUs, us);
}

void UpdateFeeds(uint32 diff)
{
    State& s = Get();
    if (!FeedsActive() || !DashLink::Active())
        return;
    s.sincePosMs += diff;
    s.logMs += diff;
    if (s.sincePosMs < s.posMs)
        return;
    s.sincePosMs = 0;
    if (s.watched.empty())
        return;
    auto const started = std::chrono::steady_clock::now();
    // The live positions of the bindings of this window: the bots of the last full report and the enemies it listed, resolved by guid on the
    // lead's map (a unit that left the report is simply gone; the next report brings new ones).
    std::vector<uint32> keys;
    keys.reserve(s.watched.size());
    for (auto const& entry : s.watched)
        keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    FD::PosWindow const window = FD::NextPosWindow(keys.size(), s.posStart, CT::kMaxPosBindings);
    s.posStart = window.next;
    int64 const tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64 const ts = int64(time(nullptr));
    for (size_t i = window.first; i < window.first + window.count; ++i)
    {
        WatchedBinding& watch = s.watched[keys[i]];
        Player* lead = watch.leadGuid ? InWorld(PlayerGuid(watch.leadGuid)) : nullptr;
        if (!lead)
            continue;
        CT::PosRef ref;
        ref.commander = watch.commander;   // the name the COMMANDERS report of the same binding carries (the daemon matches by it)
        ref.commanderGuid = keys[i];
        ref.combat = watch.combat;
        for (uint32 guid : watch.bots)
        {
            Player* bot = InWorld(PlayerGuid(guid));
            if (!bot)
                continue;
            CT::PosBot pos;
            pos.g = guid;
            pos.map = bot->GetMapId();
            pos.x = bot->GetPositionX();
            pos.y = bot->GetPositionY();
            pos.z = bot->GetPositionZ();
            pos.o = bot->GetOrientation();
            pos.alive = bot->IsAlive();
            pos.hp = pos.alive ? uint32(bot->GetHealth()) : 0;
            pos.maxHp = uint32(bot->GetMaxHealth());
            ref.bots.push_back(pos);
        }
        if (ref.bots.empty())
            continue;
        for (ObjectGuid guid : watch.mobs)
        {
            Unit* unit = ObjectAccessor::GetUnit(*lead, guid);
            if (!unit)
                continue;
            CT::PosMob mob;
            mob.id = RawOf(guid);
            mob.x = unit->GetPositionX();
            mob.y = unit->GetPositionY();
            mob.hpPct = uint32(unit->GetHealthPct());
            if (Unit* victim = unit->GetVictim())
                mob.victim = RawOf(victim->GetGUID());
            ref.mobs.push_back(mob);
        }
        DashLink::Send(CT::BuildPosPayload(12, DashLink::Session(), DashLink::NextSeq(), ts, tsMs, ref));
    }
    uint32 const us = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    ++s.posCycles;
    s.posSumUs += us;
    s.posMaxUs = std::max(s.posMaxUs, us);
    if (s.logMs >= kCostLogMs)
    {
        s.logMs = 0;
        sLog.outString("TortoiseBots: commander feeds 60s: %llu fill cycles, %llu us each on average, %u us at most; %llu position cycles, %llu us each, %u us at most",
            (unsigned long long)s.fillCycles, (unsigned long long)(s.fillCycles ? s.fillSumUs / s.fillCycles : 0), s.fillMaxUs,
            (unsigned long long)s.posCycles, (unsigned long long)(s.posCycles ? s.posSumUs / s.posCycles : 0), s.posMaxUs);
        s.fillCycles = s.fillSumUs = s.posCycles = s.posSumUs = 0;
        s.fillMaxUs = s.posMaxUs = 0;
    }
}
}
}

#else

namespace TortoiseBots
{
namespace cmd
{
void LoadFeeds() {}
void UpdateFeeds(uint32) {}
void FillFeeds(std::vector<::TortoiseBots::CommanderTelemetry::BindingInfo>&) {}
bool FeedsActive() { return false; }
}
}

#endif
