// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R3 (forever/src/compat/TbCompat_Unit.h).
// Same names and signatures; each forwards to the call the bot always made, so nothing changes on this core.
// Included at the end of ai/cmangos-compat-shim.h.
#pragma once

inline bool IsWithinLOSInMapCompat(WorldObject const* from, WorldObject const* to, bool checkDynLos = true)
{
    return from->IsWithinLOSInMap(to, checkDynLos);
}

inline bool IsInLineOfSightCompat(Map const* map, float x1, float y1, float z1, float x2, float y2, float z2, bool checkDynLos = true)
{
    return map->isInLineOfSight(x1, y1, z1, x2, y2, z2, checkDynLos);
}

inline float GetHeightCompat(Map* map, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH)
{
    return map->GetHeight(x, y, z, vmap, maxSearchDist);
}

inline float GetAngleCompat(WorldObject const* from, WorldObject const* to)
{
    return from->GetAngle(to);
}

inline bool HasNetworkTransportCompat(WorldSession* session)
{
    return session->HasNetworkTransport();
}

inline void GetRespawnCoordCompat(Creature const* creature, float& x, float& y, float& z, float* ori = nullptr, float* dist = nullptr)
{
    creature->GetRespawnCoord(x, y, z, ori, dist);
}

inline uint32 GetFactionTemplateIdCompat(Unit const* unit) { return unit ? unit->GetFactionTemplateId() : 0; }

inline float GetCombatReachCompat(Unit const* self, Unit const* victim, bool ability, float flatMod)
{
    return self->GetCombatReach(victim, ability, flatMod);
}

inline bool HasInArcCompat(WorldObject const* self, WorldObject const* target, float arcAngle)
{
    return self->HasInArc(target, arcAngle);
}

// SizeFactor is the core's own enum here.
inline float GetDistanceCompat(WorldObject const* self, WorldObject const* obj, SizeFactor distcalc)
{
    return self->GetDistance(obj, distcalc);
}

// The block ReachTargetActions.h held before the port, moved here word for word.
inline bool MeleeSpellReachCompat(Player* bot, SpellEntry const* spell, Unit* target)
{
    float modifier = 1.0f;
    if (Player* owner = bot->GetSpellModOwner())
    {
        float base = ATTACK_DISTANCE;
        modifier += owner->ApplySpellMod(spell->Id, SPELLMOD_RANGE, base);
    }
    modifier += bot->GetTotalAuraRangeModifier(SPELL_AURA_MOD_ATTACK_AND_SPELL_RANGE) /
        1000.0f / ATTACK_DISTANCE;
    return bot->CanReachWithMeleeSpellAttack(target, modifier);
}

inline Item* GetWeaponForAttackCompat(Player const* player, WeaponAttackType attackType, bool nonBroken, bool useable)
{
    return player->GetWeaponForAttack(attackType, nonBroken, useable);
}

inline bool CreatureHasWeaponCompat(Creature const* creature)
{
    return creature->HasWeapon();
}

// WorldPosition.h makes a location out of a core record: same facts as the code that was there, now one helper per record.
inline WorldLocation LocationOfCompat(uint32 mapId, Position const& pos) { return WorldLocation(mapId, pos.x, pos.y, pos.z, pos.o); }
inline WorldLocation LocationOfCompat(CreatureData const& data) { return data.position; }
inline WorldLocation LocationOfCompat(GameObjectData const& data) { return data.position; }
inline WorldLocation LocationOfCompat(SpellTargetPosition const* pos) { return WorldLocation(pos->mapId, pos->x, pos->y, pos->z); }
inline WorldLocation LocationOfCompat(TaxiNodesEntry const* pos) { return WorldLocation(pos->map_id, pos->x, pos->y, pos->z); }
inline WorldLocation LocationOfCompat(WorldSafeLocsEntry const* pos) { return WorldLocation(pos->map_id, pos->x, pos->y, pos->z, 0.0f); }
inline WorldLocation LocationOfCompat(PlayerInfo const* pos) { return WorldLocation(pos->mapId, pos->positionX, pos->positionY, pos->positionZ, pos->orientation); }

