#pragma once

// Automatic auction-house market sweep: a free bot with sellable loot walks to a faction auctioneer and posts
// it with the priced seller action (`ah vendor`). The sweep is paced by AiPlayerbot.AhMarketInterval and
// AiPlayerbot.AhMarketBatchSize and gated by TortoiseBots.World.Auction. Implemented in TbHostAhMarket.cpp.
//
// The header stays core-free so TbBotMgr.cpp can include it in the build without the engine slice.

#include "ObjectGuid.h"

#include <cstdint>

class Player;

namespace tbhost
{
namespace ah_market
{

// One per-bot tick from BotMgr. Cheap while disabled; the pass itself runs once per interval.
void Tick(Player* bot, uint32_t diffMs);

// True while the sweep is walking this bot to an auctioneer; the host pauses its engine meanwhile.
bool HasActiveErrand(Player const* bot);

void ClearBotAhMarket(ObjectGuid const& botGuid);

} // namespace ah_market
} // namespace tbhost
