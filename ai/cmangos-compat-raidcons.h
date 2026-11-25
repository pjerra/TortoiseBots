// Turtle side of the both-core helpers for the raid consumables (issue #22 round 1). ForeverCore side:
// forever/src/compat/TbCompat_RaidCons.h (the host module host/TbRaidConsumables.cpp).
//
// The feature needs that host module, which the Turtle core does not build, so every helper is the inert answer here: no
// mode, no bag item, no free buff. The Turtle core's own consumable handling (the brain's UseConsumableAction and the class
// kits) is untouched by this feature on either core. The category argument is the policy's Category number
// (ai/playerbot/policy/raid/RaidConsumablePolicy.h), the same on both cores.
#pragma once

#include "Define.h"

class Player;

inline int RaidConsModeForCompat(Player*)
{
    return 0;
}

inline uint32 RaidConsBagItemForCompat(Player*, int)
{
    return 0;
}

inline bool RaidConsApplyFreeCompat(Player*, int)
{
    return false;
}
