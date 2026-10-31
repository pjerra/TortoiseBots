#pragma once

#include <cstdint>

// Stance-dance gates for Whirlwind and Pummel. The engine runs the stance
// prerequisite before isPossible, so a plain "berserker stance" prerequisite
// swaps even when the main spell is on cooldown or unaffordable: the swap
// truncates rage and the cast fails. The dedicated prerequisite actions only
// dance when the main spell can fire this tick.

namespace ai
{
    // Whirlwind costs 25 rage.
    constexpr uint32_t WARRIOR_WHIRLWIND_RAGE_COST = 25;
    // Pummel costs 10 rage.
    constexpr uint32_t WARRIOR_PUMMEL_RAGE_COST = 10;

    // True when the stance swap is worth it: the spell is off cooldown and
    // the bot holds its rage cost.
    inline bool ShouldWhirlwindStanceDance(bool spellReady, uint32_t rage)
    {
        return spellReady && rage >= WARRIOR_WHIRLWIND_RAGE_COST;
    }

    // True when the stance swap is worth it: the spell is off cooldown, the
    // bot holds its rage cost, and the interrupt target is casting.
    inline bool ShouldPummelStanceDance(bool spellReady, uint32_t rage, bool targetCasting)
    {
        return spellReady && rage >= WARRIOR_PUMMEL_RAGE_COST && targetCasting;
    }
} // namespace ai
