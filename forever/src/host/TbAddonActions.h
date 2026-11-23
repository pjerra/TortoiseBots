// The `action` addon verb (TBF #130 piece f-actions): `action <intent>`
// drives the bots from the addon over the TBM channel, with TB's exact
// intents, scope rule and ACK/ERR lines. The grammar, scope rule and support
// table are the core-free host/TbAddonActionsPure.h; this header exposes the
// one registration the channel calls from TbScripts.cpp.
#pragma once

namespace TortoiseBots
{
namespace action
{
// Registers the `action` verb and advertises `action:<intent>` for every
// intent the live half really runs. Nothing with the engine off.
void RegisterAddonVerbsActions();
} // namespace action
} // namespace TortoiseBots
