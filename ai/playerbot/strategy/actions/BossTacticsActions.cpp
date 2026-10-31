#include "playerbot/playerbot.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <unordered_map>
#include "BossTacticsActions.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GroupMembers.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include "ChooseTargetActions.h"
#include "PositionAction.h"
#include "FollowActions.h"
#include "playerbot/strategy/values/NearestGameObjects.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

namespace
{
// Interrupt spells by name; the bot casts the first one it knows and can cast
// on the target right now (range, cooldown, stance and mana are CanCastSpell's).
const char* const kInterruptSpells[] = { "pummel", "shield bash", "kick", "counterspell", "earth shock" };

// A bot on tank or healer duty keeps it; an explicit player order wins.
bool IsFreeDps(PlayerbotAI* ai, Player* bot)
{
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
}
}

BossTacticsFight ai::ScanBossTactics(PlayerbotAI* ai, Player* bot)
{
    BossTacticsFight fight;
    const BossTacticTable& table = GetBossTactics();
    const uint32 mapId = bot->GetMapId();
    if (!table.HasMap(mapId))
        return fight;

    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    std::vector<uint32> seen;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        fight.units.push_back(unit);
        const uint32 entry = unit->GetEntry();
        if (std::find(seen.begin(), seen.end(), entry) != seen.end())
            continue;
        seen.push_back(entry);
        if (const std::vector<BossTacticRow>* rows = table.Rows(mapId, entry))
        {
            for (const BossTacticRow& row : *rows)
                fight.rows.push_back(&row);
        }
    }
    return fight;
}

BossInterruptScan ai::FindBossInterrupt(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    BossInterruptScan scan;
    (void)bot;
    for (Unit* unit : fight.units)
    {
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || !spell->m_spellInfo || !IsInterruptListed(fight.rows, unit->GetEntry(), spell->m_spellInfo->Id))
            continue;
        for (const char* name : kInterruptSpells)
        {
            if (ai->CanCastSpell(std::string(name), unit))
            {
                scan.caster = unit;
                scan.spell = name;
                return scan;
            }
        }
    }
    return scan;
}

Unit* ai::FindBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    if (fight.rows.empty() || !IsFreeDps(ai, bot))
        return nullptr;

    std::vector<TacticCandidate> candidates;
    candidates.reserve(fight.units.size());
    for (Unit* unit : fight.units)
    {
        TacticCandidate c;
        c.entry = unit->GetEntry();
        c.distance = bot->GetDistance(unit);
        c.pickable = PossibleAttackTargetsValue::IsValid(unit, bot);
        candidates.push_back(c);
    }
    const int index = PickKillFirst(fight.rows, candidates);
    return index < 0 ? nullptr : fight.units[index];
}

Unit* ai::FindBossFrontDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    if (PlayerbotAI::IsTank(bot))
        return nullptr;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::Behind)
            continue;
        for (Unit* unit : fight.units)
        {
            if (unit->GetEntry() != TacticSubjectEntry(*row))
                continue;
            // The one holding the boss stays where it is.
            if (unit->GetVictim() == bot || !unit->GetVictim())
                continue;
            if (InsideBossFront(unit->GetPositionX(), unit->GetPositionY(), unit->GetOrientation(),
                                bot->GetPositionX(), bot->GetPositionY(), row->param))
                return unit;
        }
    }
    return nullptr;
}

float ai::CastDangerOf(const BossTacticsFight& fight, Unit* unit)
{
    if (!unit)
        return 0.0f;
    Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell || !spell->m_spellInfo)
        return 0.0f;
    return AvoidCastRadius(fight.rows, unit->GetEntry(), spell->m_spellInfo->Id);
}

