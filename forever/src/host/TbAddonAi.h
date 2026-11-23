// Piece P135-C (TBF #135): `.tb ai`, `.tb cs`, and addon verb `ai`.
#pragma once

#include "ChatCommand.h"

namespace TortoiseBots
{
namespace aipreset
{

bool HandleAiCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);
bool HandleCsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);
void RegisterAddonVerbsAi();

} // namespace aipreset
} // namespace TortoiseBots
