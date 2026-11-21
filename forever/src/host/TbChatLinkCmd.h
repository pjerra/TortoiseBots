// S1-R1: `.tb link <text> [#token]`, the chat link parser at the console. It runs in BOTH builds (no engine, no bot): the same parser the bot
// brain reads links with on ForeverCore (compat/TbChatLinkPure.h, through the compat pair TbCompat_CL.h), so what it prints is what the
// engine gets. The reply is grammar version 2 (forever/docs/TB-COMMANDS.md section 17).
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace chatlink
{
bool HandleLink(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
