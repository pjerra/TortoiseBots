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

inline bool HasNetworkTransportCompat(WorldSession const* session)
{
    return session->HasNetworkTransport();
}

inline void GetRespawnCoordCompat(Creature const* creature, float& x, float& y, float& z, float* ori = nullptr, float* dist = nullptr)
{
    creature->GetRespawnCoord(x, y, z, ori, dist);
}

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
