// Slice 6, package N6 (D-b): see TbRunStands.h. Built with TORTOISEBOTS_SLICE1 only.
//
// The legacy glue (commands/Commander.cpp:7532 ApproachStandSearch, :7603 NearestReachablePoint) ported call for call, with these differences (said, not hidden):
//  * NO STAND IN OR OVER LIQUID. The legacy ring search never asked for liquid (Turtle's mesh has no lava floor the path would cross; the ForeverCore mesh of
//    Ragefire Chasm has magma next to the floor). Every candidate, every vertex of its path and every leg between two vertices (sampled every <= 1.5 yd) is
//    refused by the ONE rule of TbRunLiquid.h (the core's IsInWater bits incl. OCEAN_FLOOR ORed in, plus liquid present with its level >= z - 0.6).
//  * NO STAND OFF THE NAVMESH. A candidate needs a complete path (DetourPointReachable: not partial beyond the slop, not a shortcut, not "not using path"); a point
//    with no map height is dropped before any path is asked.
//  * ExcludeSteepSlopes is not ported (TbCompat_Unported.h): the path may use a slope Turtle excludes; the verdict is never stricter than the core's own path.
//  * The boss anchor circle (BossAnchorOfBoss) is not linked on this build: a boss stand is not held inside an anchor. Ragefire Chasm has no boss-tactic rows.
// Cost: one ring search = at most (25 - ApproachStandMinRing)/5 + 1 rings x 12 points; each point costs one height read, a few liquid reads and at most ONE
// PathFinder query and ONE line-of-sight ray (a point with no height, in liquid, or with no complete path costs less). For RFC (pull radius 10-30 yd,
// min ring 12-35) that is 12-60 candidates, once per request. R6: plus one liquid read per 1.5 yd of each COMPLETE path (a 60 yd path = 41 reads, a few us each).
#include "host/run/TbRunStands.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/run/TbRun.h"
#include "host/run/TbRunFactsPure.h"
#include "host/run/TbRunLiquid.h"

#include <chrono>
#include <cmath>
#include <list>

#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "PathGenerator.h"
#include "Unit.h"

#include "compat/TbCompat_Unit.h"
#include "playerbot/WorldPosition.h"

namespace TortoiseBots
{
namespace run
{
namespace
{
StandCost& TotalsRw()
{
    static StandCost totals;
    return totals;
}

struct Counter
{
    StandCost& cost;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    explicit Counter(StandCost& c) : cost(c) {}
    ~Counter()
    {
        cost.costUs = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        StandCost& t = TotalsRw();
        t.candidates += cost.candidates;
        t.pathQueries += cost.pathQueries;
        t.losQueries += cost.losQueries;
        t.costUs = std::max(t.costUs, cost.costUs);       // the worst single search
    }
};

float Dist(float ax, float ay, float az, float bx, float by, float bz)
{
    return ai::PlanDistance(ax, ay, az, bx, by, bz);
}

float DistLead(Player* lead, float x, float y, float z)
{
    return Dist(lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ(), x, y, z);
}

// R6 (review items 2 and 3): one liquid rule for every point and every leg (TbRunLiquid.h): the core's bits (OCEAN_FLOOR ORed in near a floor), the level rule at
// the point's own z (a path laid on the lava surface reads ABOVE_WATER from 0.5 yd up, so no lift), and the legs between path points sampled every <= 1.5 yd.
bool InLiquid(Map* map, float x, float y, float z)
{
    return liquid::PointRefusedOnMap(map, x, y, z);
}

template <class Points>
bool PathInLiquid(Map* map, Points const& points)
{
    return liquid::PathRefused(points, points.size(), [map](float x, float y, float z) { return liquid::PointRefusedOnMap(map, x, y, z); });
}

// The lead of the run: a living player in the world on a map.
Player* LeadOf(RunRec const& rec)
{
    Player* lead = ObjectAccessor::FindPlayer(rec.leader);
    return lead && lead->IsAlive() && lead->IsInWorld() && !lead->IsBeingTeleported() && lead->FindMap() ? lead : nullptr;
}

// Living hostile creatures (no pets or totems of anybody) within `range` of the lead (Commander.cpp:7093 HostileCreaturesAround).
void HostileCreaturesAround(Player* lead, float range, std::list<Unit*>& out)
{
    out.clear();
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(lead, lead, range);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(lead, out, check);
    Cell::VisitAllObjects(lead, searcher, range);
    out.remove_if([lead](Unit* unit) { return !unit->IsCreature() || !unit->GetOwnerGUID().IsEmpty() || !lead->IsHostileTo(unit) || !unit->IsAlive(); });
}
}

bool SearchApproachStand(RunRec const& rec, uint32 team, float& ox, float& oy, float& oz, StandCost* costOut)
{
    using namespace DungeonRun;
    StandCost local;
    StandCost& cost = costOut ? *costOut : local;
    cost = StandCost();
    Counter counter(cost);
    Player* const lead = LeadOf(rec);
    ai::PlanStep const* const stepPtr = Current(rec.run);
    if (!lead || !stepPtr)
        return false;
    ai::PlanStep const& step = *stepPtr;
    Map* const map = lead->GetMap();
    std::vector<StandHostile> hostiles;
    float const reach = kApproachStandYd + kApproachStandHostileYd;
    std::list<Unit*> around;
    HostileCreaturesAround(lead, DistLead(lead, step.x, step.y, step.z) + reach, around);
    for (Unit* unit : around)
        if (Dist(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), step.x, step.y, step.z) <= reach)
            hostiles.push_back(StandHostile{unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetEntry()});
    std::vector<StandCandidate> candidates;
    float const minRing = ApproachStandMinRing(step);
    for (float ring = minRing; ring <= kApproachStandYd + 0.01f; ring += 5.0f)
        for (int i = 0; i < 12; ++i)
        {
            float const angle = static_cast<float>(i) * 3.14159265f / 6.0f;
            float const px = step.x + ring * std::cos(angle);
            float const py = step.y + ring * std::sin(angle);
            float const ground = GetHeightCompat(map, px, py, step.z + 5.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - step.z) > 10.0f)
                continue;
            ++cost.candidates;
            if (InLiquid(map, px, py, ground))
                continue;                       // never a stand in lava or water (the point itself, at its own z)
            StandCandidate c;
            c.x = px;
            c.y = py;
            c.z = ground;
            PathFinder path(lead);
            ++cost.pathQueries;
            PathCalculateCompat(path, px, py, ground, false);
            uint32 const type = static_cast<uint32>(PathTypeCompat(path));
            G3D::Vector3 const end = PathActualEndCompat(path);
            c.reachable = !(type & (PATHFIND_NOT_USING_PATH | PATHFIND_SHORTCUT)) &&
                DetourPointReachable((type & PATHFIND_NOPATH) != 0, (type & PATHFIND_INCOMPLETE) != 0, Dist(end.x, end.y, end.z, px, py, ground),
                    DistLead(lead, end.x, end.y, end.z), DistLead(lead, px, py, ground), PathLengthCompat(path));
            if (!c.reachable)
                continue;
            Movement::PointsArray const& way = PathPointsCompat(path);
            if (!way.empty() && PathInLiquid(map, way))
                continue;                       // its path crosses liquid
            c.pathYd = PathLengthCompat(path);
            for (G3D::Vector3 const& v : way)
                c.path.push_back(PathPoint{v.x, v.y, v.z});
            ++cost.losQueries;
            c.los = IsInLineOfSightCompat(map, px, py, ground + 2.0f, step.x, step.y, step.z + 2.0f, true);
            candidates.push_back(std::move(c));
        }
    int const best = PickApproachStand(rec.run, step, candidates, hostiles, team);
    if (best < 0)
        return false;
    ox = candidates[static_cast<size_t>(best)].x;
    oy = candidates[static_cast<size_t>(best)].y;
    oz = candidates[static_cast<size_t>(best)].z;
    return true;
}

