// Turtle side of the both-core helpers of the world-life slice W1 (grind targets, the open-world corpse run, the spirit healer).
// Same names and signatures as forever/src/compat/TbCompat_W1.h; each one is the expression the bot always made (the call is
// moved, not changed), so the Turtle behaviour is untouched. Found through ai/playerbot/TbCompatW1.h (`__has_include`).
#pragma once

#include <ctime>
#include <list>
// The Turtle post-revive relocation (macros at the end of this file) reads the Turtle host manager.
#include "runtime/BotManager.h"

// ===============================================================================================================
// GRIND TARGETS
// ===============================================================================================================

inline bool IsHonorOrXPTargetCompat(Player* bot, Unit* unit)
{
    return bot->IsHonorOrXPTarget(unit);
}

inline void VisitUnfriendlyUnitsCompat(Player* bot, float range, std::list<Unit*>& units)
{
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

inline bool KillLowersReputationCompat(uint32 entry)
{
    ReputationOnKillEntry const* rep = sObjectMgr.GetReputationOnKillEntry(entry);
    return rep && (rep->repvalue1 < 0 || rep->repvalue2 < 0);
}

// Whether the unit offers a service (UNIT_NPC_FLAG_*): a vendor, trainer, quest giver, flight master or innkeeper. The update field the
// old names use; F has Unit::HasNpcFlag over the same flag (forever/src/compat/TbCompat_W1.h).
inline bool UnitHasNpcFlagW1Compat(Unit* unit, uint32 flag)
{
    return unit && unit->HasFlag(UNIT_NPC_FLAGS, flag);
}

// W1: `FreeGrindPolicy.h` is a ForeverCore rule. Here `IsFreeBot` is also true for every pool random bot, and those bots may today fight
// opposing-faction players and other NPCs; applying the rule would change the Turtle target pick. False leaves the scan's original answer.
inline bool FreeBotStartFightRuleCompat()
{
    return false;
}

// #24: the real-player table and the pool-share rule are the Forever host's (the helm/slice build); the Turtle pick keeps its
// original answer, so a mob next to a player is still a target there.
inline bool PoolShareMobsSkipCompat(Player* /*bot*/, Unit* /*unit*/)
{
    return false;
}


// ===============================================================================================================
// GRAVEYARDS AND MAPS
// ===============================================================================================================

inline WorldSafeLocsEntry const* ClosestGraveyardCompat(WorldObject* /*who*/, uint32 mapId, float x, float y, float z, uint32 team)
{
    return sObjectMgr.GetClosestGraveYard(x, y, z, mapId, Team(team));
}

inline uint32 SafeLocMapIdCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->map_id : 0;
}

inline float SafeLocXCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->x : 0.0f;
}

inline float SafeLocYCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->y : 0.0f;
}

inline float SafeLocZCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->z : 0.0f;
}

inline uint32 HomeBindAreaIdCompat(Player* bot)
{
    return bot ? bot->GetHomeBindAreaId() : 0;
}

inline time_t DuelStartTimeCompat(Player* bot)
{
    return bot && bot->m_duel ? bot->m_duel->startTime : 0;
}

inline MapEntry const* MapEntryW1Compat(uint32 mapId)
{
    return sMapStorage.LookupEntry<MapEntry>(mapId);
}

inline bool MapIsDungeonCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry && entry->IsDungeon();
}

inline bool MapInstanceableCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry && entry->Instanceable();
}

inline uint32 MapGhostEntranceMapCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry ? entry->ghostEntranceMap : 0;
}

// ===============================================================================================================
// AREA TRIGGERS
// ===============================================================================================================

inline uint32 AreaTriggerCountCompat()
{
    return sAreaTriggerStore.GetNumRows();
}

inline AreaTriggerEntry const* AreaTriggerAtCompat(uint32 id)
{
    return sAreaTriggerStore.LookupEntry(id);
}

inline AreaTriggerTeleport const* AreaTriggerTeleportCompat(uint32 id)
{
    return sObjectMgr.GetAreaTriggerTeleport(id);
}

inline uint32 AreaTriggerIdCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->id : 0;
}

inline uint32 AreaTriggerMapIdCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->mapid : 0;
}

inline float AreaTriggerRadiusCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->radius : 0.0f;
}

inline void AreaTriggerPosCompat(AreaTriggerEntry const* entry, float& x, float& y, float& z)
{
    x = entry ? entry->x : 0.0f;
    y = entry ? entry->y : 0.0f;
    z = entry ? entry->z : 0.0f;
}

inline uint32 AreaTeleportMapIdCompat(AreaTriggerTeleport const* at)
{
    return at ? at->destination.mapId : 0;
}

inline void AreaTeleportPosCompat(AreaTriggerTeleport const* at, float& x, float& y, float& z)
{
    x = at ? at->destination.x : 0.0f;
    y = at ? at->destination.y : 0.0f;
    z = at ? at->destination.z : 0.0f;
}

inline float AreaTeleportOrientationCompat(AreaTriggerTeleport const* at)
{
    return at ? at->destination.o : 0.0f;
}

// ===============================================================================================================
// DEATH AND REVIVAL
// ===============================================================================================================

inline bool CorpseRunTeleportsEnabledCompat()
{
    return true;
}

inline bool CorpseRunCrossMapEnabledCompat()
{
    return true;
}

inline bool ReclaimCorpseCompat(Player* bot)
{
    WorldPacket packet(CMSG_RECLAIM_CORPSE);
    packet << bot->getObjectGuid();
    bot->GetSession()->HandleReclaimCorpseOpcode(packet);
    return bot->IsAlive();
}

inline bool SpiritHealerCoreHandlerCompat()
{
    return false;
}

inline bool UnitIsSpiritHealerCompat(Unit* unit)
{
    return unit && unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER);
}

inline bool SpiritHealerInReachCompat(Player* bot, Unit* healer)
{
    return bot && healer && bot->GetDistance(healer) <= INTERACTION_DISTANCE;
}

// The direct revive is the Turtle path: the action keeps its own block (this helper is never reached there).
inline bool ActivateSpiritHealerCompat(Player* /*bot*/, Unit* /*healer*/)
{
    return false;
}

inline bool RepopAtGraveyardCompat(Player* bot)
{
    bot->RepopAtGraveyard();
    return true;
}

inline bool StaleCorpseBonesCompat(Player* /*bot*/)
{
    return false;
}

inline bool StaleCorpsePresentCompat(Player* /*bot*/)
{
    return false;
}

inline bool MotionStackEmptyCompat(MotionMaster* motion)
{
    return !motion || motion->empty();
}

inline void HeartBeatCompat(Player* bot)
{
    bot->SendHeartBeat();
}

inline void NoteReviveKindCompat(Player* /*bot*/, int /*kind*/)
{
}

// The original calls. The Turtle host manager is included at the top of this file, so the expansion has its type.
#define RelocateHopelessBotCompat(bot) TortoiseBots::BotManager::Instance().RelocateHopelessBot(bot)
#define SendStrandedLowbieHomeCompat(bot) TortoiseBots::BotManager::Instance().SendStrandedLowbieHome(bot)
