// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R4A (actions, triggers, generic strategies, warrior).
// Same names and signatures as forever/src/compat/TbCompat_A.h; each forwards to the call the bot always made.
// Owned by package R4A: no other package edits this file.
#pragma once

// Guids: the raw 64-bit value.
inline uint64 GuidRawCompat(ObjectGuid const& guid)
{
    return guid.GetRawValue();
}

// Geometry.
inline bool IsWithinLOSCompat(WorldObject const* from, float x, float y, float z, bool checkDynLos = true)
{
    return from->IsWithinLOS(x, y, z, checkDynLos);
}

inline float ObjectBoundingRadiusCompat(WorldObject const* obj)
{
    return obj->GetObjectBoundingRadius();
}

inline float MeleeReachCompat(Unit const* unit)
{
    return unit->GetMeleeReach();
}

// Movement flags.
inline bool HasMovementFlagCompat(Unit const* unit, uint64 flag)
{
    return unit->HasMovementFlag(static_cast<MovementFlags>(flag));
}

// Movement (the mapping table is in forever/src/compat/TbCompat_A.h section 5).
inline void MovePointCompat(MotionMaster& mm, uint32 id, float x, float y, float z, uint32 options)
{
    mm.MovePoint(id, x, y, z, options);
}

inline void MoveFollowCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    mm.MoveFollow(target, dist, angle);
}

inline void MoveChaseCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    mm.MoveChase(target, dist, angle);
}

inline void MotionClearCompat(MotionMaster& mm, bool reset, bool all)
{
    mm.Clear(reset, all);
}

// The raw spline is the core's own class.
using MoveSplineInitCompat = Movement::MoveSplineInit;

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

// Other engine calls.
inline Creature* SummonCreatureCompat(WorldObject* owner, uint32 entry, float x, float y, float z, float o, TempSummonType type, uint32 despawnMs)
{
    return owner->SummonCreature(entry, x, y, z, o, type, despawnMs);
}

inline bool TeleportToCompat(Player* player, uint32 mapId, float x, float y, float z, float o, uint32 options)
{
    return player->TeleportTo(mapId, x, y, z, o, options);
}

inline float ListenRangeYellCompat() { return sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_YELL); }

inline void HandleEmoteStateCompat(Unit* unit, uint32 emote)
{
    unit->HandleEmoteState(emote);
}

// Small shape differences (see forever/src/compat/TbCompat_A.h section 9).
inline bool IsUnderwaterCompat(Unit const* unit) { return unit->IsUnderwater(); }

inline void GetNearPointCompat(WorldObject const* target, WorldObject const* searcher, float& x, float& y, float& z,
    float searcherBoundingRadius, float distance2d, float absAngle)
{
    target->GetNearPoint(searcher, x, y, z, searcherBoundingRadius, distance2d, absAngle);
}

inline bool HasPlayerFlagCompat(WorldObject const* obj, uint32 flag) { return obj->HasFlag(PLAYER_FLAGS, flag); }
inline bool HasUnitFlagCompat(Unit const* unit, uint32 flag) { return unit->HasFlag(UNIT_FIELD_FLAGS, flag); }

inline bool LineHitsCollisionModelCompat(Map* map, float x1, float y1, float z1, float x2, float y2, float z2)
{
    return map->FindCollisionModel(x1, y1, z1, x2, y2, z2) != nullptr;
}

inline bool MapIsBattlegroundCompat(Map const* map) { return map->IsBattleGround(); }
inline Unit* PlayerMoverCompat(Player* player) { return player->GetMover(); }
inline void UpdateSpeedCompat(Unit* unit, UnitMoveType type, bool forced) { unit->UpdateSpeed(type, forced); }

inline Unit* ThreatCurrentVictimCompat(ThreatManager& threat)
{
    HostileReference* ref = threat.getCurrentVictim();
    return ref ? ref->getTarget() : nullptr;
}

inline void ApplySpellModRangeCompat(Player* player, uint32 spellId, float& range)
{
    player->ApplySpellMod(spellId, SPELLMOD_RANGE, range);
}

inline uint32 SpellIconIdCompat(SpellEntry const* entry) { return entry->SpellIconID; }

inline bool IsBehindTargetCompat(Unit const* self, Unit const* target, bool strict) { return self->IsBehindTarget(target, strict); }

// The last value of the spell state enum (T Spell.h:206-214 ends with SPELL_STATE_DELAYED).
constexpr SpellState SpellStateLastCompat = SPELL_STATE_DELAYED;

// Groups (see forever/src/compat/TbCompat_A.h section 10).
inline GroupReference* GroupFirstMemberCompat(Group* group) { return group->GetFirstMember(); }
inline GroupReference* GroupNextMemberCompat(Group* /*group*/, GroupReference* ref) { return ref->next(); }
inline ObjectGuid GroupMainTankGuidCompat(Group const* group) { return group->GetMainTankGuid(); }

// Spell objects (see forever/src/compat/TbCompat_A.h section 11).
inline int32 SpellCalculateDamageCompat(Spell* spell, uint32 effect, Unit* target)
{
    return spell->CalculateDamage(SpellEffectIndex(effect), target);
}
inline uint32 SpellPowerCostCompat(Spell const* spell) { return spell->GetPowerCost(); }

// Pet commands (see forever/src/compat/TbCompat_A.h section 12): the packet the bot always built.
inline void HandlePetActionCompat(WorldSession* session, ObjectGuid const& petGuid, uint32 action, ObjectGuid const& targetGuid)
{
    WorldPacket packet(CMSG_PET_ACTION);
    packet << petGuid;
    packet << action;
    packet << targetGuid;
    session->HandlePetAction(packet);
}

inline void HandlePetAbandonCompat(WorldSession* session, ObjectGuid const& petGuid)
{
    WorldPacket packet(CMSG_PET_ABANDON);
    packet << petGuid;
    session->HandlePetAbandon(packet);
}

inline ObjectGuid GroupScopeGuidCompat(Group const* group, uint64 tag) { return ObjectGuid(tag | uint64(group->GetId())); }
