// Out-of-line parts of the R4C compat pair (TbCompat_C.h). Compiled with the module, WITHOUT the cmangos shim in front: this
// file only sees the core's own headers, so the core's logging macros and the movement generator headers are usable here.
#include "ChaseMovementGenerator.h"
#include "FollowMovementGenerator.h"
#include "Log.h"
#include "MMapManager.h"
#include "MapDefines.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "PhasingHandler.h"
#include "TerrainMgr.h"
#include "Unit.h"
#include "VMapFactory.h"
#include "VMapManager.h"
#include "World.h"

#include <atomic>

// TbCompat_C.h: GuidFromRawCompat. Only 0 (no guid) has a 128-bit form; a legacy 64-bit number is refused once, loudly.
ObjectGuid GuidFromRawCompat(uint64 raw)
{
    if (raw)
    {
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true))
            TC_LOG_ERROR("server.loading", "TortoiseBots: a legacy 64-bit guid ({}) has no ForeverCore form, treated as no guid", raw);
    }
    return ObjectGuid::Empty;
}

// TbCompat_C.h: TargetedMovementTargetCompat. The unit a chase or follow generator is going after.
Unit* TargetedMovementTargetCompat(Unit* unit)
{
    if (!unit || !unit->GetMotionMaster())
        return nullptr;

    MovementGenerator const* generator = unit->GetMotionMaster()->GetCurrentMovementGenerator();
    if (!generator)
        return nullptr;

    switch (generator->GetMovementGeneratorType())
    {
        case CHASE_MOTION_TYPE:
            return static_cast<ChaseMovementGenerator const*>(generator)->GetTarget();
        case FOLLOW_MOTION_TYPE:
            return static_cast<FollowMovementGenerator const*>(generator)->GetTarget();
        default:
            return nullptr;
    }
}

// TbCompat_C.h: the vmap and navmesh calls.
bool VmapLineOfSightCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->isInLineOfSight(mapId, x1, y1, z1, x2, y2, z2, VMAP::ModelIgnoreFlags::Nothing);
}

bool VmapObjectHitPosCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float& ry, float& rz, float modifyDist)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapId, x1, y1, z1, x2, y2, z2, rx, ry, rz, modifyDist);
}

bool VmapLoadTileCompat(char const* dataPath, uint32 mapId, int x, int y)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->loadMap(dataPath, mapId, uint32(x), uint32(y)) == VMAP::LoadResult::Success;
}

bool NavMeshLoadedCompat(uint32 mapId, uint32 instanceId)
{
    return MMAP::MMapManager::instance()->GetNavMesh(mapId, instanceId) != nullptr;
}

bool NavMeshLoadTileCompat(uint32 mapId, uint32 instanceId, int x, int y)
{
    MMAP::LoadResult const result = MMAP::MMapManager::instance()->loadMap(sWorld->GetDataPath(), mapId, instanceId, x, y);
    return result == MMAP::LoadResult::Success || result == MMAP::LoadResult::AlreadyLoaded;
}
