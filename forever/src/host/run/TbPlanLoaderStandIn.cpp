// Slice 5, host package H5: a STAND-IN for `ai::ReloadDungeonPlans` until package P5 merges (spec FOREVER-RUN-S5S6-SPEC.md section 4.2: P5 ports
// ai/playerbot/DungeonPlanMgr.cpp). It does LESS: the reload is refused with a reason, nothing is read or swapped.
//
// The other two functions `.tb plan` and the loading call need, `ai::GetDungeonPlans` and `ai::LoadDungeonPlans`, already have stand-ins in
// forever/src/slice/SliceLinkStubs.cpp (an empty, unsealed table; counted under the unported `boss` row). Those belong to the slice sources, not to
// this package; P5 removes them when DungeonPlanMgr.cpp joins forever/SLICE5.txt. This one is defined WEAK (GCC and Clang), so P5's strong
// definition replaces it without a link error and the merge needs no edit to link. THE MERGE PACKAGE DELETES THIS FILE with that step.
// Built with TORTOISEBOTS_SLICE1 only.
#include "ai/playerbot/DungeonPlanMgr.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#if defined(__GNUC__) || defined(__clang__)
#define TB_STANDIN_WEAK __attribute__((weak))
#else
#define TB_STANDIN_WEAK
#endif

namespace ai
{
TB_STANDIN_WEAK PlanReloadReport ReloadDungeonPlans(std::vector<DungeonPlan const*> const& /*inUse*/)
{
    PlanReloadReport report;
    report.outcome.reason = "the dungeon plans were not loaded: the plan loader is not merged yet (stand-in)";
    return report;
}
}

#endif
