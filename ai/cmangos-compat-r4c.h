// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R4C (engine core, config, log, positions).
// Same names and signatures as forever/src/compat/TbCompat_C.h; each forwards to the call the bot always made.
// Owned by package R4C: no other package edits this file.
#pragma once

#include "LFG/LFGMgr.h"
#include "Maps/MoveMap.h"
#include "Maps/PathFinder.h"
#include "vmap/VMapFactory.h"
#include "Movement/TargetedMovementGenerator.h"

// Logging: the core's logger, as `sLog` is on this core (shared/Log.h:314).
inline Log& LegacyLogCompat() { return Log::Instance(); }

// Names as C strings, with the fallback the old code printed for a null name.
inline char const* CStrOrCompat(char const* text, char const* fallback) { return text ? text : fallback; }
inline char const* CStrOrCompat(std::string const& text, char const* /*fallback*/) { return text.c_str(); }

// Guids: one 64-bit number here, so every helper is the call the bot always made.
inline uint64 GuidKeyCompat(ObjectGuid const& guid) { return guid.GetRawValue(); }
inline ObjectGuid GuidFromRawCompat(uint64 raw) { return ObjectGuid(raw); }
inline ObjectGuid GuidFromNumbersCompat(HighGuid hi, uint32 entry, uint32 counter) { return ObjectGuid(hi, entry, counter); }
inline void GuidWriteCompat(std::ostream& out, ObjectGuid const& guid) { out << guid.GetRawValue(); }
inline ObjectGuid GuidReadCompat(std::istream& in)
{
    uint64 g = 0;
    in >> g;
    return ObjectGuid(g);
}

// Settings and constants.
inline bool WorldBoolConfigCompat(eConfigBoolValues index) { return sWorld.getConfig(index); }
inline uint32 MaxLevelCompat() { return DEFAULT_MAX_LEVEL; }

// Players, units.
inline uint32 ZoneIdCompat(WorldObject const* wo) { return wo->GetZoneId(); }
inline uint32 AccountIdOfCharacterCompat(ObjectGuid const& guid) { return sObjectMgr.GetPlayerAccountIdByGUID(guid); }
inline bool InBattleGroundQueueCompat(Player const* player) { return player->InBattleGroundQueue(); }
inline bool IsPlayerInLfgQueueCompat(ObjectGuid const& guid) { return sLFGMgr.IsPlayerInQueue(guid); }
inline bool HasSpellCategoryCooldownCompat(Player const* player, uint32 category) { return player->HasSpellCategoryCooldown(category); }
inline void SetOrientationAndSendCompat(Unit* unit, float angle)
{
    unit->SetOrientation(angle);
    unit->SendHeartBeat();
}
inline bool IsUnderwaterCompat(Unit const* unit) { return unit->IsUnderwater(); }

// The unit a chase or follow movement generator is going after: the block that was in ServerFacade.cpp, word for word, with
// the dispatch on the mover's type that GetChaseTarget did around it.
namespace tbcompat_r4c
{
    template<class T>
    inline Unit* TargetedMovementTargetOf(T* unit)
    {
        if (!unit || !unit->GetMotionMaster())
            return nullptr;

        MovementGenerator const* generator = unit->GetMotionMaster()->GetCurrent();
        if (!generator)
            return nullptr;

        switch (generator->GetMovementGeneratorType())
        {
            case CHASE_MOTION_TYPE:
                return static_cast<ChaseMovementGenerator<T> const*>(generator)->GetTarget();
            case FOLLOW_MOTION_TYPE:
                return static_cast<FollowMovementGenerator<T> const*>(generator)->GetTarget();
            default:
                return nullptr;
        }
    }
}
inline Unit* TargetedMovementTargetCompat(Unit* target)
{
    if (!target)
        return nullptr;

    if (target->GetTypeId() == TYPEID_PLAYER)
        return tbcompat_r4c::TargetedMovementTargetOf(static_cast<Player*>(target));
    if (target->GetTypeId() == TYPEID_UNIT)
        return tbcompat_r4c::TargetedMovementTargetOf(static_cast<Creature*>(target));
    return nullptr;
}