// Maps and terrain, as WorldPosition.h reads them: the calls the header always made.
inline TerrainInfo const* TerrainOfCompat(Map const* map, uint32 mapId)
{
    return map ? map->GetTerrain() : sTerrainMgr.LoadTerrain(mapId);
}
inline bool TerrainIsInWaterCompat(TerrainInfo const* terrain, float x, float y, float z) { return terrain->IsInWater(x, y, z); }
inline bool TerrainIsUnderWaterCompat(TerrainInfo const* terrain, float x, float y, float z) { return terrain->IsUnderWater(x, y, z); }
inline float TerrainWaterLevelCompat(TerrainInfo const* terrain, float x, float y, float z, float* ground = nullptr)
{
    return terrain->GetWaterLevel(x, y, z, ground);
}
inline uint32 GetAreaIdCompat(WorldObject const* wo)
{
    return sTerrainMgr.GetAreaId(wo->GetMapId(), wo->GetPositionX(), wo->GetPositionY(), wo->GetPositionZ());
}
inline MapEntry const* MapEntryCompat(uint32 mapId) { return sMapStorage.LookupEntry<MapEntry>(mapId); }
inline uint32 FirstInstanceIdCompat(uint32 mapId)
{
    for (auto& map : sMapMgr.Maps())
    {
        if (map.second->GetId() == mapId)
            return map.second->GetInstanceId();
    }
    return 0;
}
inline float VisibilityDistanceCompat(Map const* map, bool overworld)
{
    return map ? map->GetVisibilityDistance() : (overworld ? World::GetMaxVisibleDistanceOnContinents() : World::GetMaxVisibleDistanceInInstances());
}
inline bool AreaHasFlagsCompat(AreaTableEntry const* area, uint32 mask) { return area && (area->Flags & mask); }
inline bool GetLosHitPositionCompat(Map* map, float srcX, float srcY, float srcZ, float& destX, float& destY, float& destZ, float modifyDist)
{
    return map->GetLosHitPosition(srcX, srcY, srcZ, destX, destY, destZ, modifyDist);
}
inline uint16 AreaKeyAtCompat(uint32 mapId, float x, float y, float z) { return sTerrainMgr.GetAreaFlag(mapId, x, y, z); }

inline FactionTemplateEntry const* GetFactionTemplateEntryCompat(uint32 id) { return sObjectMgr.GetFactionTemplateEntry(id); }
inline std::string AreaNameCompat(AreaTableEntry const* area) { return area->Name; }
inline int32 AreaLevelCompat(AreaTableEntry const* area) { return area->AreaLevel; }
inline WorldObject* GetWorldObjectCompat(Map* map, ObjectGuid const& guid) { return map->GetWorldObject(guid); }

inline uint32 CreatureEntryOfCompat(CreatureData const& data) { return data.creature_id[0]; }
inline std::string const& CreatureNameCompat(CreatureInfo const* tpl) { return tpl->name; }
template <class Check>
struct UnitListSearcherCompat : MaNGOS::UnitListSearcher<Check>
{
    UnitListSearcherCompat(WorldObject const* /*searcher*/, std::list<Unit*>& objects, Check& check)
        : MaNGOS::UnitListSearcher<Check>(objects, check) { }
};

inline char const* CStrCompat(char const* text) { return text; }
inline uint32 PlayerXPCompat(Player const* player) { return player->GetUInt32Value(PLAYER_XP); }
inline uint32 PlayerLifetimeHonorableKillsCompat(Player const* player) { return player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS); }
inline float UnitMinDamageCompat(Unit const* unit) { return unit->GetFloatValue(UNIT_FIELD_MINDAMAGE); }
inline float UnitMaxDamageCompat(Unit const* unit) { return unit->GetFloatValue(UNIT_FIELD_MAXDAMAGE); }
inline uint32 WorldIntConfigCompat(eConfigUInt32Values index) { return sWorld.getConfig(index); }
inline uint32 GameObjectTrapSpellIdCompat(GameObjectInfo const* info) { return info->trap.spellId; }

// R4M (slice 1, integration): the helpers two or more of the packages V, A, P, C had defined (one name, one definition each; the table is
// forever/docs/SLICE1-R4M-NOTES.md section 1). Same names and signatures as the R4M section of forever/src/compat/TbCompat_Unit.h.
#include "Maps/PathFinder.h"
#include "Maps/GridMap.h"
#include "Threat/ThreatManager.h"

