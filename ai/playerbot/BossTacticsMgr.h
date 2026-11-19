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

    // The seek_aura fight records belong to the boss-tactics module (strategy/actions/BossTacticsActions.cpp). The plan reload
    // re-keys them against the new tactic rows before the old table is retired (DungeonPlanMgr.cpp ReloadDungeonPlans), through
    // this pointer: BossTacticsActions.cpp registers ai::ReconcileSeekFightState at startup, and a build that does not compile
    // that file has none, which the reload skips. It keeps the loader free of the 306-place boss-tactics header (slice 5).
    using SeekFightReconciler = void (*)(const BossTacticTable& next);
    void SetSeekFightReconciler(SeekFightReconciler reconciler);
    SeekFightReconciler GetSeekFightReconciler();

    // One pass over `ai_boss_tactic` into an empty table, then its seal: the rows skipped. `rowsRead` (a reload) gets
    // the rows the query handed it, accepted or not.
    std::uint32_t ReadBossTacticRows(BossTacticTable& table, std::uint64_t* rowsRead = nullptr);
}
