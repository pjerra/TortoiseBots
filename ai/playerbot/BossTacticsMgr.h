#pragma once

#include "BossTacticsPolicy.h"
#include "DungeonPlanReloadPolicy.h"

#include <cstdint>

namespace ai
{
    // The current table, filled by LoadBossTactics() and read-only afterwards; `.bot run reloadplans` replaces it with a
    // new sealed table (DungeonPlanMgr.h ReloadDungeonPlans). Nothing keeps a row pointer past the call that read it.
    BossTacticTable& GetBossTactics();
    ReloadableTable<BossTacticTable>& BossTacticsHolder();

    // Reads `ai_boss_tactic` once at startup (one pass over the rows) and seals
    // the table. A second call does nothing.
    void LoadBossTactics();

    // What one pass over `ai_boss_tactic` could not use: a refused row of a kind this build knows (an error, and a
    // reload refuses the whole set on one) and a row of a listed foreign kind (foreign data: skipped here and by a
    // reload alike, never a refusal).
    struct BossTacticLoadCounts
    {
        std::uint32_t refused = 0;
        std::uint32_t foreign = 0;

        std::uint32_t Skipped() const { return refused + foreign; }
    };

    // One pass over `ai_boss_tactic` into an empty table, then its seal: the rows it could not use, split by verdict
    // (JudgeTacticRow). `rowsRead` (a reload) gets the rows the query handed it, accepted or not.
    BossTacticLoadCounts ReadBossTacticRows(BossTacticTable& table, std::uint64_t* rowsRead = nullptr);
}
