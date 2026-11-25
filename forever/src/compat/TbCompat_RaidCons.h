// ForeverCore side of the both-core helpers for the raid consumables (issue #22): the mode of one bot, the item of one
// category already in its bags, and the free-mode cast of one category, as the engine-side files ask them. Turtle side:
// ai/cmangos-compat-raidcons.h (every answer inert).
//
// The host module is the one owner of the config, the checked table and the casts (host/TbRaidConsumables.h); these inline
// functions are only the names the engine's side uses, so a Turtle build reads the same call sites as no-ops instead of
// needing a second implementation. The category is the policy's Category number (ai/playerbot/policy/raid/
// RaidConsumablePolicy.h), not a row: a free cast is always chosen from the host's checked index.
#pragma once

#include <cstdint>

#include "host/TbRaidConsumables.h"

// The mode of this bot: 0 off, 1 items, 2 free (ai/playerbot/policy/raid/RaidConsumablePolicy.h).
inline int RaidConsModeForCompat(Player* bot)
{
    return static_cast<int>(TortoiseBots::raidcons::ModeFor(bot));
}

// Mode 1: the item of that category already in the bot's bags, 0 for none. Never creates an item.
inline uint32 RaidConsBagItemForCompat(Player* bot, int category)
{
    if (category < 0 || category >= static_cast<int>(ai::raidcons::Category::Count))
        return 0;
    return TortoiseBots::raidcons::BagItemFor(bot, static_cast<ai::raidcons::Category>(category));
}

// Mode 2: a triggered cast of one checked index row's spell for that category (a weapon row: on the main-hand item). False
// when the bot's mode is not Free, the map or the bot's state refuses, the category is held, the index has no row for the
// bot's role and category at its level, or the core refused the cast.
inline bool RaidConsApplyFreeCompat(Player* bot, int category)
{
    if (category < 0 || category >= static_cast<int>(ai::raidcons::Category::Count))
        return false;
    return TortoiseBots::raidcons::ApplyFree(bot, static_cast<ai::raidcons::Category>(category));
}
