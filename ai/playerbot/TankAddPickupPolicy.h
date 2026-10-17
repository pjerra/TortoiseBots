#pragma once

// TankAddPickupPolicy — pure off-tank add-pickup rules (issue #905).
//
// Donor behaviour (mod-playerbots, read-only reference @ ed54b459):
// `PlayerbotAI::HasAggro` (Bot/PlayerbotAI.cpp) counts a mob as held when
// its victim is the bot, or — for any tank but the explicit main tank —
// when its victim is another tank; and `FindTankTargetSmartStrategy::
// IsBetter` (Ai/Base/Value/TankTargetValue.cpp) pins the explicitly flagged
// main tank to its current target while the group fields more than one
// tank. So the main tank keeps the skull and the off-tanks peel to loose
// adds first; a lone tank, or a group with no raid Main Tank flag, keeps
// the normal loose-add-first tournament.
//
// The shared "has aggro" value keeps victim==bot on purpose: raid DPS rows
// (wand-on-aggro, kite, pet torment) depend on it. Only the tank assist
// gate reads the wider rule, through TankHoldsCurrentTarget; the explicit
// main-tank pin lives in the tournament via MainTankPolicy.h.

namespace ai
{
    // Donor `PlayerbotAI::HasAggro` minus the validity preamble: the bot's
    // own mob is held, and — unless this bot is the explicit main tank — so
    // is a mob on another tank. An off-tank parked on the main tank's skull
    // therefore still counts as holding it and peels to loose adds.
    inline bool TankHoldsCurrentTarget(bool victimIsBot, bool victimIsTank, bool isExplicitMainTank)
    {
        if (victimIsBot)
            return true;

        return !isExplicitMainTank && victimIsTank;
    }

    // The rest of the assist gate: peel only while holding the current mob,
    // and finish a held mob at or below LowHealth first (the add waits a few
    // seconds, a half-dead mob left behind waits forever).
    inline bool TankPeelsToLooseAdd(bool holdsCurrent, float currentHealthPct, float lowHealthPct)
    {
        if (!holdsCurrent)
            return false;

        return currentHealthPct > lowHealthPct;
    }
}
