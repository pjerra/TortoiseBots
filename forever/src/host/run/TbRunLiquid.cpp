// Slice 6, review fix R6: see TbRunLiquid.h. Built with TORTOISEBOTS_SLICE1 only.
#include "host/run/TbRunLiquid.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <cstdio>
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

std::vector<std::string> DescribeAround(Map* map, float x, float y, float z, float radius, float step, std::size_t maxLines)
{
    std::vector<std::string> out;
    TerrainInfo const* terrain = map ? map->GetTerrain() : nullptr;
    if (!terrain)
    {
        out.push_back("liquid: no terrain");
        return out;
    }
    auto const read = [terrain](float px, float py, float pz, GridMapLiquidData& data) { return static_cast<std::uint32_t>(LiquidStatusCompat(terrain, px, py, pz, MAP_ALL_LIQUIDS, &data)); };
    char buffer[200];
    {
        GridMapLiquidData data;
        std::uint32_t const status = read(x, y, z, data);
        std::snprintf(buffer, sizeof buffer, "liquid here: status=0x%02x level=%.1f z=%.1f refused=%s", status, status ? data.level : 0.0f, z, PointRefused(status, data.level, z) ? "yes" : "no");
        out.push_back(buffer);
    }
    struct Cell { float d, x, y, z, level; std::uint32_t status; };
    std::vector<Cell> wet, dry;
    for (float dx = -radius; dx <= radius; dx += step)
        for (float dy = -radius; dy <= radius; dy += step)
        {
            float const px = x + dx, py = y + dy;
            float const floorZ = GetHeightCompat(map, px, py, z + 10.0f, true, 40.0f);
            if (floorZ <= INVALID_HEIGHT)
                continue;
            GridMapLiquidData data;
            std::uint32_t const status = read(px, py, floorZ, data);
            if (PointRefused(status, data.level, floorZ))
                wet.push_back(Cell{ std::sqrt(dx * dx + dy * dy), px, py, floorZ, data.level, status });
            else if (std::fabs(floorZ - z) <= 3.0f)
                dry.push_back(Cell{ std::sqrt(dx * dx + dy * dy), px, py, floorZ, 0.0f, status });      // dry floor on the bot's own level (a way out of the pool)
        }
    std::sort(wet.begin(), wet.end(), [](Cell const& a, Cell const& b) { return a.d < b.d; });
    std::snprintf(buffer, sizeof buffer, "liquid scan: %u wet floor cells of the grid (radius %.0f, step %.0f)", unsigned(wet.size()), radius, step);
    out.push_back(buffer);
    for (std::size_t i = 0; i < wet.size() && i < maxLines; ++i)
    {
        std::snprintf(buffer, sizeof buffer, "liquid wet: %.1f %.1f %.1f status=0x%02x level=%.1f d=%.1f", wet[i].x, wet[i].y, wet[i].z, wet[i].status, wet[i].level, wet[i].d);
        out.push_back(buffer);
    }
    std::sort(dry.begin(), dry.end(), [](Cell const& a, Cell const& b) { return a.d < b.d; });
    for (std::size_t i = 0; i < dry.size() && i < 8; ++i)
    {
        std::snprintf(buffer, sizeof buffer, "liquid dry: %.1f %.1f %.1f d=%.1f", dry[i].x, dry[i].y, dry[i].z, dry[i].d);
        out.push_back(buffer);
    }
    return out;
}

bool SegmentPrefixRefusedOnMap(Map* map, float ax, float ay, float az, float bx, float by, float bz)
{
    return SegmentPrefixRefused(ax, ay, az, bx, by, bz, [map](float x, float y, float z) { return PointRefusedOnMap(map, x, y, z); });
}
}
}
}

#endif
