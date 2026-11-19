// Slice 6, review fix R6 (items 2, 3, 7): the ONE liquid test of the run glue, so no stand, path, move or fact reads liquid a different way.
//
// What it replaces: the facts' pointLiquid and the stand search compared the liquid STATUS for equality (IN_WATER / UNDER_WATER) at a point 0.5 yd above the
// asked z. ForeverCore ORs LIQUID_MAP_OCEAN_FLOOR into the status near the floor (TerrainMgr.cpp:779-781, GridMap.cpp:686-687), so a bot standing on the floor
// of a lava pool read 0x04|0x10 and passed; and a path laid ON the lava surface reads ABOVE_WATER from 0.5 yd up. The rules now:
//  * status: the BITS, as the core's own TerrainInfo::IsInWater (TerrainMgr.cpp:975): (status & (IN_WATER | UNDER_WATER)) != 0;
//  * level: any liquid present whose level is >= z - kLevelSlackYd refuses (the rule of TerrainIsSwimmableCompat, TbCompat_Unit.h:702-705: in it, under it,
//    walking on it, or within JUMP_HEIGHT above it), at the asked z itself, not above it;
//  * lines: a straight segment (the path's legs, a direct move's line) is sampled every kSegmentSampleYd or less, both ends included, with the same point rule.
// No terrain to ask = refused (do less). The pure part is core-free (tools/test_run_liquid.cpp); the core part is in TbRunLiquid.cpp.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Map;

