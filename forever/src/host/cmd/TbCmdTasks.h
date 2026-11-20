// Commander P6: `.tb cmd loot|open|use|gossip|state|lootmethod` (plus `ritual` / `scout` -> `unsupported`).
// Shares the task engine in `forever/src/host/run/TbRunTasks.{h,cpp}` with `.tb run`. World thread only.
#pragma once

#include "ChatCommand.h"
#include "host/TbH3.h"

#include <string>

class ChatHandler;

namespace TortoiseBots
{
namespace cmd
{
bool HandleCmd(ChatHandler* handler, Trinity::ChatCommands::Tail args);

// Seam for A (Commander P3): dispatches `.tb cmd <verb>` when <verb> is a P6 task verb
// (`loot`, `open`, `use`, `gossip`, `state`, `lootmethod`, `ritual`, `scout`).
// Returns true if the verb was handled (accepted or refused); returns false without writing a reply if <verb> is not a P6 verb.
bool TryHandleTaskVerb(ChatHandler* handler, h3::Words const& args, std::string const& verb, size_t argStart);
}
}
