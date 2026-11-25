// Package P4 of the hire port (issue #298): the gear-tier sets and the contract unlocks. This header declares the
// Forever-side entry points the rest of the module needs besides the shared contract in TbHire.h; the catalog (tiers,
// quests, bosses, fees) is pure and lives in TbHireTiersPure.h, the core/engine half in TbHireTiers.cpp.
#pragma once

#include "host/hire/TbHire.h"

namespace TortoiseBots
{
namespace hire
{

// Loads the tier sets (`tortoise_bots_hire_gear`) once at world start; a missing table leaves every tier without a
// set, which is a valid state (the hire keeps its levelled kit). Called from the module's world startup.
void LoadAtStartup();

// Registers this package's PlayerScript: the contract kill credit (creature kills, login/logout/delete of the kill
// store). Called from AddSC_tortoisebots.
void RegisterScripts();

// Hire fee multiplier for a tier (the `AiPlayerbot.HireGearTierCostMultipliers` row, index clamped to 0..6). P2's
// QuoteFee multiplies the base quote by this.
float TierCostMultiplier(uint8 gearTier);

// Restores the quest-log counters of the unlock quests a player has open from the kills stored for the character.
// Runs on login, before the contracts page renders, and is what P3's recruiter should call when a contract quest is
// accepted (the core has no PlayerScript quest-accept hook). Cheap when no contract quest is open.
void SyncContracts(Player* player);

} // namespace hire
} // namespace TortoiseBots
