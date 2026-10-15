
#include "playerbot/playerbot.h"
#include "PossibleTargetsValue.h"
#include "PossibleAttackTargetsValue.h"
#include "FreeMoveValues.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <atomic>

using namespace ai;
using namespace MaNGOS;

std::list<ObjectGuid> PossibleTargetsValue::Calculate()
{
    // Phase 2 guarded scan cadence (Issue #175): the Cell grid visit below is
    // the hottest world-thread cost (~60% of tick). True idle (taxi/rested
    // sanctuary) returns empty immediately; roaming bots reuse the cached
    // list at a staggered 1s cadence. Combat/death/damage/grace/human guards
    // inside ShouldReuseSpatialScan() force full 100ms rate. Throttling only
    // delays *discovery* of new grind candidates: attackers/threat, current
    // targets and explicit commands bypass this value entirely.
    if (ai->IsSpatialScanIdle())
        return std::list<ObjectGuid>();
    if (ai->ShouldReuseSpatialScan())
        return LazyGet();

    float rangeCheck = range;
    bool shouldIgnoreValidate = false;
    if (!qualifier.empty())
    {
        rangeCheck = Qualified::getMultiQualifierInt(qualifier, 0, ":");
        shouldIgnoreValidate = Qualified::getMultiQualifierInt(qualifier, 1, ":");
    }

    std::list<Unit*> targets;
    FindPossibleTargets(bot, targets, rangeCheck);

    std::list<ObjectGuid> results;
    for (std::list<Unit*>::iterator i = targets.begin(); i != targets.end(); ++i)
    {
        Unit* unit = *i;
        if (unit && (shouldIgnoreValidate || AcceptUnit(unit)))
        {
            results.push_back(unit->getObjectGuid());
        }
    }

    Set(results);
    ai->NoteSpatialScan();
    return results;
}

void PossibleTargetsValue::FindUnits(std::list<Unit*> &targets)
{
    FindPossibleTargets(bot, targets, range);
}

bool PossibleTargetsValue::AcceptUnit(Unit* unit)
{
    return IsValid(unit, bot, ignoreLos);
}

void PossibleTargetsValue::FindPossibleTargets(Player* player, std::list<Unit*>& targets, float range)
{
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(player, player, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(player, searcher, range);
}

bool PossibleTargetsValue::IsFriendly(Unit* target, Player* player)
{
    bool friendly = false;
    if (sServerFacade.IsFriendlyTo(target, player))
    {
        friendly = true;

    }

    return friendly;
}

bool PossibleTargetsValue::IsAttackable(Unit* target, Player* player)
{
    return !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_ATTACKABLE_1) &&
           !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNTARGETABLE) &&
           !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE) &&
           !target->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION);
}

bool PossibleTargetsValue::IsValid(Unit* target, Player* player, bool ignoreLos)
{
    // If the target is available
    if (target && target->IsInWorld() && (target->GetMapId() == player->GetMapId()))
    {
        // If the target is dead
        if (sServerFacade.UnitIsDead(target))
        {
            return false;
        }

        // If the target is friendly
        if (IsFriendly(target, player))
        {
            return false;
        }

        // If the target can't be attacked
        if (!IsAttackable(target, player))
        {
            return false;
        }

        // A creature in evade mode is untouchable: the core refuses to start an attack
        // on one and drops every point of damage aimed at it (WorldObject::DealDamageMods),
        // while its health regenerates. One is therefore not a possible target - not for
        // grinding (GrindTargetValue), not for a pull, not for the travel/area scans - and
        // a bot that reaches it walks the whole way for nothing and then stands on it.
        // It is also what clears a stale "attack target" in SelectNewTargetAction: that
        // check is "possible targets no los", so the guid must not survive here.
        if (target->IsCreature() && static_cast<Creature*>(target)->IsInEvadeMode())
        {
            return false;
        }

        // Being in combat with *this* target is a reason to know where it is
        // without seeing it. Being in combat at all is not: player->IsInCombat()
        // used to be part of this, which meant a bot fighting anyone could pick
        // out every stealthed player within range.
        bool isInCombatWithTarget = target->GetVictim() == player ||
                                     target->GetThreatManager().getThreat(player) > 0.0f;

        if (!ignoreLos && !isInCombatWithTarget)
        {
            if (!target->IsVisibleForOrDetect(player, player->GetCamera().GetBody(), true))
            {
                return false;
            }
        }
        if (!CanFreeMoveValue::CanFreeAttack(PlayerbotAIStorage::Instance().GetAI(player), target))
            return false;

        return true;
    }

    return false;
}