namespace TortoiseBots
{
namespace run
{
namespace liquid
{
constexpr std::uint32_t kInWaterBit = 0x04;         // ZLiquidStatus: ABOVE_WATER 0x01, WATER_WALK 0x02, IN_WATER 0x04, UNDER_WATER 0x08, OCEAN_FLOOR 0x10
constexpr std::uint32_t kUnderWaterBit = 0x08;      // (common/Collision/Maps/MapDefines.h:135-139)
constexpr float kLevelSlackYd = 0.6f;               // JUMP_HEIGHT of T's IsSwimmable (GridMap.cpp:1057)
constexpr float kSegmentSampleYd = 1.5f;            // the line is sampled at least this often (the brief: every 1-2 yd)

// The core's IsInWater: the bits, whatever else is ORed in (OCEAN_FLOOR, ABOVE_WATER).
inline bool InWaterBits(std::uint32_t status)
{
    return (status & (kInWaterBit | kUnderWaterBit)) != 0;
}

// One point: `status` and `level` as the core's liquid read gave them (status 0 = no liquid: `level` is not read).
inline bool PointRefused(std::uint32_t status, float level, float z)
{
    if (status == 0)
        return false;
    return InWaterBits(status) || level >= z - kLevelSlackYd;
}

// The samples of a line a -> b: ceil(length / kSegmentSampleYd) + 1 points, both ends included. `refused(x, y, z)` is the point rule.
template <class Refused>
inline bool SegmentRefused(float ax, float ay, float az, float bx, float by, float bz, Refused&& refused)
{
    float const dx = bx - ax, dy = by - ay, dz = bz - az;
    float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
    std::size_t const steps = length > kSegmentSampleYd ? static_cast<std::size_t>(std::ceil(length / kSegmentSampleYd)) : 1;
    for (std::size_t i = 0; i <= steps; ++i)
    {
        float const t = static_cast<float>(i) / static_cast<float>(steps);
        if (refused(ax + t * dx, ay + t * dy, az + t * dz))
            return true;
    }
    return false;
}

// A path as points p[0..n): refused when any vertex or any leg between two is. `Points` holds elements with x, y, z.
template <class Points, class Refused>
inline bool PathRefused(Points const& p, std::size_t n, Refused&& refused)
{
    if (n == 1)
        return refused(p[0].x, p[0].y, p[0].z);
    for (std::size_t i = 1; i < n; ++i)
        if (SegmentRefused(p[i - 1].x, p[i - 1].y, p[i - 1].z, p[i].x, p[i].y, p[i].z, refused))
            return true;
    return false;
}

// ---- a MOVE of a bot that may start in liquid (R6 round 2) ----
// A mover that already stands in water or lava must be able to walk out of it, but ONLY out of it: the wet samples of its route are allowed as ONE contiguous
// prefix from the origin; after the first dry sample every wet sample refuses, and a wet destination (the last sample) always refuses. So: wet -> dry is
// accepted; wet -> wet (submerged all the way, or ending in liquid) is refused; wet -> dry -> wet is refused; a dry origin is the same rule with an empty prefix.
// Used at validation (the run executor's judge) and at execution (the move-direct verb): the same samples, the same answer.
struct PrefixState
{
    bool seenDry = false;
    bool lastWet = false;
    bool bad = false;
    bool hasAt = false;                 // the first sample that decided the refusal (a wet sample after a dry one, else the wet last sample): for the refusal text
    float atX = 0.0f, atY = 0.0f, atZ = 0.0f;
    float lastX = 0.0f, lastY = 0.0f, lastZ = 0.0f;
    void Sample(bool wet, float x = 0.0f, float y = 0.0f, float z = 0.0f)
    {
        lastWet = wet;
        lastX = x;
        lastY = y;
        lastZ = z;
        if (!wet)
            seenDry = true;
        else if (seenDry && !bad)
        {
            bad = true;
            hasAt = true;
            atX = x;
            atY = y;
            atZ = z;
        }
    }
    bool Refused() const { return bad || lastWet; }
    // Where it was refused: the first wet-after-dry sample, else the (wet) end of the route.
    void Where(float& x, float& y, float& z) const
    {
        x = hasAt ? atX : lastX;
        y = hasAt ? atY : lastY;
        z = hasAt ? atZ : lastZ;
    }
};

// A straight line a -> b, sampled like SegmentRefused (every <= kSegmentSampleYd, both ends), in order from a.
template <class Refused>
inline void SegmentSamples(PrefixState& state, float ax, float ay, float az, float bx, float by, float bz, Refused&& refused)
{
    float const dx = bx - ax, dy = by - ay, dz = bz - az;
    float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
    std::size_t const steps = length > kSegmentSampleYd ? static_cast<std::size_t>(std::ceil(length / kSegmentSampleYd)) : 1;
    for (std::size_t i = 0; i <= steps; ++i)
    {
        float const t = static_cast<float>(i) / static_cast<float>(steps);
        float const px = ax + t * dx, py = ay + t * dy, pz = az + t * dz;
        state.Sample(refused(px, py, pz), px, py, pz);
    }
}

template <class Refused>
inline bool SegmentPrefixRefused(float ax, float ay, float az, float bx, float by, float bz, Refused&& refused, PrefixState* where = nullptr)
{
    PrefixState state;
    SegmentSamples(state, ax, ay, az, bx, by, bz, refused);
    if (where)
        *where = state;
    return state.Refused();
}

// A path p[0..n) walked in order (vertices and legs).
template <class Points, class Refused>
inline bool PathPrefixRefused(Points const& p, std::size_t n, Refused&& refused, PrefixState* where = nullptr)
{
    PrefixState state;
    if (n == 0)
        return false;
    if (n == 1)
        state.Sample(refused(p[0].x, p[0].y, p[0].z), p[0].x, p[0].y, p[0].z);
    for (std::size_t i = 1; i < n; ++i)
        SegmentSamples(state, p[i - 1].x, p[i - 1].y, p[i - 1].z, p[i].x, p[i].y, p[i].z, refused);
    if (where)
        *where = state;
    return state.Refused();
}

// ---- the core half (TbRunLiquid.cpp, TORTOISEBOTS_SLICE1 only): the same rules over the map's terrain ----
bool PointRefusedOnMap(Map* map, float x, float y, float z);
bool SegmentRefusedOnMap(Map* map, float ax, float ay, float az, float bx, float by, float bz);
// A diagnostic for `.tb where`: the liquid at (x, y, z) itself and the nearest wet floor cells on a grid of `step` yd within `radius` (the floor under each cell asked
// with the run rule): one line each, at most `maxLines`. For live checks near lava; no state, one terrain read per cell.
std::vector<std::string> DescribeAround(Map* map, float x, float y, float z, float radius, float step, std::size_t maxLines);
bool SegmentPrefixRefusedOnMap(Map* map, float ax, float ay, float az, float bx, float by, float bz);   // the move rule above
}
}
}
