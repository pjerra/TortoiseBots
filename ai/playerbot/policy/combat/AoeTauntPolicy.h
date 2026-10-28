#pragma once

// Gate for the tank AoE taunts (Challenging Shout 1161, Challenging Roar
// 5209): 10-min cooldowns, so they fire only for real loose-add emergencies,
// never for a pack the tank already holds. Pure rule, no engine types: the
// caller (LooseAddsNearTrigger) translates game state into plain inputs.

#include <vector>

namespace ai
{
    constexpr float kAoeTauntRadiusYd = 10.0f;     // Challenging Shout/Roar radius index 13
    constexpr unsigned kAoeTauntMinLooseAdds = 3;  // one Taunt fixes one or two

    struct LooseAddCandidate
    {
        bool victimIsGroupPlayer = false; // victim is the bot, its master or a group member
        bool victimIsTank = false;        // victim holds it already: no taunt needed
        float distanceToBot = 0.0f;       // 2D distance from the bot
    };

    // A candidate counts when a group member (never a tank) is hit and the
    // mob is inside the shout radius.
    inline unsigned CountLooseAddsInReach(std::vector<LooseAddCandidate> const& candidates)
    {
        unsigned loose = 0;
        for (std::vector<LooseAddCandidate>::const_iterator i = candidates.begin(); i != candidates.end(); ++i)
        {
            if (!i->victimIsGroupPlayer || i->victimIsTank)
                continue;
            if (i->distanceToBot > kAoeTauntRadiusYd)
                continue;
            ++loose;
        }
        return loose;
    }

    inline bool ShouldAoeTaunt(unsigned looseInReach) { return looseInReach >= kAoeTauntMinLooseAdds; }
} // namespace ai
