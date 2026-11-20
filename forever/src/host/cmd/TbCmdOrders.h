// Commander port, package P3: `.tb cmd <verb> ... [#token]` — routes a bound commander's orders through the lifted run judge (`run::Issue`).
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace cmd
{
bool HandleCmd(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
