#pragma once

#include "DungeonPlanReloadPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{
    // The current plan table, filled by LoadDungeonPlans() and read-only afterwards. A `.bot run reloadplans` replaces it
    // with a new sealed table (ReloadDungeonPlans): a reference or plan pointer taken from it is good for the world tick
    // it was taken in, except a run's plan pointer, which the reload keeps alive (DungeonPlanReloadPolicy.h).
    DungeonPlanTable& GetDungeonPlans();

    // Reads `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step` and `ai_dungeon_boss` once at
    // startup (one pass over each) and seals the table. A second call does nothing.
    void LoadDungeonPlans();

    struct PlanReloadReport
    {
        PlanReloadOutcome outcome;
        std::string summary;       // the boot line of the NEW rows (PlanLoadSummary), also when refused
        std::uint32_t tacticRows = 0;
        std::uint32_t tacticForeign = 0; // rows of a listed foreign kind: skipped by boot and by the reload alike
    };

    // `.bot run reloadplans`, world thread only: reads the four plan tables and `ai_boss_tactic` again through the
    // startup loaders into new tables and swaps both in when every row passes (CommitPlanReload). `inUse`: every run's
    // plan pointer (their table is kept alive). The caller rebuilds the boss spawn cache after a swap.
    PlanReloadReport ReloadDungeonPlans(std::vector<DungeonPlan const*> const& inUse);
}
