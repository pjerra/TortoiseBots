#pragma once

#include "../commands/BotCommandQueue.h"

class Player;

namespace TortoiseBots {
namespace LuaBindings {

// The Lua API over TortoiseBots (docs/developers/lua-api.md): a read API (TB.GetBots, TB.GetBot, TB.GetRun) and
// one write, TB.Command, which goes through the same queue and the same `.bot` handlers as a typed command. It is
// bound through the core's ElunaCustomRegistry, in the world Lua state only, and exists only in a core built with
// Eluna (ENABLE_ELUNA); in any other build every function below is an empty shell.

// Adds the registrar to ElunaCustomRegistry. A static initializer in LuaBindings.cpp already does it before main:
// the world Lua state is made before ScriptMgr::Initialize runs RegisterScripts, so RegisterScripts is too late.
// This call only keeps the translation unit in the link and is safe to repeat.
void Register();

// World thread, from BotChatAdapter::DrainQueuedCommands: runs one BotCommandReply::Lua command as its requester
// and keeps its reply lines for Flush.
void ExecuteCommand(Player* requester, PendingBotCommand const& command);

// World thread: the requester's queue hit the per-requester cap and dropped commands (logged; a Lua script has
// no per-command answer for them).
void ReportDropped(Player* requester, DroppedBotCommands const& dropped);

// World thread, right after the drain: hands finished replies to their Lua callbacks (or the log), and drops
// callbacks that waited too long. One mutex lock and nothing else while nothing is pending.
void Flush();

} // namespace LuaBindings
} // namespace TortoiseBots
