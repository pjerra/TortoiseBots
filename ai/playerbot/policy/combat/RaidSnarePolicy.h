#pragma once

// Raid snare rule: in a raid group a snare is only worth the GCD on a mob
// that is attacking someone who is not a tank. Raid trash on the tank is
// already controlled; snaring it (Piercing Howl, Hamstring) spends the
// warrior's GCDs for nothing. Outside a raid group nothing changes.
//
// "Tank" is whoever the module identifies as a tank (forced role, tank
// strategy, talents: PlayerbotAI::IsTank) or carries the raid's Main Tank
// flag (Group::GetMainTankGuid) — the flag is an explicit order from the
// raid leader and sticks whatever the spec reads.

namespace ai
{
    // inRaidGroup: the bot's group is a raid (Group::isRaidGroup, not the
    // instance map: a 5-man party in a dungeon is not a raid). victimIsTank:
    // the mob's victim is a tank by the read above. fleeing: FLEEING motion
    // picks are never a positioning problem (shared runner-finish rule).
    inline bool SnareAllowedInRaid(bool inRaidGroup, bool victimIsTank, bool fleeing)
    {
        if (fleeing)
            return true;
        if (!inRaidGroup)
            return true;
        return !victimIsTank;
    }
} // namespace ai