namespace
{
// The core hits a player whose centre is within the radius of the blast's source, in 3D and with no size factor
// (Spell.cpp SpellNotifierCreatureAndPlayer, PUSH_SRC_CENTER / PUSH_SELF_CENTER); a self-centred cast's source is
// the caster's position.
float CenterDistance(Unit* caster, float x, float y, float z)
{
    const float dx = caster->GetPositionX() - x;
    const float dy = caster->GetPositionY() - y;
    const float dz = caster->GetPositionZ() - z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A tank the caster is on stays in the blast (BossTacticsPolicy.h HoldsCaster).
bool BotHoldsCaster(Player* bot, Unit* caster)
{
    return HoldsCaster(PlayerbotAI::IsTank(bot), caster->GetVictim() == bot);
}
}

std::vector<CastDangerZone> ai::CastDangerZones(const BossTacticsFight& fight, Player* bot)
{
    std::vector<CastDangerZone> zones;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f || (bot && BotHoldsCaster(bot, unit)))
            continue;
        CastDangerZone zone;
        zone.x = unit->GetPositionX();
        zone.y = unit->GetPositionY();
        zone.z = unit->GetPositionZ();
        zone.radius = radius;
        zones.push_back(zone);
    }
    return zones;
}

BossCastDanger ai::FindBossCastDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    BossCastDanger danger;
    if (fight.rows.empty())
        return danger;
    float nearest = 0.0f;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f)
            continue;
        const float distance = CenterDistance(unit, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        if (!MustLeaveCast(BotHoldsCaster(bot, unit), radius, distance))
            continue;
        if (!danger.caster || distance < nearest)
        {
            danger.caster = unit;
            danger.radius = radius;
            nearest = distance;
        }
    }
    return danger;
}

bool BossTacticsInterruptAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossInterruptScan scan = FindBossInterrupt(ai, bot, fight);
    return scan.caster && ai->CastSpell(std::string(scan.spell), scan.caster);
}

bool BossTacticsKillFirstAction::Execute(Event& event)
{
    (void)event;
    Unit* pick = FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot));
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current == pick)
        return false;
    return Attack(bot, pick);
}

float BossTacticsHoldMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    const bool assist = dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action);
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    if (!assist && !aoe)
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!current)
        return 1.0f;
    return FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot)) == current ? 0.0f : 1.0f;
}

float BossTacticsCastHoldMultiplier::GetValue(Action* action)
{
    if (!action || !GetBossTactics().HasAvoidCastMap(bot->GetMapId()))
        return 1.0f;
    MovementAction* move = dynamic_cast<MovementAction*>(action);
    if (!move)
        return 1.0f;
    bool const walkOut = dynamic_cast<BossTacticsAvoidCastAction*>(action) != nullptr;
    if (walkOut)
        return 1.0f;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    if (fight.rows.empty())
        return 1.0f;
    const std::vector<CastDangerZone> zones = CastDangerZones(fight, bot);
    if (zones.empty())
        return 1.0f;
    // A stay walks the bot back to its spot: held while that spot lies in a blast.
    if (dynamic_cast<ReturnToStayPositionAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        PositionMap& positions = AI_VALUE(PositionMap&, "position");
        auto stay = positions.find("stay");
        bool const has = stay != positions.end() && stay->second.isSet() && stay->second.mapId == bot->GetMapId();
        return has && CastHoldsMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, zones) ? 0.0f
                                                                                                             : 1.0f;
    }
    // Any other move towards a unit (a reach, a chase, a heal walk, a follow): held while that unit stands in a blast.
    // A follow does not name its unit as the action's target (it reads "follow target" in Execute), and it walks to
    // the bot's formation spot around that unit: held while either the unit or that spot stands in a blast.
    Unit* target = nullptr;
    if (dynamic_cast<FollowAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        target = AI_VALUE(Unit*, "follow target");
        WorldPosition const spot = AI_VALUE(WorldPosition, "formation position");
        if (spot.GetMapId() == bot->GetMapId() &&
            CastHoldsMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), zones))
            return 0.0f;
    }
    else
        target = action->GetTarget();
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return 1.0f;
    return CastHoldsMove(false, target == bot, true, target->GetPositionX(), target->GetPositionY(),
                         target->GetPositionZ(), zones)
               ? 0.0f
               : 1.0f;
}

