// Autonomous profession grants for pool bots reaching level 5 (two primary professions + First Aid + Cooking).
// Pure selection rules live in host/TbAutoProfsPure.h; this file applies them on the world thread.
#pragma once

#include <cstdint>

class Player;

namespace TortoiseBots
{
namespace autoprofs
{

// TortoiseBots.AutoProfessions (default 1), read by OnConfigLoad.
void LoadConfig();
bool Enabled();

// Grants primary pair + First Aid + Cooking if eligible (pool bot, level >= 5, missing skills).
// Returns count of skills taught.
std::uint32_t Sync(Player* bot, char const* why);

} // namespace autoprofs
} // namespace TortoiseBots