// Players, guids, groups.
inline Player* GetPlayerCompat(ObjectGuid const& guid) { return sObjectMgr.GetPlayer(guid); }
inline uint64 GuidKeyCompat(ObjectGuid const& guid) { return guid.GetRawValue(); }
inline std::string GuidTextCompat(ObjectGuid const& guid)
{
    std::ostringstream out;
    out << guid;
    return out.str();
}
inline uint32 AccountIdOfCharacterCompat(ObjectGuid const& guid) { return sObjectMgr.GetPlayerAccountIdByGUID(guid); }
inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->m_duel ? player->m_duel->opponent : ObjectGuid();
}
inline ObjectGuid GroupMainTankGuidCompat(Group const* group) { return group->GetMainTankGuid(); }
inline ObjectGuid GroupMainAssistGuidCompat(Group const* group) { return group->GetMainAssistantGuid(); }
inline bool HasPlayerFlagCompat(WorldObject const* obj, uint32 flag) { return obj->HasFlag(PLAYER_FLAGS, flag); }
inline bool HasUnitFlagCompat(Unit const* unit, uint32 flag) { return unit->HasFlag(UNIT_FIELD_FLAGS, flag); }
inline uint32 ItemDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_DURABILITY); }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY); }
// Auras.
struct AuraOfTypeCompat
{
    SpellEntry const* spell;
    uint32 effIndex;
    Unit* caster;
};
template <class Fn>
inline bool ForEachAuraOfTypeCompat(Unit const* unit, AuraType type, Fn&& fn)
{
    for (Aura const* aura : unit->GetAurasByType(type))
    {
        if (!aura)
            continue;
        AuraOfTypeCompat const view{ GetSpellEntryCompat(aura->GetSpellProto()), uint32(aura->GetEffIndex()), aura->GetCaster() };
        if (fn(view))
            return true;
    }
    return false;
}
inline bool AuraIsPositiveCompat(Aura const* aura) { return aura->IsPositive(); }
// Threat and hostility.
inline Unit* CurrentThreatVictimCompat(Unit* unit)
{
    HostileReference* ref = unit->GetThreatManager().getCurrentVictim();
    return ref ? ref->getTarget() : nullptr;
}
inline uint32 CreatureRankCompat(Creature const* creature) { return creature->GetCreatureInfo()->rank; }
inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit const* caster) { return Spell::CalculatePowerCost(spell, const_cast<Unit*>(caster), nullptr, nullptr); }
// Spell objects.
inline int32 SpellCalculateDamageCompat(Spell* spell, uint32 effect, Unit* target)
{
    return spell->CalculateDamage(SpellEffectIndex(effect), target);
}
inline void ApplySpellModRangeCompat(Player* player, uint32 spellId, float& range)
{
    player->ApplySpellMod(spellId, SPELLMOD_RANGE, range);
}
// Geometry.
inline bool IsWithinLOSCompat(WorldObject const* from, float x, float y, float z, bool checkDynLos = true)
{
    return from->IsWithinLOS(x, y, z, checkDynLos);
}
// Small shape differences.
inline bool IsUnderwaterCompat(Unit const* unit) { return unit->IsUnderwater(); }
// The core's own "is the unit rooted by an aura" (R9): Unit::IsInRoots() here, Unit::HasRootAura() on ForeverCore (forever/src/compat/TbCompat_Unit.h).
inline bool RootedCompat(Unit const* unit) { return unit->IsInRoots(); }
inline uint32 AreaZoneIdCompat(AreaTableEntry const* area) { return area->ZoneId; }
inline std::string AreaNameOrEmptyCompat(AreaTableEntry const* area) { return area && area->Name ? std::string(area->Name) : std::string(); }
// Paths.
inline bool PathCalculateCompat(PathFinder& path, float x, float y, float z, bool forceDest = false)
{
    return path.calculate(x, y, z, forceDest);
}
inline bool PathCalculateCompat(PathFinder& path, G3D::Vector3 const& start, G3D::Vector3 const& dest, bool forceDest = false)
{
    return path.calculate(start, dest, forceDest);
}
inline Movement::PointsArray const& PathPointsCompat(PathFinder const& path) { return path.getPath(); }
inline uint32 PathTypeCompat(PathFinder const& path) { return static_cast<uint32>(path.getPathType()); }
inline G3D::Vector3 PathActualEndCompat(PathFinder const& path) { return path.getActualEndPosition(); }
inline float PathLengthCompat(PathFinder const& path) { return path.Length(); }
inline bool PathIsCompleteCompat(PathFinder const& path) { return path.getPathType() == PATHFIND_NORMAL; }
// Liquid and terrain reads.
inline GridMapLiquidStatus LiquidStatusCompat(TerrainInfo const* terrain, float x, float y, float z, uint32 reqLiquidType, GridMapLiquidData* data = nullptr)
{
    return terrain->getLiquidStatus(x, y, z, reqLiquidType, data);
}
inline bool TerrainIsSwimmableCompat(TerrainInfo const* terrain, float x, float y, float z, float radius = 1.5f, GridMapLiquidData* data = nullptr)
{
    return terrain->IsSwimmable(x, y, z, radius, data);
}
inline float TerrainHeightStaticCompat(TerrainInfo const* terrain, float x, float y, float z)
{
    return terrain->GetHeightStatic(x, y, z);
}
inline float TerrainWaterOrGroundLevelCompat(TerrainInfo const* terrain, float x, float y, float z, float* ground, bool swim)
{
    return terrain->GetWaterOrGroundLevel(x, y, z, ground, swim);
}
inline bool SpellIsChannelingCompat(Spell const* spell)
{
    // T has no separate channeling state: SPELL_STATE_CHANNELING is SPELL_STATE_CASTING in the shim (the call the bot always made).
    return spell->getState() == SPELL_STATE_CASTING || spell->getState() == SPELL_STATE_CHANNELING;
}

inline void CombatStartPositionCompat(Creature const* creature, float& x, float& y, float& z) { creature->GetCombatStartPosition(x, y, z); }
inline time_t CombatTimeCompat(Creature const* creature) { return creature->GetCombatTime(); }

inline bool AuraHasMechanicMaskCompat(SpellAuraHolder const* holder, uint32 legacyMask) { return holder->HasMechanicMask(legacyMask); }
