// S1-R2: `.tb hear <bot> <sender> whisper|party <text>`, the test helper that sends a bot the chat line a real player would send.
// The line goes through the same Judge as a live one, with the `injected` marker (only the `not-real` rule is skipped), and into the same
// inbox, so the engine delivers it at the next tick exactly as it delivers a whispered order.
//
// Engine build only: the option-OFF build answers `error code=engine-not-built` (TbH3Off.cpp), because without an engine no bot has a
// brain to deliver to. The reply is grammar version 2 (forever/docs/TB-COMMANDS.md section 18).
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace chatin
{
bool HandleHear(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
