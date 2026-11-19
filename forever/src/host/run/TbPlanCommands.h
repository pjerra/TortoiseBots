// Slice 5, host package H5: `.tb plan` (forever/docs/TB-COMMANDS.md section 9; spec FOREVER-RUN-S5S6-SPEC.md section 4.4).
//
//   .tb plan list [#token]
//   .tb plan <plan> info [#token]
//   .tb plan <plan> check <character> [#token]     (the character, a host bot or the sender's own, stands on the plan's map)
//   .tb plan reload [#token]
//
// All replies are grammar version 2 through the one encoder (TbH3.h), markers `plan-begin` / `plan-end` (addendum A2: named after the command; the verb is
// the end line's `verb=`). Core headers and the policy headers only: compiled with and without TORTOISEBOTS_SLICE1 (the default build answers
// `error code=engine-not-built`, as the slice-3 and slice-4 stubs do). Every handler runs on the world thread.
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace run
{
bool HandlePlan(ChatHandler* handler, Trinity::ChatCommands::Tail args);
}
}
