// The `trade` addon verb and headless trade-window driver (TBF #134 piece P134-C):
// `trade [<bot>]` over the TBM addon channel (`TbAddonChannel.h`), plus helpers
// for `item <bot> trade` and `item <bot> give` during an open trade.
#pragma once

#include <cstdint>
#include <string_view>

class Item;
class Player;

namespace TortoiseBots
{
namespace trade
{
void RegisterAddonVerbsTrade();
bool HandleItemTrade(Player* requester, std::string_view botName);
bool PlaceItemInOpenTrade(Player* requester, Player* bot, Item* item, std::uint8_t bag, std::uint8_t slot);
} // namespace trade
} // namespace TortoiseBots
