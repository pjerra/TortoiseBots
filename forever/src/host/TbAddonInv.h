// The `inv`/`inventory` and `item` addon verbs (TBF #134 pieces P134-A/P134-B):
// TB's `.bot inv` snapshot and `.bot item <bot> equip|unequip/give/trade` moves
// (TortoiseBots/commands/Inventory.cpp:158-228,353-600) over the TBM channel.
// Pure parsing and formatting live in `TbAddonInvPure.h`.
#pragma once

#include <string_view>

class Player;

namespace TortoiseBots
{
namespace inv
{
// Registers the `inv`, `inventory` and `item` verbs and earns the
// `inventory` capability (BotPanel shows the bag button once the server
// answers with it). World thread, startup only, after addon::AddScripts().
// The `item` verb answers equip/unequip/give here: `trade` routes to the
// trade-window piece (i-trade, host/TbAddonTrade.h), and `give` rides its
// open window while one is up, else the direct move below.
void RegisterAddonVerbsInv();
// One full TBM:INV_* snapshot of the bot, the lines BotPanel parses
// (INV_BEGIN copper, INV_EQ worn + bags, INV_ITEM every carried item,
// INV_END free/total). i-trade reuses this after its trade-window moves.
void SendInventorySnapshot(Player* requester, Player* bot);
// The `item` tail: server-side equip/unequip/give with TB's ACTION_ACK/ERR
// shapes plus a fresh snapshot; `trade` routes to the trade-window piece.
void HandleItemVerb(Player* requester, std::string_view args);
} // namespace inv
} // namespace TortoiseBots
