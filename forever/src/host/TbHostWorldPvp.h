#pragma once

// Open-world PvP on ForeverCore: the PvP-flag share of free bots (TB runtime/BotManager.cpp:1544-1555) and the
// `world pvp` seek role rolled over a share of them (TB AiFactory.cpp:1195-1254 and RandomBotService.cpp:1994-2019).
// The rules are the core-free policy in runtime/policy/pvp/WorldPvpSeekPolicy.h; this layer only feeds it the host's
// facts and applies the answer to the engine. World-thread only, memory only.
//
// Hires are PvE-only unless AiPlayerbot.HireAllowPvP, so neither the flag share nor the role ever reaches a bot with a
// master: only a free host bot (BotMgr::IsFreeBot) is considered, and a master binding clears that flag first.
class Player;

namespace tbhost
{
namespace worldpvp
{

// PlayerScript::OnLogin for a bot: put a free bot of the PvP-flagged share into the /pvp preference the core keeps in
// PLAYER_FLAGS_IN_PVP (Player::InitPvP restores it after a relog). Idempotent; nothing for owned or hired bots.
void OnBotLogin(Player* bot);

// One per-bot pass: strip the role from a bot that stopped being eligible, roll it at login, re-roll it about every
// AiPlayerbot.WorldPvpSeekRerollMinutes (with jitter, never in combat). Cheap and inert without a brain.
void Tick(Player* bot);

// The engine rebuilt this bot's kit (a brain, a strategy reset): put back a role the last roll granted. The rebuild drops
// every strategy the kit does not name; a player's own `nc -world pvp` is not undone until the next rebuild or re-roll.
void ApplyGrantedRole(Player* bot);

// True when the bot holds the seek role on either engine (for `.tb where`-style diagnostics and the tests).
bool HoldsRole(Player* bot);

} // namespace worldpvp
} // namespace tbhost
