// Automatic level-up and login talent spending for ForeverCore bots via TraitConfig.
// Pure build table and spec selection live in host/TbTalentsPure.h.
#pragma once

#include <cstdint>

#include "ChatCommand.h"

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace talents
{

// TortoiseBots.AutoTalentsOnLevel (default 1), read by OnConfigLoad.
void LoadConfig();
bool Enabled();

// TortoiseBots.Pool.CensusSpec (default 1): a pool bot's spec tab is rolled with the live server's spec shares
// (TbTalentsPure.h); 0 is the pool's own balanced roll. The dashboard's spec census uses the same answer.
bool PoolCensusSpecEnabled();

// Logs per-class build-step -> TraitNode mapping counts at world startup.
void LogStartupSelfCheck();

// Spends any free talent points on the bot's active combat TraitConfig following the picked spec build.
// Never resets existing spent points. Returns the number of points newly spent.
uint32_t Sync(Player* bot, char const* why);

// `.tb talents <bot>`: prints spent/free/spec and applies any free points now.
bool HandleTalentsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);

} // namespace talents
} // namespace TortoiseBots
