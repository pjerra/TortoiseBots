#pragma once

#include <cstdint>

// Holy Paladin melee-weaving (AiPlayerbot.HolyPaladinMeleeWeave, default 1).
// When enabled, a grouped Holy Paladin whose groupmates (and itself) are all
// at or above the medium-health line walks into melee of its current target,
// keeps Seal of Wisdom up, auto-attacks, and casts Holy Strike on cooldown.
// Forever paladins have Holy Strike but no player Crusader Strike, so Holy
// Strike is the only strike; a rank below Holy Strike's first level simply
// makes the row inert.
//
// No core includes: callers translate game state into plain inputs.
namespace TortoiseBots
{

struct HolyMeleeWeaveInput
{
    bool enabled = false;
    bool isHolyPaladin = false;
    bool inGroup = false;
    bool hasTarget = false;
    bool targetAlive = false;
    bool targetIsPlayer = false;
    bool inRaid = false;
    bool targetEngaged = false;
    uint8_t selfHealthPct = 100;
    uint8_t partyMemberToHealPct = 100;
    uint8_t mediumHealthPct = 70;
};

inline bool HolyMeleeWeaveAllowed(HolyMeleeWeaveInput const& in)
{
    if (!in.enabled || !in.isHolyPaladin || !in.inGroup)
        return false;
    if (!in.hasTarget || !in.targetAlive || in.targetIsPlayer || in.inRaid || !in.targetEngaged)
        return false;
    if (in.selfHealthPct < in.mediumHealthPct || in.partyMemberToHealPct < in.mediumHealthPct)
        return false;
    return true;
}

} // namespace TortoiseBots
