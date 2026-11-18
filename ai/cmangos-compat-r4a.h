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
