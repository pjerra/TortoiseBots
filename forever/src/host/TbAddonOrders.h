#pragma once

namespace TortoiseBots
{
namespace addonorders
{
// Registers the `order` verb and the guild-order capability tokens on the
// addon channel. No-op in a build without the legacy slice, where the order
// store and the `.tb guildorder` verbs do not exist.
void RegisterAddonVerbsOrders();
} // namespace addonorders
} // namespace TortoiseBots