float TrapHoldMultiplier::GetValue(Action* action)
{
    if (!action || !dynamic_cast<MovementAction*>(action))
        return 1.0f;
    // Both walk-outs must run: the step out of the ground effect, and the listed
    // cast's own way out (its destination may lie in a fire beside the blast).
    if (dynamic_cast<AvoidAoeAction*>(action) || dynamic_cast<BossTacticsAvoidCastAction*>(action))
        return 1.0f;
    std::vector<AoeHazardCircle> hazards;
    CollectDamagingTrapHazards(ai, bot, hazards);
    if (hazards.empty())
        return 1.0f;
    // A stay walks the bot back to its spot: held while that spot lies in a fire.
    if (dynamic_cast<ReturnToStayPositionAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        PositionMap& positions = AI_VALUE(PositionMap&, "position");
        auto stay = positions.find("stay");
        bool const has = stay != positions.end() && stay->second.isSet() && stay->second.mapId == bot->GetMapId();
        return has && HazardsHoldMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, hazards)
                   ? 0.0f
                   : 1.0f;
    }
    // Any other move towards a unit (a reach, a chase, a heal walk, a follow):
    // held while that unit stands in a fire; a follow also for its spot.
    Unit* target = nullptr;
    if (dynamic_cast<FollowAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        target = AI_VALUE(Unit*, "follow target");
        WorldPosition const spot = AI_VALUE(WorldPosition, "formation position");
        if (spot.GetMapId() == bot->GetMapId() &&
            HazardsHoldMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), hazards))
            return 0.0f;
    }
    else
        target = action->GetTarget();
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return 1.0f;
    return HazardsHoldMove(false, target == bot, true, target->GetPositionX(), target->GetPositionY(),
                           target->GetPositionZ(), hazards)
               ? 0.0f
               : 1.0f;
}

// Not MovementAction::isUseful: that one is false on a stay, and the bots of a run fight on one (a pull's anchor hold,
// a healer's stand). The blast kills in one hit; the stay walks the bot back once the cast is over.
bool BossTacticsAvoidCastAction::isUseful()
{
    return true;
}

namespace
{
// The walk-out's candidate points: the clear ring points (AvoidCastEscapes, shortest walk first), each on the ground
// and checked against every blast in 3D and for sight. Writes up to kAvoidCastPoints points; returns how many.
struct AvoidCastSpot
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

int AvoidCastSpots(Player* bot, Unit* caster, float radius, const std::vector<CastDangerZone>& zones, AvoidCastSpot* out)
{
    AvoidCastPoint points[kAvoidCastPoints];
    const int count = AvoidCastEscapes(caster->GetPositionX(), caster->GetPositionY(), bot->GetPositionX(),
                                       bot->GetPositionY(), radius, bot->GetOrientation() + M_PI_F, zones, points);
    int n = 0;
    for (int i = 0; i < count; ++i)
    {
        float z = std::max(caster->GetPositionZ(), bot->GetPositionZ()) + 2.0f;
        bot->UpdateGroundPositionZ(points[i].x, points[i].y, z);
        // The ground under a ring point can lie on another floor (a ledge, the stairs below) and so inside the 3D
        // radius again; another caster's blast is no way out either.
        if (InsideAnyCastZone(zones, points[i].x, points[i].y, z))
            continue;
        if (!bot->IsWithinLOS(points[i].x, points[i].y, z + bot->GetCollisionHeight()))
            continue;
        out[n].x = points[i].x;
        out[n].y = points[i].y;
        out[n].z = z;
        ++n;
    }
    return n;
}
}

// Possible only with a usable way out, so the reaction engine does not pick the walk-out for a bot that has none (it
// would keep its cast; see ShouldReactionInterruptCast).
bool BossTacticsAvoidCastAction::isPossible()
{
    if (!MovementAction::isPossible() || !ai->CanMove())
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    AvoidCastSpot spots[kAvoidCastPoints];
    return AvoidCastSpots(bot, danger.caster, danger.radius, CastDangerZones(fight, bot), spots) > 0;
}

