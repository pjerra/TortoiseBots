// Abilities the world hands out through a skill value rather than a trainer
// spell: Dual Wield (spell 674), Block, Parry, and the weapon "Shoot" spells.
// The core learns them on character load (Player::LearnSkillRewardedSpells via
// Player::UpdateSkillsForLevel) but marks them dependent, and _SaveSpells writes
// only a non-dependent spell: the spell book has them for the session and loses
// them on the next load. Without 674 the core refuses every off-hand weapon
// (Player::CanEquipItem) and m_canDualWield stays false, which is per-session
// state nothing else restores.
#pragma once

#include <cstdint>

class Player;

namespace tbhost
{
namespace skillspells
{

// World thread only; idempotent, safe on every login and level change.
// Returns the number of spells learned or made persistent this call.
std::uint32_t Sync(Player* bot, char const* why);

} // namespace skillspells
} // namespace tbhost
