// Slice 5, host package H5: a STAND-IN for the dungeon plan loader, until package P5 merges (spec FOREVER-RUN-S5S6-SPEC.md section 4.2, P5 ports
// ai/playerbot/DungeonPlanMgr.cpp). It does LESS: the plan table stays empty and unsealed, so `.tb plan` answers `not-loaded` and no run can start.
//
// The three functions are declared in ai/playerbot/DungeonPlanMgr.h and defined here as WEAK symbols (GCC and Clang): when P5's DungeonPlanMgr.cpp is
// linked, its strong definitions replace these without a link error, so the merge needs no edit to link. THE MERGE PACKAGE DELETES THIS FILE once
// DungeonPlanMgr.cpp is in forever/SLICE5.txt (it must not stay as dead code that could answer for the real loader on a compiler without weak symbols).
// Built with TORTOISEBOTS_SLICE1 only.
#include "ai/playerbot/DungeonPlanMgr.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "Log.h"

#if defined(__GNUC__) || defined(__clang__)
#define TB_STANDIN_WEAK __attribute__((weak))
#else
#define TB_STANDIN_WEAK
#endif

namespace ai
{
TB_STANDIN_WEAK DungeonPlanTable& GetDungeonPlans()
{
    static DungeonPlanTable table;      // never sealed: nothing is loaded
    return table;
}

TB_STANDIN_WEAK void LoadDungeonPlans()
{
    TC_LOG_INFO("server.loading", ">> Dungeon plans: the plan loader is not merged yet (stand-in): no plan is loaded");
}

TB_STANDIN_WEAK PlanReloadReport ReloadDungeonPlans(std::vector<DungeonPlan const*> const& /*inUse*/)
{
    PlanReloadReport report;
    report.outcome.reason = "the dungeon plans were not loaded: the plan loader is not merged yet (stand-in)";
    return report;
}
}

#endif
