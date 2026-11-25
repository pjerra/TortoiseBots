// ForeverCore side of the pool gear top-up helpers. Turtle side: ai/cmangos-compat-poolgear.h.
// Reached through ai/playerbot/TbCompatPoolGear.h.
#pragma once

#include "runtime/PoolGearTopUpPolicy.h"

class Player;

namespace tbs_poolgear
{
// host/TbHostPoolGear.cpp: the host's pool flag and the level marker the legacy facade kept in memory.
bool IsPoolBot(Player* bot);
bool IsHired(Player* bot);
uint32 LastLevel(Player* bot);
void MarkLevel(Player* bot, uint32 level);
uint32 TopUp(Player* bot, uint32 level);
}

// A Forever host bot is not a `random bot` of the legacy pool (host/TbHostBotManager.cpp answers IsRandomBot false);
// the pool scheduler's own flag (TbBotMgr.h, IsPoolBot) is the identity here.
inline bool PoolGearTopUpIsPoolBotCompat(Player* bot)
{
    return tbs_poolgear::IsPoolBot(bot);
}

// Hired companions keep the kit their player paid for: the ledger answers (host/hire/TbHireLifecycle.cpp); no guild bot
// is claimed on this host (runtime/ClaimLifecycle.cpp is not part of the slice and TbHostOwnership.cpp answers false).
inline bool PoolGearTopUpIsHiredCompat(Player* bot)
{
    return tbs_poolgear::IsHired(bot);
}

inline bool PoolGearTopUpIsClaimedCompat(Player*)
{
    return false;
}

inline uint32 PoolGearTopUpLastLevelCompat(Player* bot)
{
    return tbs_poolgear::LastLevel(bot);
}

inline void PoolGearTopUpMarkLevelCompat(Player* bot, uint32 level)
{
    tbs_poolgear::MarkLevel(bot, level);
}

inline uint32 PoolGearTopUpRunCompat(Player* bot, uint32 level)
{
    return tbs_poolgear::TopUp(bot, level);
}
