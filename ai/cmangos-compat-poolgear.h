// Turtle side of the pool gear top-up helpers. ForeverCore side: forever/src/compat/TbCompat_PoolGear.h.
// Reached through ai/playerbot/TbCompatPoolGear.h.
#pragma once

#include "playerbot/RandomBotFacade.h"
#include "playerbot/PlayerbotFactory.h"
#include "runtime/ClaimLifecycle.h"
#include "runtime/HireLifecycle.h"
#include "runtime/PoolGearTopUpPolicy.h"

inline bool PoolGearTopUpIsPoolBotCompat(Player* bot)
{
    return sRandomBotFacade.IsRandomBot(bot);
}

inline bool PoolGearTopUpIsHiredCompat(Player* bot)
{
    return TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
}

inline bool PoolGearTopUpIsClaimedCompat(Player* bot)
{
    return TortoiseBots::ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow());
}

inline uint32 PoolGearTopUpLastLevelCompat(Player* bot)
{
    return sRandomBotFacade.GetValue(bot, TortoiseBots::kPoolGearTopUpLevelValue);
}

inline void PoolGearTopUpMarkLevelCompat(Player* bot, uint32 level)
{
    sRandomBotFacade.SetValue(bot, TortoiseBots::kPoolGearTopUpLevelValue, level);
}

inline uint32 PoolGearTopUpRunCompat(Player* bot, uint32 level)
{
    PlayerbotFactory factory(bot, level);
    return factory.TopUpPoolGear();
}
