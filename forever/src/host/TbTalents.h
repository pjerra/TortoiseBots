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

// Logs per-class build-step -> TraitNode mapping counts at world startup.
void LogStartupSelfCheck();

// Spends any free talent points on the bot's active combat TraitConfig following the picked spec build.
// Never resets existing spent points. Returns the number of points newly spent.
uint32_t Sync(Player* bot, char const* why);

// `.tb talents <bot>`: prints spent/free/spec and applies any free points now.
bool HandleTalentsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);

} // namespace talents
} // namespace TortoiseBots
