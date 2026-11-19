// Slice 6, host package C6: `.tb run` (forever/docs/TB-COMMANDS.md section 10; spec FOREVER-RUN-S5S6-SPEC.md section 5.4).
//
//   .tb run start <leader> <plan> [#token]
//   .tb run stop <leader> [reason words] [#token]
//   .tb run pause <leader> [#token]
//   .tb run resume <leader> [#token]
//   .tb run status <leader|all> [#token]
//   .tb run events <leader> [since=<seq>] [#token]
//
// All replies are grammar version 2 through the one encoder (TbH3.h): markers `run-begin` / `run-end` (addendum A2: named after the command, the verb is the end
// line's `verb=`), `error` on one line. Core headers and the policy headers only: compiled with and without TORTOISEBOTS_SLICE1 (the default build answers
// `error code=engine-not-built`, as the slice-3, slice-4 and `.tb plan` stubs do). Every handler runs on the world thread; the logic is in TbRun.cpp.
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace run
{
bool HandleRun(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