namespace
{
// Each bot's last walk-out (BossTacticsPolicy.h AvoidCastLogState), by bot guid, so one cast logs one line per bot.
// Only touched while bot_events.csv is on; a mutex guards it because every bot's engine reaches it.
std::mutex g_avoidCastLogMutex;
std::unordered_map<uint64, AvoidCastLogState> g_avoidCastLog;

// One bot_events.csv row per bot per cast (the audit of the boss tactics' avoid_cast walk-out; the walk-6/7 sampler of
// the bots' current action missed a walk that lasts less than its poll). Row: event BossTacticsAvoidCast, then
//   info1 = "caster=<entry>:<guid low>,spell=<id>"
//   info2 = "dist=<bot to caster centre before the move>,radius=<row radius>,dest=<distance of the point MoveTo took,
//            or none>,stopped=<1 when the walk-out broke the bot's own cast>,walk=<started|deferred|none>"
// walk: started = a walk is under way; deferred = MoveTo took the point but no walk started (a collapsed path, a
// transport or teleport wait); none = no usable point. A no-op unless bot_events.csv is in AiPlayerbot.AllowedLogFiles,
// like the hunter telemetry; the throttle is checked before any string is built.
void LogAvoidCast(PlayerbotAI* ai, Player* bot, Unit* caster, Spell* spell, uint32 spellId, float radius, float distBefore,
                  float destDistance, bool stopped, AvoidCastWalk walk)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_avoidCastLogMutex);
        if (!AvoidCastIsNewCast(g_avoidCastLog[bot->GetObjectGuid().GetRawValue()], caster->GetObjectGuid().GetRawValue(),
                                spellId, static_cast<uint64>(reinterpret_cast<std::uintptr_t>(spell)), nowMs))
            return;
    }
    char info1[64];
    snprintf(info1, sizeof(info1), "caster=%u:%u,spell=%u", caster->GetEntry(), caster->GetGUIDLow(), spellId);
    char info2[112];
    if (destDistance >= 0.0f)
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=%.1f,stopped=%d,walk=%s", distBefore, radius,
                 destDistance, stopped ? 1 : 0, AvoidCastWalkName(walk));
    else
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=none,stopped=%d,walk=%s", distBefore, radius,
                 stopped ? 1 : 0, AvoidCastWalkName(walk));
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsAvoidCast", info1, info2);
}

bool WalkUnderWay(Player* bot)
{
    return sServerFacade.isMoving(bot) || !bot->movespline->Finalized();
}
}

bool BossTacticsAvoidCastAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    Unit* caster = danger.caster;
    const bool logIt = sPlayerbotAIConfig.hasLog("bot_events.csv");
    const float distBefore = logIt ? CenterDistance(caster, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ())
                                   : 0.0f;
    AvoidCastSpot spots[kAvoidCastPoints];
    const int count = AvoidCastSpots(bot, caster, danger.radius, CastDangerZones(fight, bot), spots);
    bool stopped = false;
    float destDistance = -1.0f;
    AvoidCastWalk walk = AvoidCastWalk::None;
    for (int i = 0; i < count && walk != AvoidCastWalk::Started; ++i)
    {
        // A cast of the bot's own would hold it in the blast. MoveTo breaks a running cast itself once it launches the
        // walk (MoveTo2); what is left (a wand's auto-repeat) is stopped only once a walk is under way, so a bot with
        // no way out keeps its heal or bolt.
        bool const casting = bot->IsNonMeleeSpellCasted(true);
        if (!MoveTo(bot->GetMapId(), spots[i].x, spots[i].y, spots[i].z, false, false, false, true))
            continue;
        // MoveTo can say yes without a walk (a collapsed path rebuilt next tick, a transport or teleport wait): only a
        // walk under way counts; otherwise the next point is tried.
        if (!WalkUnderWay(bot))
        {
            walk = AvoidCastWalk::Deferred;
            continue;
        }
        walk = AvoidCastWalk::Started;
        if (casting)
        {
            if (bot->IsNonMeleeSpellCasted(true))
                bot->CastStop();
            stopped = true;
        }
        if (logIt)
            destDistance = CenterDistance(caster, spots[i].x, spots[i].y, spots[i].z);
    }
    if (logIt)
    {
        Spell* spell = caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        LogAvoidCast(ai, bot, caster, spell, spell && spell->m_spellInfo ? spell->m_spellInfo->Id : 0, danger.radius,
                     distBefore, destDistance, stopped, walk);
    }
    return walk == AvoidCastWalk::Started;
}

bool BossTacticsStepBehindAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsStepBehindAction::Execute(Event& event)
{
    (void)event;
    Unit* boss = FindBossFrontDanger(ai, bot, ScanBossTactics(ai, bot));
    if (!boss)
        return false;
    float x = 0.0f, y = 0.0f;
    BossFlankDestination(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
                         bot->GetPositionX(), bot->GetPositionY(), 3.0f, x, y);
    float z = boss->GetPositionZ();
    bot->UpdateGroundPositionZ(x, y, z);
    return MoveTo(bot->GetMapId(), x, y, z);
}

