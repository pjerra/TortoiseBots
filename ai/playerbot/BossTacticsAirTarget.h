#pragma once

// Game-state translation of the air_target row kind (BossTacticsPolicy.h):
// whether a unit stays a target because its own row says the air phase keeps
// it. The decision itself is AirTargetKeep, pure and tested on its own; this
// only reads the table and the unit.
//
// No core includes beyond the playerbot ones: the target values and the reach
// actions call this from their own tick, so it stays a lookup, never a grid
// sweep.

#include "playerbot/PlayerbotAI.h"
#include "BossTacticsMgr.h"

namespace ai
{
    inline bool IsAirTargetKept(PlayerbotAI* ai, Player* bot, Unit* unit)
    {
        if (!ai || !bot || !unit || !bot->IsInWorld())
            return false;
        // The rows only act for a bot whose tactics are armed (the same gate
        // as BossTacticsTriggers.cpp Ready); every other bot behaves as before.
        if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
            return false;

        const std::vector<BossTacticRow>* rows = GetBossTactics().Rows(bot->GetMapId(), unit->GetEntry());
        if (!rows)
            return false;

        const Creature* creature = unit->ToCreature();
        const bool evading = creature && creature->IsInEvadeMode();
        const bool unreachable = creature && creature->IsEvadeBecauseTargetNotReachable();

        for (const BossTacticRow& row : *rows)
        {
            if (row.kind != BossTacticKind::AirTarget)
                continue;
            if (AirTargetKeep(TacticSubjectEntry(row) == unit->GetEntry(), unit->HasAura(row.spellId), evading, unreachable))
                return true;
        }
        return false;
    }
}
