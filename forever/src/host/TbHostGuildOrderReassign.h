#pragma once

// `.tb guildorder reassign <bot> <newBot>`: move one guild order to another bot
// (TBF #155). The permission is the same as giving orders; the target must be a
// module-owned bot online in the order's guild with no running or watched order of
// its own. The moved row keeps the order's goal and its already-delivered count;
// the work state (farm zone, supply leg, need list, worked clocks) starts fresh on
// the new bot, and the old bot keeps only unspent guild gold as a refund stub.
//
// World thread only (a `.tb` command).
#include <cstdint>
#include <string>

namespace tbhost
{
namespace guild_orders
{

bool ReassignOrder(uint32_t fromBotGuidLow, uint32_t toBotGuidLow,
    std::string& outCode, std::string& outDetail);

} // namespace guild_orders
} // namespace tbhost