void PossibleTriggersValue::FindUnits(std::list<Unit*> &targets)
{
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool PossibleTriggersValue::AcceptUnit(Unit* unit)
{
    if (!unit || !unit->IsInWorld() || !sServerFacade.IsAlive(unit))
        return false;
    // Listed permanent-aura creature (opt-in keep-out), selectable or not.
    if (KeepoutFor(unit))
        return true;
    // Invisible trigger NPCs cannot be selected.
    if (!unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
        return false;
    // Periodic-trigger aura whose triggered spell deals school damage.
    Unit::AuraList const& triggered = unit->GetAurasByType(SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    for (Unit::AuraList::const_iterator it = triggered.begin(); it != triggered.end(); ++it)
    {
        Aura* aura = *it;
        if (!aura)
            continue;
        SpellEntry const* spellProto = aura->GetSpellProto();
        if (!spellProto)
            continue;
        uint32 triggerSpell = spellProto->EffectTriggerSpell[aura->GetEffIndex()];
        SpellEntry const* triggerProto = sServerFacade.LookupSpellInfo(triggerSpell);
        if (!triggerProto)
            continue;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (triggerProto->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                return true;
    }
    return false;
}

CreatureAuraKeepout const* PossibleTriggersValue::KeepoutFor(Unit* unit)
{
    if (!sPlayerbotAIConfig.avoidCreatureAuras || !unit || !unit->IsCreature())
        return nullptr;
    CreatureAuraKeepout const* row = FindCreatureAuraKeepout(unit->GetMapId(), unit->GetEntry());
    if (!row)
        return nullptr;
    // The explicit-order exemption is checked by the callers (per bot).
    if (!CreatureAuraKeepoutApplies(true, row, sServerFacade.IsAlive(unit), unit->HasAura(row->auraSpellId), false))
        return nullptr;
    return row;
}

Unit* PossibleTriggersValue::KeepoutRingAt(PlayerbotAI* ai, float x, float y, Unit* extra)
{
    if (!sPlayerbotAIConfig.avoidCreatureAuras || !ai)
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    ObjectGuid const explicitTarget = context->GetValue<ObjectGuid>("explicit attack target")->Get();
    auto ringed = [&](Unit* unit)
    {
        CreatureAuraKeepout const* row = KeepoutFor(unit);
        if (!CreatureAuraKeepoutApplies(true, row, true, true, unit && unit->GetObjectGuid() == explicitTarget))
            return false;
        float const dx = unit->GetPositionX() - x, dy = unit->GetPositionY() - y;
        return CreatureAuraEndPointBlocked(*row, std::sqrt(dx * dx + dy * dy));
    };
    if (extra && ringed(extra))
        return extra;
    for (ObjectGuid const& guid : context->GetValue<std::list<ObjectGuid>>("possible triggers")->Get())
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit != extra && ringed(unit))
            return unit;
    }
    return nullptr;
}

namespace
{
    // Listed keep-out entries of one map within `radius` of a point (2D,
    // centre measure, the measure the ring uses).
    struct ListedKeepoutNearPoint
    {
        uint32 mapId;
        float x, y, radius;
        bool operator()(Unit* unit) const
        {
            if (!unit || !unit->IsCreature() || !FindCreatureAuraKeepout(mapId, unit->GetEntry()))
                return false;
            float const dx = unit->GetPositionX() - x, dy = unit->GetPositionY() - y;
            return dx * dx + dy * dy < radius * radius;
        }
    };

    // Shared by every bot. World thread only: bot AI updates run in the
    // module's world-thread pass (BotManager), never on a map thread.
    CreatureAuraSnapshotCache& KeepoutSnapshotCache()
    {
        static CreatureAuraSnapshotCache cache;
        return cache;
    }
}

std::atomic<uint32_t> PossibleTriggersValue::hazardSnapshotRefreshCount{ 0 };

CreatureAuraHazardSnapshot const* PossibleTriggersValue::HazardSnapshot(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.avoidCreatureAuras || !ai)
        return nullptr;
    Player* bot = ai->GetBot();
    uint32 const mapId = bot->GetMapId();
    float const maxHazard = CreatureAuraMaxHazardRadiusOnMap(mapId);
    if (maxHazard <= 0.0f)
        return nullptr; // no listed creature on this map: never a grid visit
    Map* map = bot->GetMap();
    if (!map)
        return nullptr;
    CreatureAuraSnapshotKey const key =
        CreatureAuraSnapshotKeyFor(mapId, bot->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY());
    return CreatureAuraSnapshotCacheGet(KeepoutSnapshotCache(), key, WorldTimer::getMSTime(),
        [&](std::vector<CreatureAuraHazardEntry>& out, float cx, float cy)
        {
            // The one bounded grid visit per (map, instance, cell) per
            // kCreatureAuraSnapshotMs, shared by every bot standing there.
            hazardSnapshotRefreshCount.fetch_add(1, std::memory_order_relaxed);
            float const radius = CreatureAuraSnapshotRadius(maxHazard);
            std::list<Unit*> nearby;
            ListedKeepoutNearPoint check{ mapId, cx, cy, radius };
            MaNGOS::UnitListSearcher<ListedKeepoutNearPoint> searcher(nearby, check);
            Cell::VisitAllObjects(cx, cy, map, searcher, radius);
            for (Unit* unit : nearby)
                if (CreatureAuraKeepout const* row = KeepoutFor(unit))
                    out.push_back({ unit->GetObjectGuid().GetRawValue(), unit->GetPositionX(), unit->GetPositionY(),
                        CreatureAuraHazardRadius(*row) });
        });
}

bool PossibleTriggersValue::SnapshotRingedAt(PlayerbotAI* ai, float x, float y, Unit* skip)
{
    CreatureAuraHazardSnapshot const* snap = HazardSnapshot(ai);
    if (!snap)
        return false;
    // A point the snapshot does not reach is not called clear by it.
    if (!CreatureAuraSnapshotCovers(*snap, x, y))
        return true;
    uint64 const explicitRaw =
        ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get().GetRawValue();
    uint64 const skipRaw = skip ? skip->GetObjectGuid().GetRawValue() : 0;
    return CreatureAuraSnapshotRingAt(*snap, x, y, skipRaw, explicitRaw) != 0;
}

bool PossibleTriggersValue::EndPointRinged(PlayerbotAI* ai, float x, float y, Unit* extra)
{
    if (!sPlayerbotAIConfig.avoidCreatureAuras || !ai)
        return false;
    // `extra` (the current target) and the cached list, live; then the
    // shared hazard snapshot for rings past the list.
    if (KeepoutRingAt(ai, x, y, extra))
        return true;
    return SnapshotRingedAt(ai, x, y, extra);
}

bool PossibleTriggersValue::GapCloserCouldFire(PlayerbotAI* ai, std::string const& spell, Unit* target)
{
    // Mirrors the refusals of CastSpellAction::isPossible and
    // PlayerbotAI::CanCastSpell (their order and their tests), nothing more.
    Player* bot = ai->GetBot();
    CreatureAuraGapCloserReadiness r;
    uint32 const spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
    SpellEntry const* spellInfo = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
    r.knowsSpell = spellInfo != nullptr;
    if (!r.knowsSpell)
        return CreatureAuraGapCloserCouldFire(r);
    r.lostControl = bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    r.onCooldown = !sServerFacade.IsSpellReady(bot, spellId);
    // CheckSpellPower (PlayerbotAI.cpp): the cost without caster spellmods.
    if (spellInfo->powerType < MAX_POWERS)
        r.powerShort = bot->GetPower(Powers(spellInfo->powerType)) <
            Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr);
    // Spell::CheckCast(strict): SPELL_FAILED_AFFECTING_COMBAT.
    r.nonCombatSpell = spellInfo->IsNonCombatSpell();
    r.inCombat = bot->IsInCombat();
    if (spellInfo->rangeIndex != SPELL_RANGE_IDX_COMBAT && spellInfo->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY &&
        spellInfo->rangeIndex != SPELL_RANGE_IDX_ANYWHERE)
    {
        float maxRange = 0.0f, minRange = 0.0f;
        if (ai->GetSpellRange(spell, &maxRange, &minRange))
        {
            r.rangeKnown = true;
            r.minRange = minRange;
            r.maxRange = maxRange;
            r.distance = bot->GetDistance(target, ai->IsRanged(bot) ? SizeFactor::CombatReach : SizeFactor::CombatReachWithMelee);
        }
    }
    return CreatureAuraGapCloserCouldFire(r);
}

