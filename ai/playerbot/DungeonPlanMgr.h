#pragma once

#include "DungeonPlanReloadPolicy.h"

#include <cstdint>
#include <functional>
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
    };

    // `.bot run reloadplans`, world thread only: reads the four plan tables and `ai_boss_tactic` again through the
    // startup loaders into new tables and swaps both in when every row passes (CommitPlanReload). `inUse`: every run's
    // plan pointer (their table is kept alive). The caller rebuilds the boss spawn cache after a swap.
    //
    // `host` (ForeverCore's `.tb plan reload`, null on the Turtle core): a host table that must change with the plan
    // tables. `prepare` runs once the new plan table is read and sealed: it reads and validates the host's rows against it
    // into the host's own temporaries and returns the rows read (the revision check compares them with the table's row
    // count) and why they may not be installed ("" = fine); `install` moves them in once, only when every check passed.
    // The table's revision (`table`, `columns`) is read before and after like the six tables'. A refusal of any kind
    // leaves plans, tactics and the host's table untouched.
    struct PlanReloadHost
    {
        std::string table;      // "" = no host table this time
        std::string columns;    // the host's SELECT list ("`a`, `b`"), for the revision
        std::function<std::string(DungeonPlanTable const& next, std::uint64_t& rowsRead)> prepare;
        std::function<void()> install;
    };
    PlanReloadReport ReloadDungeonPlans(std::vector<DungeonPlan const*> const& inUse, PlanReloadHost const* host = nullptr);
}
