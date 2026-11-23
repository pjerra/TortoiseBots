// The `totem` addon verb (TBF #133 piece c-totem): `totem <bot|party|raid>
// <slot> <totem|auto|none> | set | mode | drop | status` with TB's grammar
// (TortoiseBots/commands/Totem.cpp) and TB's TBM:TOTEM + ACTION_ACK/ERR
// lines (BotTotem.lua). Permission is the player tier Owner per target bot
// (host/player/TbPlayerAuthPure.h); the orders live in the bot AIs'
// `manual saved string` values, which the shaman triggers/actions consult
// first (shared code, already compiled), so the next fight drops the loadout.
#pragma once

namespace TortoiseBots
{
namespace totem
{
// Registers the `totem` verb and earns the `totem` capability (FakeServer's
// FAKE_CAPS advertises it; BotTotem.lua lights up once the server answers).
// World thread, startup only, after addon::AddScripts().
void RegisterAddonVerbsTotem();
} // namespace totem
} // namespace TortoiseBots
