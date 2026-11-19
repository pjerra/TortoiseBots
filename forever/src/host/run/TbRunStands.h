// Slice 6, package N6 (D-b): the two stand searches the run leader asks the glue for after its tick (commands/Commander.cpp:7532 ApproachStandSearch, :7603
// NearestReachablePoint; answered there at :13941-13971). Each runs ONCE per request of the policy (Run::approachStandWanted, Run::stallStandInWanted),
// never per tick, on the world thread, from the core's public API. Compiled with TORTOISEBOTS_SLICE1 only (an empty object otherwise).
#pragma once

#include "Define.h"

namespace ai { struct PlanStep; }

namespace TortoiseBots
{
namespace run
{
struct RunRec;

// What one search cost (for `.tb run status` and the notes).
struct StandCost
{
    uint32 candidates = 0;          // ring points that were put on the ground and tested
    uint32 pathQueries = 0;         // PathFinder::CalculatePath calls
    uint32 losQueries = 0;          // line-of-sight rays
    uint32 costUs = 0;              // wall clock of the search, microseconds
};

// The approach stand of the run's current pull or boss step: the safe point on a ring around the step's point from which the pack is in sight, reached by a
// complete navmesh path of the lead bot, on dry ground, away from every other hostile (DungeonRun::PickApproachStand decides among the candidates). False:
// none (the policy then says so and pauses by its own rules).
bool SearchApproachStand(RunRec const& rec, uint32 team, float& x, float& y, float& z, StandCost* cost = nullptr);

// The nearest point the lead bot can walk to on dry ground at most DungeonRun::MoveFallbackYd from the current step's point (the stalled walk's stand-in).
bool SearchStallStand(RunRec const& rec, float& x, float& y, float& z, StandCost* cost = nullptr);

// The totals since boot.
StandCost const& StandTotals();
}
}
