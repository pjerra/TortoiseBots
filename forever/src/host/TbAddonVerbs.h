// The passthrough `.bot` verbs over the addon channel (TBF #130 piece f-verbs): every `.bot` word the addon sends that the
// player layer already answers is passed to player::Dispatch with the reply sink set, and the captured lines become the TB
// wire reply (one Ack/Err plus the captured lines verbatim). Verbs without a player-layer handler are not registered: the
// channel answers those `unsupported` (f-chan).
#pragma once

namespace TortoiseBots
{
namespace addonverbs
{
// Registers add/logout/remove/summon/stats/who/maintenance/autogear. World thread, startup only, after addon::AddScripts().
void RegisterAddonVerbsPassthrough();
} // namespace addonverbs
} // namespace TortoiseBots
