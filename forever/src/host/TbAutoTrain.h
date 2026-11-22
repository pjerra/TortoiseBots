// Automatic class-spell training for pool and hired bots on login and level-up, shared with `.tb train <bot>`.
// Uses the core's class trainer data and class skill-line abilities with a cached per-class index.
#pragma once

#include "Trainer.h"
#include <cstdint>
#include <string>

class Player;

namespace tbhost
{
namespace autotrain
{

struct Tally
{
    uint32_t trainerTaught = 0;
    uint32_t lineTaught = 0;
    uint32_t trainers = 0;
    uint32_t passes = 0;
    uint32_t retailSkipped = 0;
    uint32_t removedCount = 0;
    std::string removed;
    Trainer::Trainer::FreeTeachTally trainerTally;

    uint32_t TotalTaught() const { return trainerTaught + lineTaught; }
};

// TortoiseBots.AutoLearnOnLevel (default 1), read by OnConfigLoad and OnWorldInitialized.
void LoadConfig();
bool Enabled();

// World thread only: caches class trainer ids and class skill-line spell ids per class.
void BuildIndex();

// Teaches `bot` every class spell it has reached the level for, free, without an NPC.
// Preserves any spell already known before this call (such as class-quest spells or talent spells).
uint32_t TeachClassSpells(Player* bot, Tally& out);

// Auto-learn hook for OnLogin / OnLevelChanged: gated on TortoiseBots.AutoLearnOnLevel and BotMgr::IsBot.
uint32_t Sync(Player* bot, char const* why);

} // namespace autotrain
} // namespace tbhost