bool SearchStallStand(RunRec const& rec, float& ox, float& oy, float& oz, StandCost* costOut)
{
    using namespace DungeonRun;
    StandCost local;
    StandCost& cost = costOut ? *costOut : local;
    cost = StandCost();
    Counter counter(cost);
    Player* const lead = LeadOf(rec);
    ai::PlanStep const* const stepPtr = Current(rec.run);
    if (!lead || !stepPtr)
        return false;
    ai::PlanStep const& step = *stepPtr;
    Map* const map = lead->GetMap();
    float const maxYd = MoveFallbackYd(step);
    auto reachable = [&](float px, float py, float pz) -> bool
    {
        float const ground = GetHeightCompat(map, px, py, pz + 5.0f);
        if (ground <= INVALID_HEIGHT || std::fabs(ground - pz) > 10.0f)
            return false;
        ++cost.candidates;
        if (InLiquid(map, px, py, ground))
            return false;                       // dry ground only (the stalled walk's own move had failed in the water)
        PathFinder path(lead);
        ++cost.pathQueries;
        PathCalculateCompat(path, px, py, ground, false);
        uint32 const type = static_cast<uint32>(PathTypeCompat(path));
        if (type & (PATHFIND_NOT_USING_PATH | PATHFIND_SHORTCUT))
            return false;
        G3D::Vector3 const end = PathActualEndCompat(path);
        if (!DetourPointReachable((type & PATHFIND_NOPATH) != 0, (type & PATHFIND_INCOMPLETE) != 0, Dist(end.x, end.y, end.z, px, py, ground),
                DistLead(lead, end.x, end.y, end.z), DistLead(lead, px, py, ground), PathLengthCompat(path)))
            return false;
        Movement::PointsArray const& way = PathPointsCompat(path);
        if (!way.empty() && PathInLiquid(map, way))
            return false;
        // And its OWN path toward the plan's point leads on (Commander.cpp:7640: the pit the tank fell into has a ring point the lead's path reaches, no way out).
        PathFinder probe(lead);
        ++cost.pathQueries;
        PathCalculateCompat(probe, G3D::Vector3(px, py, ground), G3D::Vector3(step.x, step.y, step.z), false);
        uint32 const probeType = static_cast<uint32>(PathTypeCompat(probe));
        G3D::Vector3 const probeEnd = PathActualEndCompat(probe);
        if (!DetourCandidateLeadsOn((probeType & PATHFIND_NOPATH) != 0, (probeType & PATHFIND_INCOMPLETE) != 0,
                Dist(probeEnd.x, probeEnd.y, probeEnd.z, px, py, ground), DistLead(lead, probeEnd.x, probeEnd.y, probeEnd.z),
                Dist(px, py, ground, step.x, step.y, step.z), Dist(probeEnd.x, probeEnd.y, probeEnd.z, step.x, step.y, step.z), ground - step.z))
            return false;
        ox = px;
        oy = py;
        oz = ground;
        return true;
    };
    if (reachable(step.x, step.y, step.z))
        return true;
    float const rings[] = {3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 25.0f};
    for (float ring : rings)
        for (int i = 0; ring <= maxYd && i < 12; ++i)
        {
            float const angle = static_cast<float>(i) * 3.14159265f / 6.0f;
            if (reachable(step.x + ring * std::cos(angle), step.y + ring * std::sin(angle), step.z))
                return true;
        }
    return false;
}

StandCost const& StandTotals()
{
    return TotalsRw();
}
}
}

#endif
