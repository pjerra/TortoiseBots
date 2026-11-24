// `.tb trace` and `.tb why` (run trace and why trace), the ForeverCore command glue of runtime/RunTrace.cpp and
// runtime/WhyTrace.cpp. Registered in the `.tb` table (TbScripts.cpp) in both builds: without the engine they answer
// `error code=engine-not-built` like the other engine-facing commands.
//
// `.tb trace start <run_id> [party [<leader>] | bot <Name>] [driver=<model>:<effort>:<run>]`
// `.tb trace stop [<run_id>]`
// `.tb trace status`
// `.tb why <bot> [seconds]`
//
// Every handler runs on the world thread (a console command: World::ProcessCliCommands; a GM chat command:
// World::UpdateSessions, both PROCESS_THREADUNSAFE, forever/docs/TB-COMMANDS.md section 1).
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace trace
{
bool HandleTrace(ChatHandler* handler, Trinity::ChatCommands::Tail args);
bool HandleWhy(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
