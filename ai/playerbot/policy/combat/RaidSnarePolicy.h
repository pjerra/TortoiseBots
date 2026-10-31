#pragma once

// Raid snare rule: in a raid group a snare is only worth the GCD on a mob
// that is attacking someone who is not a tank. Raid trash chasing the tank
// is already controlled; snaring it (Piercing Howl, Hamstring) spends the
// warrior's GCDs for nothing. Outside a raid group nothing changes.

namespace ai
{
    // inRaidGroup: the bot's group is a raid (Group::isRaidGroup, not the
    // instance map: a 5-man party in a dungeon is not a raid). victimOnTank:
    // the mob's victim is a tank. fleeing: FLEEING/ASSISTANCE motion picks
    // are never a positioning problem.
    inline bool SnareAllowedInRaid(bool inRaidGroup, bool victimOnTank, bool fleeing)
    {
        if (fleeing)
            return true;
        if (!inRaidGroup)
            return true;
        return !victimOnTank;
    }
} // namespace ai
