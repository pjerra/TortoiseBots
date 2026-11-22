// `.bot maintenance <bot>` for owned bots (ticket #137, piece E): TB's
// `.bot maintenance` (commands/Inventory.cpp HandleMaintenance) on the TBF
// `.bot` surface. The eligibility rule and step list are TbUpkeepPure.h; the
// world work is here. The owner-tier command glue lives in TbPlayerCmd.cpp
// next to the other verbs (its Say/Refuse/resolve helpers are file-local
// there). Threading: world thread.
#pragma once

#include "host/player/TbUpkeepPure.h"

#include <cstdint>
#include <string>

class Player;

namespace TortoiseBots
{
namespace upkeep
{
// What one maintenance pass did, for the reply line.
struct Summary
{
    std::uint32_t grantedItems = 0;
    bool repaired = false;
};

// AiPlayerbot.OwnedBotMaintenanceEnabled (default 0). False without the engine.
bool IsEnabled();

// Reagents, bandages, hunter ammo, a free repair and a save. False when
// there is no live bot to work on. Potions, food, consumes and thrown have
// no Forever equivalent (TbUpkeepPure.h) and are skipped.
bool Run(Player* bot, Summary& summary);
std::string Describe(Summary const& summary);
} // namespace upkeep
} // namespace TortoiseBots