namespace
{
// What each bot remembers of the window it was seated in (BossStandPolicy.h StandHold), by guid. A mutex guards it:
// the plan is asked for from the trigger, the action, the reach and the dodge of every bot.
std::mutex g_standHoldMutex;
std::unordered_map<uint64, StandHold> g_standHolds;
// Seats the mover refused, by bot guid (StandFailure): three in a row for one seat and the bot gets no seat.
std::unordered_map<uint64, StandFailure> g_standFailures;
// Entries in the two maps: a bot with nothing to forget costs one atomic read, not a lock.
std::atomic<size_t> g_standStateCount{0};

// The seat plan of an open window (PlanStandSeats), by boss instance (BossStandPolicy.h StandPlanKey), anchor and
// role: built by the first bot that sees the window and read by every bot of the role, so all of them walk to one plan
// and nobody's seat flips while bots move. A plan nobody read for kBossStandPlanIdleMs is stale: the sweep below drops
// it, whatever its key, and the next window builds a new one. Guarded by g_standHoldMutex.
struct StandPlan
{
    std::vector<StandSeat> seats;
    uint32 lastReadMs = 0;
};
std::unordered_map<StandPlanKey, StandPlan, StandPlanKeyHash> g_standPlans;

// Drops what is remembered of a bot (it died, left, or is on a map without stand rows).
void ForgetStand(uint64 key)
{
    if (g_standStateCount.load(std::memory_order_relaxed) == 0)
        return;
    std::lock_guard<std::mutex> lock(g_standHoldMutex);
    g_standHolds.erase(key);
    g_standFailures.erase(key);
    g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
}
}

// The window spell of the row the boss is stopped at: the same detection FindBossStand feeds StandPointsNow (the aura,
// the anchor radius and the stopped-without-a-target state), read for the commander snapshot, whose cast read only sees
// cast slots a triggered spell never reaches. Core: Unit::IsStopped (Objects/Unit.h:1544), Unit::GetTargetGuid
// (Objects/Unit.h:1358).
std::uint32_t ai::ActiveStandWindowSpell(Unit* boss)
{
    if (!boss)
        return 0;
    const std::vector<BossStandRow>* rows = GetBossStands().Rows(boss->GetMapId(), boss->GetEntry());
    if (!rows)
        return 0;
    return StoppedStandWindowSpell(*rows, boss->GetPositionX(), boss->GetPositionY(),
                                   [boss](uint32 aura) { return boss->HasAura(aura); }, boss->IsStopped(),
                                   !boss->GetTargetGuid().IsEmpty());
}

