// Abilities the world hands out through a skill value rather than a trainer
// spell: Dual Wield (spell 674), Block, Parry, and the weapon "Shoot" spells.
// The legacy PlayerbotFactory::EnsureSkillRewardedSpells does this on Turtle,
// but its file is not in the ForeverCore slice; the core's own pass
// (Player::LearnSkillRewardedSpells) only runs when a skill value is written,
// so a seeded bot can own the skill without the spell. Without 674 the core
// refuses every off-hand weapon (Player::CanUseItem) and m_canDualWield stays
// false, which is per-session state nothing else restores.
#pragma once

#include <cstdint>

class Player;

namespace tbhost
{
namespace skillspells
{

// World thread only; idempotent, safe on every login and level change.
// Returns the number of spells taught this call.
std::uint32_t Sync(Player* bot, char const* why);

} // namespace skillspells
} // namespace tbhost
