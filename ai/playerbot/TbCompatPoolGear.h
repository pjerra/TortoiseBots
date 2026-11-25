// Both-core helpers of the pool gear top-up: ForeverCore forever/src/compat/TbCompat_PoolGear.h, Turtle ai/cmangos-compat-poolgear.h.
#pragma once
#if __has_include("TbCompat_PoolGear.h")
#include "TbCompat_PoolGear.h"
#else
#include "cmangos-compat-poolgear.h"
#endif

// The pool identity on its own, for the other organic gear decisions that only
// need to know whether the bot is a pool bot (EquipUpgradesAction).
inline bool IsPoolBotCompat(Player* bot)
{
    return PoolGearTopUpIsPoolBotCompat(bot);
}
