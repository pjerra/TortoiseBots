#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"

class Player;
class PlayerbotAI;

namespace TortoiseBots::onyxia
{
bool HandleCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);
void Tick(Player* bot, PlayerbotAI* ai, uint32 diff);
void Forget(ObjectGuid bot);
}