BossStandTarget ai::FindBossStand(PlayerbotAI* ai, Player* bot)
{
    BossStandTarget target;
    if (!ai || !bot)
        return target;
    const uint64 selfKey = bot->GetObjectGuid().GetRawValue();
    // A bot that is dead, gone or teleporting keeps nothing of its last window.
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
    {
        ForgetStand(selfKey);
        return target;
    }
    // A map without rows costs one lookup and nothing else.
    const BossStandTable& table = GetBossStands();
    const uint32 mapId = bot->GetMapId();
    if (!table.HasMap(mapId))
    {
        ForgetStand(selfKey);
        return target;
    }
    // The rows only act for a bot whose tactics are armed (the same gate as BossTacticsTriggers.cpp Ready).
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
    {
        ForgetStand(selfKey);
        return target;
    }

    const StandRole role = StandRoleOf(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));

    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    const uint32 nowMs = WorldTimer::getMSTime();
    StandHold hold;
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto it = g_standHolds.find(selfKey);
        if (it != g_standHolds.end())
            hold = it->second;
    }
    // What the other bots can see of this one: its hold as of the last tick, before this tick arms it again.
    const StandHold heldBefore = hold;
    bool sawBoss = false;
    Unit* boss = nullptr;
    const std::vector<BossStandRow>* bossRows = nullptr;
    std::vector<const BossStandRow*> points;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        // While a hold is active only its own boss is read: another boss with rows must not clear it.
        if (hold.armed && hold.bossEntry != 0 && unit->GetEntry() != hold.bossEntry)
            continue;
        const std::vector<BossStandRow>* rows = table.Rows(mapId, unit->GetEntry());
        if (!rows)
            continue;
        // The spell the boss is casting now (a cast with a cast time, as FindBossInterrupt reads it).
        uint32 casting = 0;
        if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            if (spell->m_spellInfo)
                casting = spell->m_spellInfo->Id;
        }
        // A spell cast triggered (a breath) never shows as the current spell: the state the cast leaves opens the
        // window of a row whose signal says so (the boss stands still and has no target; the row also names the aura
        // it has to wear and the anchor it has to be at).
        const bool stoppedNoTarget = unit->IsStopped() && unit->GetTargetGuid().IsEmpty();
        // The open window's points; after the cast the same seats for up to the row's hold time, until the boss
        // is at the release anchor (StandPointsNow arms, keeps and clears the hold).
        sawBoss = true;
        points = StandPointsNow(*rows, role, unit->GetPositionX(), unit->GetPositionY(), casting,
                                [unit](uint32 aura) { return unit->HasAura(aura); }, hold, nowMs, stoppedNoTarget);
        if (!points.empty())
        {
            boss = unit;
            bossRows = rows;
            break;
        }
    }

    // The bots that share the seats: this group's bots of the same role that take part, by one rule for every bot that
    // evaluates the plan (StandMemberCounts: in the fight with THIS boss within range of the anchor, or already
    // holding a stand for this boss and anchor), so all of them compute the same plan. A bot elsewhere in the
    // instance, or one the boss is not fighting, reserves no seat.
    bool selfCounts = false;
    std::vector<StandBot> bots;
    const ObjectGuid selfGuid = bot->GetObjectGuid();
    if (boss)
    {
        const float anchorX = points[0]->anchorX;
        const float anchorY = points[0]->anchorY;
        const uint32 bossEntry = boss->GetEntry();
        const uint32 anchor = points[0]->anchor;
        const auto candidateOf = [&](Player* member, const StandHold& memberHold)
        {
            StandCandidate candidate;
            candidate.id = member->GetObjectGuid().GetRawValue();
            candidate.x = member->GetPositionX();
            candidate.y = member->GetPositionY();
            // On the boss's threat list (as AttackersValue.cpp:367 reads it), or the boss's victim.
            candidate.engaged = boss->GetThreatManager().getThreat(member) > 0.0f || boss->GetVictim() == member;
            candidate.holdsSameAnchor = memberHold.armed && memberHold.bossEntry == bossEntry && memberHold.anchor == anchor;
            return candidate;
        };
        std::vector<StandCandidate> candidates;
        const StandCandidate self = candidateOf(bot, heldBefore);
        selfCounts = StandMemberCounts(self, anchorX, anchorY);
        candidates.push_back(self);
        if (Group* group = bot->GetGroup())
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == bot || !member->IsInWorld() || member->GetMapId() != mapId || !sServerFacade.IsAlive(member))
                    continue;
                PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
                if (!memberAi || !memberAi->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
                    continue;
                if (StandRoleOf(PlayerbotAI::IsTank(member), PlayerbotAI::IsHeal(member), ai->IsRanged(member)) != role)
                    continue;
                StandHold memberHold;
                {
                    std::lock_guard<std::mutex> lock(g_standHoldMutex);
                    auto it = g_standHolds.find(member->GetObjectGuid().GetRawValue());
                    if (it != g_standHolds.end())
                        memberHold = it->second;
                }
                candidates.push_back(candidateOf(member, memberHold));
            }
        }
        bots = SelectStandBots(candidates, anchorX, anchorY);
    }

    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        // The hold is kept for a bot that takes part (it is what the others read); a bot that does not count leaves
        // none behind. A tick that sees no boss with rows (it dropped off the attacker list for a moment) keeps the
        // hold until its time is up; StandPointsNow clears it itself when a boss is seen and the hold is over.
        const bool kept = hold.armed &&
            (boss ? selfCounts : (sawBoss || static_cast<uint32>(nowMs - hold.lastOpenMs) < hold.holdMs));
        if (kept)
            g_standHolds[selfKey] = hold;
        else
        {
            g_standHolds.erase(selfKey);
            g_standFailures.erase(selfKey);  // the window is over: the next one starts afresh
        }
        g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
    }
    if (points.empty())
        return target;

    // The side is the side of the lane (anchor -> release anchor) the bot stands on. The seats come from the window's
    // one plan (PlanStandSeats), built once from the positions when the window opened and kept while it is read.
    const StandLane lane = bossRows ? StandLaneOf(*bossRows, *points[0]) : StandLane();
    StandSeat seat;
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        Group* group = bot->GetGroup();
        // a bot with no group plans alone: its own guid keeps two groupless bots from sharing one plan
        const uint64 groupGuid = group ? group->GetLeaderGuid().GetRawValue() : bot->GetObjectGuid().GetRawValue();
        const StandPlanKey key{ boss->GetObjectGuid().GetRawValue(), boss->GetMapId(), boss->GetInstanceId(),
                                points[0]->anchor, static_cast<uint8>(role), points[0]->windowSpell, groupGuid };
        auto it = g_standPlans.find(key);
        if (it == g_standPlans.end() || StandPlanStale(nowMs, it->second.lastReadMs))
        {
            for (auto old = g_standPlans.begin(); old != g_standPlans.end();)
                old = StandPlanStale(nowMs, old->second.lastReadMs) ? g_standPlans.erase(old) : std::next(old);
            StandPlan plan;
            plan.seats = PlanStandSeats(points, bots, lane);
            it = g_standPlans.insert_or_assign(key, std::move(plan)).first;
        }
        it->second.lastReadMs = nowMs;
        for (const StandSeat& planned : it->second.seats)
        {
            if (planned.id == selfGuid.GetRawValue())
            {
                seat = planned;
                break;
            }
        }
    }
    // A bot the plan did not seat (its side was full, or it was not in the fight when the window opened) keeps the
    // reactive dodge.
    if (!seat.valid)
        return target;
    // A seat the mover refused three times in a row is given up on: the reach and the dodge take over.
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto failure = g_standFailures.find(selfKey);
        if (failure != g_standFailures.end() && StandSeatGivenUp(failure->second, seat.x, seat.y))
            return target;
    }
    target.valid = true;
    target.x = seat.x;
    target.y = seat.y;
    target.z = seat.z;
    return target;
}

