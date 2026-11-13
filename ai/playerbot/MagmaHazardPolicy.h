#pragma once

// Magma guard for bot movement (RFC r3-19z20, step 22: the lead tank's idle
// "formation station" walk ended in the trogg-cave lava basin and the party
// followed it in; 5 deaths). The core hurts a player standing in magma with a
// flat 605-610 fire tick (Player::UpdateTerainEnvironmentFlags sets
// ENVIRONMENT_FLAG_IN_MAGMA, the mirror timer calls
// EnvironmentalDamage(DAMAGE_LAVA, EnvironmentalDamage.Min..Max)), which kills
// a level-16 bot on its first or second tick. The navmesh calls a shallow
// basin floor plain ground, so the path finder walks straight in.
//
// This header owns the pure rules, MovementAction::MoveTo2 and the formation
// station trigger own the terrain query, so the rules stay testable in
// tools/test_magma_hazard_policy.cpp. No core includes: the core's liquid
// values are mirrored below and pinned with static_asserts where the core
// headers are in scope (MovementActions.cpp).
//
// Magma only. Slime does no damage in this core (Player.cpp, the DAMAGE_SLIME
// tick is commented out), and refusing it would strand bots in Undercity.
// The swim hand-off is the exception (IsSlimeHazard, ClassifyLiquidHazard
// below): a hand-off route is a swim the route filter (SwimPathFits) already
// refuses through slime, so the hand-off samples every leg for slime exactly
// as for magma instead of taking it.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // GridMapDefines.h: MAP_LIQUID_TYPE_MAGMA (a type_flags bit).
    constexpr std::uint32_t kMagmaLiquidTypeFlag = 0x01;
    // GridMapDefines.h: MAP_LIQUID_TYPE_SLIME (a type_flags bit). Slime counts in the
    // DispatchMovement restore gate (a clamp sunk in slime needs a confirmed floor too)
    // and in the swim hand-off route guard (LiquidHazard); ordinary route refusal stays magma-only.
    constexpr std::uint32_t kSlimeLiquidTypeFlag = 0x04;
    // GridMapDefines.h GridMapLiquidStatus: LIQUID_MAP_WATER_WALK,
    // LIQUID_MAP_IN_WATER, LIQUID_MAP_UNDER_WATER.
    constexpr std::uint32_t kLiquidStatusWaterWalk = 0x02;
    constexpr std::uint32_t kLiquidStatusInWater = 0x04;
    constexpr std::uint32_t kLiquidStatusUnderWater = 0x08;

    // Spacing (yards) of the samples taken between two path points: the
    // magma lip in Ragefire Chasm is a few yards wide, a path corner can sit
    // either side of it.
    constexpr float kMagmaSampleStepYd = 2.0f;

    // At most this many samples between path points per route check; past it
    // only the path points themselves are looked at (the nearest 2 * 48 =
    // 96 yd of the route keep the 2 yd spacing). Worst case per MoveTo2: 1
    // lookup for the end point, 1 for the bot's own spot, 48 between points
    // and 1 per path point (ClipPath has already ended the route within
    // ReactDistance of its start); each lookup is one getLiquidStatus (a vmap
    // liquid query plus a static height query).
    constexpr std::size_t kMagmaMaxInteriorSamples = 48;

    // How long (seconds) a refused magma destination stays refused for a bot
    // without a new look, and the grid (yards) its point is rounded to.
    constexpr std::int64_t kMagmaRefusalSeconds = 10;
    constexpr float kMagmaRefusalGridYd = 2.0f;
    // A refused route is refused from memory only while the bot is still in
    // the same cell of this size: from anywhere else (a knockback to the safe
    // side, a step) the route is looked at again at once.
    constexpr float kMagmaRefusalOriginGridYd = 4.0f;

    // The core's rule for "in magma" (Player::UpdateTerainEnvironmentFlags):
    // the liquid there is magma and the point is under it, in it, or just on
    // its surface. `typeFlags` is GridMapLiquidData::type_flags, `status` the
    // GridMapLiquidStatus that getLiquidStatus returned.
    inline bool IsMagmaHazard(std::uint32_t typeFlags, std::uint32_t status)
    {
        if (!(typeFlags & kMagmaLiquidTypeFlag))
            return false;
        return (status & (kLiquidStatusUnderWater | kLiquidStatusInWater | kLiquidStatusWaterWalk)) != 0;
    }

    // The same rule for slime (round 2, Codex HIGH 2026-10-25): the liquid
    // there is slime and the point is under it, in it, or just on its
    // surface. The swim hand-off routes use it (see above), and so does the
    // DispatchMovement restore gate: a clamp sunk in slime is no safer a floor
    // signal than one sunk in magma, so the stair-workaround restore needs a
    // confirmed floor there as well. Ordinary route refusal stays magma-only.
    inline bool IsSlimeHazard(std::uint32_t typeFlags, std::uint32_t status)
    {
        if (!(typeFlags & kSlimeLiquidTypeFlag))
            return false;
        return (status & (kLiquidStatusUnderWater | kLiquidStatusInWater | kLiquidStatusWaterWalk)) != 0;
    }

    // One sampled guard's per-sample verdict: magma always, slime too when
    // `withSlime` (the 0x81, 0x1 and swim-out hand-off routes); ordinary moves
    // pass false and see exactly what IsMagmaHazard saw. Magma wins when both
    // bits are set (one liquid type per point in practice).
    enum class LiquidHazard
    {
        None,
        Magma,
        Slime,
    };

    inline LiquidHazard ClassifyLiquidHazard(std::uint32_t typeFlags, std::uint32_t status, bool withSlime)
    {
        if (IsMagmaHazard(typeFlags, status))
            return LiquidHazard::Magma;
        if (withSlime && IsSlimeHazard(typeFlags, status))
            return LiquidHazard::Slime;
        return LiquidHazard::None;
    }

    // Where the guard applies: bots on an instance map (dungeon or raid,
    // Map::IsDungeon). The movement layer runs for every bot on the server;
    // the open world keeps its old movement (no added terrain queries for the
    // random population, no refused grind or quest targets).
    inline bool MagmaGuardApplies(bool mapIsInstance)
    {
        return mapIsInstance;
    }

    // A bot's memory of the last destination the guard refused: the same
    // destination (same map, same 2 yd cell) is refused again without terrain
    // queries until `until` (wall-clock seconds).
    // A route refusal also keeps the bot's origin cell (`hasOrigin`): the
    // memory then holds only while the bot is still there. An end point in
    // magma is refused from anywhere (no origin).
    struct MagmaRefusal
    {
        bool set = false;
        std::uint32_t mapId = 0;
        std::int32_t cellX = 0, cellY = 0, cellZ = 0;
        bool hasOrigin = false;
        std::int32_t originX = 0, originY = 0, originZ = 0;
        std::int64_t until = 0;
    };

    inline std::int32_t MagmaGridCell(float v, float grid)
    {
        if (v != v)
            return 0;
        return static_cast<std::int32_t>(std::floor(v / grid));
    }

    inline std::int32_t MagmaRefusalCell(float v)
    {
        return MagmaGridCell(v, kMagmaRefusalGridYd);
    }

    // An end point in magma: refused from anywhere.
    inline MagmaRefusal MagmaRefusalFor(std::uint32_t mapId, float x, float y, float z, std::int64_t now)
    {
        MagmaRefusal r;
        r.set = true;
        r.mapId = mapId;
        r.cellX = MagmaRefusalCell(x);
        r.cellY = MagmaRefusalCell(y);
        r.cellZ = MagmaRefusalCell(z);
        r.until = now + kMagmaRefusalSeconds;
        return r;
    }

    // A route into magma: refused from this origin only.
    inline MagmaRefusal MagmaRouteRefusalFor(std::uint32_t mapId, float x, float y, float z,
        float fromX, float fromY, float fromZ, std::int64_t now)
    {
        MagmaRefusal r = MagmaRefusalFor(mapId, x, y, z, now);
        r.hasOrigin = true;
        r.originX = MagmaGridCell(fromX, kMagmaRefusalOriginGridYd);
        r.originY = MagmaGridCell(fromY, kMagmaRefusalOriginGridYd);
        r.originZ = MagmaGridCell(fromZ, kMagmaRefusalOriginGridYd);
        return r;
    }

    // Whether a move from (fromX, fromY, fromZ) to (mapId, x, y, z) at `now`
    // is refused from memory.
    inline bool MagmaRefusalActive(MagmaRefusal const& r, std::uint32_t mapId, float x, float y, float z,
        float fromX, float fromY, float fromZ, std::int64_t now)
    {
        if (!(r.set && now < r.until && r.mapId == mapId &&
                r.cellX == MagmaRefusalCell(x) && r.cellY == MagmaRefusalCell(y) && r.cellZ == MagmaRefusalCell(z)))
            return false;
        if (!r.hasOrigin)
            return true;
        return r.originX == MagmaGridCell(fromX, kMagmaRefusalOriginGridYd) &&
            r.originY == MagmaGridCell(fromY, kMagmaRefusalOriginGridYd) &&
            r.originZ == MagmaGridCell(fromZ, kMagmaRefusalOriginGridYd);
    }

    // A follower's follow slot, or a chaser's chase goal, that is magma, or a
    // route to it that walks into magma after dry ground: the bot holds at its
    // own spot instead of taking the engine's follow or chase move. Neither
    // generator samples the terrain, and both goals come off the navmesh,
    // which calls a shallow lava shelf plain ground (RFC first-bridge deaths
    // 2026-11-13: the Ragefire Chasm follow slot landed on a shelf 0.1-0.4 yd
    // under the lava surface, 605 a tick). A bot already standing in magma is
    // never held there: it walks out first, so its own spot is not a reason to
    // hold and the caller leaves the goal alone.
    inline bool FollowSlotHolds(bool guard, bool slotInMagma, bool routeInMagma)
    {
        if (!guard)
            return false;
        return slotInMagma || routeInMagma;
    }

    // A commander task's walk nudge (a plain core MovePoint to its standing
    // point, BotCommands NudgeTaskWalk) on an instance map waits while the
    // bot's magma refusal memory on this map is still in its window, or when the
    // standing point itself is in magma: otherwise the nudge walks the bot
    // where MoveTo2 refused to. The memory alone decides, not the last try's
    // fail reason: MoveTo2 resets that reason on every try (the action log's
    // note), so a later try that failed otherwise cleared it, and the nudge then
    // walked across the magma (Codex review 2026-10-23).
    inline bool MagmaNudgeBlocked(bool guardApplies, MagmaRefusal const& r, std::uint32_t mapId,
        std::int64_t now, bool standInMagma)
    {
        if (!guardApplies)
            return false;
        if (standInMagma)
            return true;
        return r.set && r.mapId == mapId && now < r.until;
    }

    struct MagmaPoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // The point the core chase generator is handed: `distance` from the target
    // at `angle` off the target's facing (TargetedMovementGenerator's
    // m_fOffset/m_fAngle, the pair ChaseTo passes to MoveChase; the generator
    // adds the chaser's bounding radius to the offset, so the caller passes
    // that effective distance). A zero distance is a melee chase: the core
    // sends the bot to an attack point on the creature itself and ignores the
    // angle, so the goal is the target's spot. The magma guard must plan
    // against THIS point: a point derived from the bot's side of the target is
    // dry while an angled or behind slot at the same distance sits in the lava.
    inline MagmaPoint ChaseGoalPoint(float targetX, float targetY, float targetOrientation, float distance,
        float angle)
    {
        MagmaPoint goal;
        goal.x = targetX;
        goal.y = targetY;
        if (!(distance > 0.0f))
            return goal;
        float const slot = targetOrientation + angle;
        goal.x = targetX + distance * std::cos(slot);
        goal.y = targetY + distance * std::sin(slot);
        return goal;
    }

    // The verdict on a route. `cut` true: it walks into magma after dry
    // ground (MoveTo2 then refuses the whole move and dispatches nothing);
    // `keepPoints`, `addPoint` and `point` say where the dry part ends (the
    // first `keepPoints` path points, then the last dry sample between two
    // path points when `addPoint`), for the log and the tests.
    struct MagmaPathCut
    {
        bool cut = false;
        std::size_t keepPoints = 0;
        bool addPoint = false;
        MagmaPoint point;
    };

    // How many samples lie strictly between two path points `length` yards
    // apart, at most kMagmaSampleStepYd apart.
    inline std::size_t MagmaInteriorSamples(float length)
    {
        if (!(length > kMagmaSampleStepYd))
            return 0;
        return static_cast<std::size_t>(std::ceil(length / kMagmaSampleStepYd)) - 1;
    }

    // How far (yards) the interior-sample budget reaches at full density:
    // kMagmaMaxInteriorSamples samples at kMagmaSampleStepYd spacing. A route
    // longer than this needs more interior samples than the budget holds, past
    // which only the path points are looked at. The swim hand-off refuses such
    // a route (Codex HIGH 2026-10-25) instead of checking it sparsely; other
    // callers keep the shared budget as is.
    constexpr float kMagmaFullSampleCoveredYd = kMagmaMaxInteriorSamples * kMagmaSampleStepYd;

    // Whether the route `line` (first point the bot's spot) is too long to
    // check at the guard's full density: its total length exceeds what the
    // interior-sample budget covers, past which only path points are looked
    // at, so slime/magma between two safe late waypoints would be missed.
    inline bool MagmaRouteTooLongToCheck(std::vector<MagmaPoint> const& line)
    {
        float total = 0.0f;
        for (std::size_t i = 1; i < line.size(); ++i)
        {
            float const dx = line[i].x - line[i - 1].x;
            float const dy = line[i].y - line[i - 1].y;
            float const dz = line[i].z - line[i - 1].z;
            total += std::sqrt(dx * dx + dy * dy + dz * dz);
            if (total > kMagmaFullSampleCoveredYd)
                return true;
        }
        return false;
    }

    // The route from `start` (the bot) along `path`, sampled at each path
    // point and every kMagmaSampleStepYd between them (at most
    // `maxInteriorSamples` of those, nearest the bot first), is cut at its
    // first magma sample. A bot already standing in magma walks out first:
    // the leading run of magma samples is not a cut (only magma after dry
    // ground is). A route that never leaves magma is no cut either (the
    // destination check refuses a magma end point before this runs).
    template <typename InMagma>
    MagmaPathCut CutPathBeforeMagma(MagmaPoint const& start, std::vector<MagmaPoint> const& path, InMagma inMagma,
        std::size_t maxInteriorSamples = kMagmaMaxInteriorSamples)
    {
        MagmaPathCut result;
        std::size_t budget = maxInteriorSamples;
        bool dry = !inMagma(start);
        MagmaPoint lastSafe = start;
        MagmaPoint prev = start;
        for (std::size_t i = 0; i < path.size(); ++i)
        {
            MagmaPoint const& next = path[i];
            float const dx = next.x - prev.x, dy = next.y - prev.y, dz = next.z - prev.z;
            float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
            std::size_t const interior = MagmaInteriorSamples(length);
            std::size_t const taken = interior < budget ? interior : budget;
            budget -= taken;
            for (std::size_t k = 0; k <= taken; ++k)
            {
                bool const atPoint = k == taken;
                MagmaPoint sample = next;
                if (!atPoint)
                {
                    float const t = static_cast<float>(k + 1) / static_cast<float>(interior + 1);
                    sample.x = prev.x + dx * t;
                    sample.y = prev.y + dy * t;
                    sample.z = prev.z + dz * t;
                }
                if (inMagma(sample))
                {
                    if (dry)
                    {
                        result.cut = true;
                        result.keepPoints = i;
                        // The last safe sample is a path point already kept
                        // (or the bot's own spot) unless it lies inside this
                        // segment.
                        result.addPoint = k > 0;
                        result.point = lastSafe;
                        return result;
                    }
                    continue;
                }
                dry = true;
                lastSafe = sample;
            }
            prev = next;
        }
        return result;
    }

    // DispatchMovement rewrites each path point with UpdateAllowedPositionZ; over a thin floor above WMO
    // magma the core answers liquid level - 2 (GetWaterOrGroundLevel, swim=true, on LIQUID_MAP_ABOVE_WATER),
    // which is inside the magma (Dragonmaw Retreat r39s2/r41s2, specs/dmr-step453.md). Keep the navmesh
    // floor then — but only when the core's COLLISION (vmap) height query confirms a real floor at the
    // navmesh point (round 1): a bridged/eroded navmesh point over an open hole reads dry but has no
    // floor, so restoring its z would float the bot; a polygon a little above open lava reads dry but
    // the bot would walk on the lava surface.
    inline bool KeepNavmeshZOverMagma(bool guardApplies, bool lowered, bool clampedInMagma, bool navmeshInMagma,
        bool floorConfirmed)
    {
        return guardApplies && lowered && clampedInMagma && !navmeshInMagma && floorConfirmed;
    }
    // The crypt-stair large-drop workaround (MovementActions.cpp) keeps the
    // navmesh z whenever the clamp dropped more than kMaxWaypointDrop. It keeps
    // that behaviour only when the ORIGINAL clamp is neither in magma nor in
    // slime: a big drop into either means the navmesh point may itself sit
    // over a hole or an open liquid pool, so only the confirmed-floor path
    // may restore. Pure, so the policy test owns the table.
    inline bool KeepStairNavmeshZ(bool lowered, bool bigDrop, bool clampedInMagmaOrSlime)
    {
        return lowered && bigDrop && !clampedInMagmaOrSlime;
    }
}