// Auras, for the action log's dump: the holder map by spell id and the visible slots as unit fields.
inline std::vector<uint32> AuraSpellIdsCompat(Unit const* unit)
{
    std::vector<uint32> ids;
    for (auto const& it : unit->GetSpellAuraHolderMap())
        ids.push_back(it.first);
    return ids;
}
inline std::vector<std::pair<uint32, uint32>> VisibleAuraSlotsCompat(Unit const* unit)
{
    std::vector<std::pair<uint32, uint32>> slots;
    for (uint32 slot = 0; slot < MAX_AURAS; ++slot)
        if (uint32 sid = unit->GetUInt32Value(UNIT_FIELD_AURA + slot))
            slots.emplace_back(slot, sid);
    return slots;
}

// Item classes: the count the AhBot tables are sized with (see forever/src/compat/TbCompat_C.h).
inline constexpr uint32 LegacyItemClassCountCompat = MAX_ITEM_CLASS;

// Factions and reputation.
inline uint32 FactionOfTemplateCompat(FactionTemplateEntry const* faction) { return faction->faction; }
inline FactionEntry const* FactionEntryOfCompat(uint32 id) { return sObjectMgr.GetFactionEntry(id); }
inline std::string FactionNameCompat(FactionEntry const* faction, int locale) { return faction->name[locale]; }

struct ReputationStateCompat
{
    bool known = false;
    bool visible = false;
    bool atWar = false;
};
inline ReputationStateCompat ReputationStateOfCompat(Player const* player, FactionEntry const* faction)
{
    ReputationStateCompat state;
    if (FactionState const* entry = player->GetReputationMgr().GetState(faction))
    {
        state.known = true;
        state.visible = (entry->Flags & FACTION_FLAG_VISIBLE) != 0;
        state.atWar = (entry->Flags & FACTION_FLAG_AT_WAR) != 0;
    }
    return state;
}
inline ReputationRank ReputationRankCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().GetRank(faction); }
inline int32 ReputationValueCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().GetReputation(faction); }
inline bool ReputationAtWarCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().IsAtWar(faction); }
inline ReputationRank const* ForcedReputationRankCompat(Player const* player, FactionTemplateEntry const* faction)
{
    return player->GetReputationMgr().GetForcedRankIfAny(faction);
}

// Flags of a unit and a player.
inline bool IsPlayerControlledCompat(Unit const* unit) { return unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED); }
inline bool IsContestedPvpCompat(Player const* player) { return player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP); }

inline uint32 GameObjectFactionCompat(GameObjectInfo const* tpl) { return tpl->faction; }
inline void GameTelePointCompat(GameTele const* tele, float& x, float& y, float& z)
{
    x = tele->x;
    y = tele->y;
    z = tele->z;
}

// Maps, creatures.
inline Creature* GetAnyTypeCreatureCompat(Map* map, ObjectGuid const& guid) { return map->GetAnyTypeCreature(guid); }
inline bool MapIsLoadedCompat(Map const* map, float x, float y) { return map->IsLoaded(x, y); }

// Names and texts for chat links (ChatHelper.cpp).
inline char const* RaceNameCompat(ChrRacesEntry const* race, int locale) { return race->name[locale]; }
inline uint32 RaceIdCompat(ChrRacesEntry const* race) { return race->RaceID; }
inline char const* SkillNameCompat(SkillLineEntry const* skill, int locale) { return skill->name[locale]; }
inline uint32 SkillValuePureCompat(Player const* player, uint32 skill) { return player->GetSkillValuePure(skill); }
inline uint32 SkillMaxPureCompat(Player const* player, uint32 skill) { return player->GetSkillMaxPure(skill); }
inline int32 SkillBonusPermanentCompat(Player const* player, uint32 skill) { return player->GetSkillBonusPermanent(skill); }
inline int32 SkillBonusTemporaryCompat(Player const* player, uint32 skill) { return player->GetSkillBonusTemporary(skill); }
inline char const* MangosStringCompat(uint32 id, int locale) { return sObjectMgr.GetMangosString(id, locale); }
inline bool IsNumericCompat(std::string const& text) { return isNumeric(text); }