bool BossTacticsStandAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsStandAction::Execute(Event& event)
{
    (void)event;
    const BossStandTarget stand = FindBossStand(ai, bot);
    if (!stand.valid || StandArrived(bot->GetPositionX(), bot->GetPositionY(), stand.x, stand.y))
    {
        hasLast = false;
        return false;
    }
    // Already walking to this seat: leave the spline alone.
    if (hasLast && !bot->IsStopped() && !bot->movespline->Finalized() &&
        std::fabs(lastX - stand.x) < 1.0f && std::fabs(lastY - stand.y) < 1.0f)
        return true;

    // The mover ignores a destination whose z is off the floor, so the seat is put back on the ground. A far seat's
    // floor can lie above the row's z, so the probe starts above both the row's and the bot's own; a probe that
    // comes back unchanged found nothing and the row's z stays.
    const float probeZ = StandProbeZ(stand.z, bot->GetPositionZ());
    float z = probeZ;
    bot->UpdateGroundPositionZ(stand.x, stand.y, z);
    z = StandSnappedZ(stand.z, probeZ, z);
    bot->AttackStop();
    // The cast of a bot on its way out is of no use: stop it, as the reactive dodge does - unless it is already close
    // to its seat, where a cast in progress is let finish.
    if (StandShouldStopCast(bot->GetPositionX(), bot->GetPositionY(), stand.x, stand.y))
        bot->CastStop();
    const uint64 key = bot->GetObjectGuid().GetRawValue();
    if (!MoveTo(bot->GetMapId(), stand.x, stand.y, z, false, false, false, true))
    {
        // A seat the mover keeps refusing is given up on after three tries (FindBossStand returns invalid for it).
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        StandNoteMoveFailure(g_standFailures[key], stand.x, stand.y);
        g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_standHoldMutex);
        auto failure = g_standFailures.find(key);
        if (failure != g_standFailures.end())
        {
            g_standFailures.erase(failure);
            g_standStateCount.store(g_standHolds.size() + g_standFailures.size(), std::memory_order_relaxed);
        }
    }
    lastX = stand.x;
    lastY = stand.y;
    hasLast = true;
    return true;
}