bool PossibleTriggersValue::GapCloserIntoKeepout(PlayerbotAI* ai, std::string const& spell, Unit* target)
{
    if (!sPlayerbotAIConfig.avoidCreatureAuras || !ai || !target)
        return false;
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    bool const isTank = PlayerbotAI::IsTank(bot);
    ObjectGuid const explicitTarget = context->GetValue<ObjectGuid>("explicit attack target")->Get();
    bool const ordered = target->GetObjectGuid() == explicitTarget;
    if (isTank || ordered)
        return false; // CreatureAuraGapCloserVetoed allows these; skip the lookups
    // Asked from isUseful() on every tick the trigger fires: nothing to veto
    // while the cast cannot fire, and isPossible() refuses it then anyway.
    if (!GapCloserCouldFire(ai, spell, target))
        return false;
    if (KeepoutFor(target))
        return CreatureAuraGapCloserVetoed(true, false, isTank, ordered);
    float lx = 0.0f, ly = 0.0f;
    CreatureAuraGapCloserLanding(bot->GetPositionX(), bot->GetPositionY(),
        target->GetPositionX(), target->GetPositionY(), bot->GetCombatReach(target, true, 0.0f), lx, ly);
    // Fast path: the bot's cached 15 yd list, read fresh every time.
    if (KeepoutRingAt(ai, lx, ly))
        return CreatureAuraGapCloserVetoed(false, true, isTank, ordered);
    // Rings past the list: the shared hazard snapshot (no grid visit of its
    // own). The target is checked live above.
    bool const landingInRing = SnapshotRingedAt(ai, lx, ly, target);
    return CreatureAuraGapCloserVetoed(false, landingInRing, isTank, ordered);
}