inline std::string QuestTitleCompat(Quest const* quest, int locale)
{
    std::string title = quest->GetTitle();
    if (locale >= 0)
    {
        if (QuestLocale const* texts = sObjectMgr.GetQuestLocale(quest->GetQuestId()))
            if (static_cast<size_t>(locale) < texts->Title.size() && !texts->Title[locale].empty())
                title = texts->Title[locale];
    }
    return title;
}
inline int32 QuestLevelCompat(Quest const* quest) { return quest->GetQuestLevel(); }

inline std::string ItemLocalizedNameCompat(uint32 itemId, int locale, std::string const& fallback)
{
    std::string name = fallback;
    if (locale >= 0)
    {
        if (ItemLocale const* texts = sObjectMgr.GetItemLocale(itemId))
            if (static_cast<size_t>(locale) < texts->Name.size() && !texts->Name[locale].empty())
                name = texts->Name[locale];
    }
    return name;
}

// ---------------------------------------------------------------------------------------------------------------
// WorldPosition.cpp: areas, spawn data, creature templates, terrain, line of sight, transports, grids, paths.
// ---------------------------------------------------------------------------------------------------------------
// Areas: T's key is the per-map area flag, F's the area id (forever/src/compat/TbCompat_C.h).
inline AreaTableEntry const* AreaEntryOfKeyCompat(uint32 key, uint32 mapId) { return AreaEntry::GetByAreaFlagAndMap(key, mapId); }
inline void ZoneAndAreaIdOfKeyCompat(uint32& zoneId, uint32& areaId, uint16 key, uint32 mapId)
{
    TerrainManager::GetZoneAndAreaIdByAreaFlag(zoneId, areaId, key, mapId);
}
inline uint32 AreaIdOfCompat(AreaTableEntry const* area) { return area->Id; }
inline uint32 AreaZoneIdCompat(AreaTableEntry const* area) { return area->ZoneId; }
inline uint32 AreaTeamCompat(AreaTableEntry const* area) { return area->Team; }
inline std::string AreaNameOrEmptyCompat(AreaTableEntry const* area) { return area->Name ? area->Name : ""; }
inline std::string MapNameOrEmptyCompat(MapEntry const* map) { return std::string(map->name ? map->name : ""); }

// Spawn data.
template <class Worker> inline void DoCreatureDataCompat(Worker& worker) { sObjectMgr.DoCreatureData(worker); }
template <class Worker> inline void DoGameObjectDataCompat(Worker& worker) { sObjectMgr.DoGOData(worker); }
inline GameObject* GetGameObjectBySpawnIdCompat(Map* map, uint32 entry, uint64 spawnId)
{
    // The guid of a spawned object: entry and spawn id, as the old call built it (gopair->second.id, gopair->first).
    return map->GetGameObject(ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32(spawnId)));
}

// Creature templates.
inline bool CreatureIsCivilianCompat(CreatureInfo const* info) { return info->civilian; }
// The numbers the code always used (Creature.h:62 GUARD, :59 INVISIBLE, :69 NO_TARGET).
inline bool CreatureIsGuardFlagCompat(CreatureInfo const* info) { return (info->flags_extra & 0x00000400) != 0; }
inline bool CreatureNeverAggrosCompat(CreatureInfo const* info)
{
    return (info->flags_extra & 0x00000080u) != 0 || (info->flags_extra & 0x00020000u) != 0;
}
inline bool CreatureIsAboveNormalCompat(CreatureInfo const* info) { return info->rank > CREATURE_ELITE_NORMAL; }
inline bool FactionTemplateHostileToCompat(FactionTemplateEntry const* from, FactionTemplateEntry const* to) { return from->IsHostileTo(*to); }

