#pragma once

// Grants missing Classic profession tools (into a free bag slot, once, no
// duplicates) to pool/free bots on login and level-up. Never touches real
// players or owned alts.

#include <cstdint>

class Player;

namespace TortoiseBots
{
namespace autotools
{

// TortoiseBots.AutoTools (default 1), read by OnConfigLoad.
void LoadConfig();
bool Enabled();

// Grants any missing tools for the bot's learned professions; returns how many
// items were added. `why` labels the log line ("login" or "level").
uint32_t Sync(Player* bot, char const* why);

} // namespace autotools
} // namespace TortoiseBots
