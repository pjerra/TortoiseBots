// Slice 6, review fix R6: see TbRunLiquid.h. Built with TORTOISEBOTS_SLICE1 only.
#include "host/run/TbRunLiquid.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "Map.h"
#include "compat/TbCompat_Unit.h"

namespace TortoiseBots
{
namespace run
{
namespace liquid
{
bool PointRefusedOnMap(Map* map, float x, float y, float z)
{
    TerrainInfo const* terrain = map ? map->GetTerrain() : nullptr;
    if (!terrain)
        return true;                        // no terrain to ask: refused (do less)
    GridMapLiquidData data;
    GridMapLiquidStatus const status = LiquidStatusCompat(terrain, x, y, z, MAP_ALL_LIQUIDS, &data);
    return PointRefused(static_cast<std::uint32_t>(status), data.level, z);
}

bool SegmentRefusedOnMap(Map* map, float ax, float ay, float az, float bx, float by, float bz)
{
    return SegmentRefused(ax, ay, az, bx, by, bz, [map](float x, float y, float z) { return PointRefusedOnMap(map, x, y, z); });
}

bool SegmentPrefixRefusedOnMap(Map* map, float ax, float ay, float az, float bx, float by, float bz)
{
    return SegmentPrefixRefused(ax, ay, az, bx, by, bz, [map](float x, float y, float z) { return PointRefusedOnMap(map, x, y, z); });
}
}
}
}

#endif