// Randomness.
inline float RandNormCompat() { return rand_norm_f(); }

// Terrain.
inline uint32 LiquidAtCompat(TerrainInfo const* terrain, float x, float y, float z, uint32 request, uint32& typeFlags)
{
    GridMapLiquidData data{};
    uint32 const status = static_cast<uint32>(terrain->getLiquidStatus(x, y, z, static_cast<uint8>(request), &data));
    typeFlags = status != LIQUID_MAP_NO_WATER ? data.type_flags : 0;
    return status;
}
inline float TerrainWaterOrGroundLevelCompat(TerrainInfo const* terrain, float x, float y, float z, float* ground, bool swim)
{
    return terrain->GetWaterOrGroundLevel(x, y, z, ground, swim);
}

// Static line of sight and hit positions in the vmaps; map and navmesh loading.
inline bool VmapLineOfSightCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->isInLineOfSight(mapId, x1, y1, z1, x2, y2, z2);
}
inline bool VmapObjectHitPosCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float& ry, float& rz, float modifyDist)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapId, x1, y1, z1, x2, y2, z2, rx, ry, rz, modifyDist);
}
inline bool VmapLoadTileCompat(char const* dataPath, uint32 mapId, int x, int y)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->loadMap(dataPath, mapId, x, y);
}
inline bool NavMeshLoadedCompat(uint32 mapId, uint32 /*instanceId*/) { return MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapId) != nullptr; }
inline bool NavMeshLoadTileCompat(uint32 mapId, uint32 /*instanceId*/, int x, int y) { return MMAP::MMapFactory::createOrGetMMapManager()->loadMap(mapId, x, y); }

// Grid and cell coordinates.
template <class Pair> inline void CoordIncXCompat(Pair& pair, uint32 value) { pair >> value; }
template <class Pair> inline void CoordIncYCompat(Pair& pair, uint32 value) { pair += value; }

// Transports.
inline GenericTransport* TransportOfCompat(WorldObject const* object) { return object->GetTransport(); }
// (Templates on the transport type: the Transport class is only complete in the files that include Transports/Transport.h.)
template <class TransportType = Transport>
inline std::set<GenericTransport*> TransportsOnMapCompat(uint32 mapId, uint32 entry)
{
    std::set<GenericTransport*> transports;
    typename HashMapHolder<TransportType>::ReadGuard guard(HashMapHolder<TransportType>::GetLock());
    for (auto const& transportEntry : HashMapHolder<TransportType>::GetContainer()) // Boats & zeppelins.
    {
        TransportType* transport = transportEntry.second;
        if (transport && transport->GetMapId() == mapId && (!entry || transport->GetEntry() == entry))
            transports.insert(transport);
    }
    return transports;
}
template <class TransportType>
inline void PassengerPositionCompat(TransportType* transport, float& x, float& y, float& z, float* o = nullptr) { transport->CalculatePassengerPosition(x, y, z, o); }
template <class TransportType>
inline void PassengerOffsetCompat(TransportType* transport, float& x, float& y, float& z, float* o = nullptr) { transport->CalculatePassengerOffset(x, y, z, o); }

// Path generator.
inline bool PathCalculateCompat(PathFinder& path, G3D::Vector3 const& start, G3D::Vector3 const& dest, bool forceDest) { return path.calculate(start, dest, forceDest); }
inline PathType PathTypeOfCompat(PathFinder const& path) { return path.getPathType(); }
inline Movement::PointsArray const& PathPointsOfCompat(PathFinder const& path) { return path.getPath(); }
