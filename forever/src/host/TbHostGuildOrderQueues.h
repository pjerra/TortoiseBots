#pragma once

// The guild gold refund queue (TBF #155): unspent guild gold an order still
// holds, including the stub a reassign leaves behind, is repaid to the guild
// bank at the next reachable banker. The deposit-only seam keeps the guild
// from losing gold the order never spent; the rules themselves are the pure
// policy in runtime/GuildOrderRefundPolicy.h.
#include <cstdint>

class Player;

namespace tbhost
{
namespace guild_orders
{

// World thread, from the bot manager's idle pass beside TickBotOrder: repay
// unspent guild gold at a guild banker in reach (walking to one within the
// order visit distance). A bot with nothing owed costs one store lookup.
void TickGuildOrderQueues(Player* bot);
// A bot left the world: its banker scan is forgotten, so a previous session
// never delays the next one.
void ForgetGuildOrderQueueSession(uint32_t botGuidLow);

} // namespace guild_orders
} // namespace tbhost
