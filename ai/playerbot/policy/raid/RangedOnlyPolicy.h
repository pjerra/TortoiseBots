#pragma once

// Pure decision rules for the ranged_only boss tactic: Ayamiss climbs 20 yd in the air 5 s into the fight, where a
// melee bot cannot reach her, so the melee bots take the adds until she lands (consumer: DpsTargetValue.cpp,
// TankTargetValue.cpp and BossTacticsActions.cpp).
//
// The only dependency is BossTacticsPolicy.h's row types (no core includes), so the rules stay testable in
// tools/test_ranged_only_policy.cpp. The caller passes the bot's role and the two heights; the comparison is the raw z
// difference, never the navmesh height: the boss stands in the air, not on a floor.

#include <cstdint>
#include "../../BossTacticsPolicy.h"

namespace ai
{
    // Whether a melee bot standing `botFloorZ` under a subject at `subjectZ` is held off it: the subject stands
    // `gapYd` yd or more above the bot. The threshold is inclusive (exactly gapYd holds); a ranged bot is never held.
    // The caller passes the row's gap, so a subject with no ranged_only row (0) is never held and a player's attack
    // order still wins there. Inside an active row the hold outranks that order as well (Onyxia P2: the pull order
    // lasted the whole air phase and kept the tanks on the airborne boss), so `hasExplicitOrder` no longer exempts the
    // bot; it stays in the signature as the caller's input.
    inline bool RangedOnlyHolds(bool isMeleeBot, float subjectZ, float botFloorZ, float gapYd, bool /*hasExplicitOrder*/)
    {
        if (!isMeleeBot || gapYd <= 0.0f)
            return false;
        return subjectZ - botFloorZ >= gapYd;
    }

    // The gap of the fight's own ranged_only row that holds `entry` (0: no such row), read from a fight scan's active
    // rows. The map's static table (`BossTacticTable::RangedOnlyGap`) also names rows of bosses that are not in this
    // fight at all, so it may only pre-filter: answering from it holds a melee bot off a subject whose encounter is
    // not engaged and erases the order there (Onyxia P2: a bot fighting the whelps anywhere else on the map). The
    // row's held entry is its target, or its boss when `target_entry` is 0.
    inline float RangedOnlyFightGap(const ActiveBossRows& rows, std::uint32_t entry)
    {
        for (const BossTacticRow* row : rows)
        {
            if (!row || row->kind != BossTacticKind::RangedOnly || !(row->param > 0.0f))
                continue;
            if ((row->targetEntry != 0 ? row->targetEntry : row->bossEntry) == entry)
                return row->param;
        }
        return 0.0f;
    }
}
