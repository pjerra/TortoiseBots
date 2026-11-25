// `.tb los` (#308): the SERVER's line of sight between two units, or a unit and a position, read-only.
//
//   .tb los <a> <b> [map=<id>]
//
// A side is a character/bot name (`FindOnline`), `target` (the caller's selection), a guid as the module prints it
// (32 hex digits from `.tb state`, or the core's `0x...`) or a position `x,y,z` on the other side's map; two positions
// need `map=<id>`. The answer is ONE line, whatever happened:
//
//   los a=<name|guid|x,y,z> b=<...> map=<id> los=<0|1> dist=<yards, one decimal> same_map=<0|1> in_world=<0|1>
//      [reason=<code>]
//
// The line is a stable interface for tools (the agent's plan scout records true sight with it; docs/play/commands/
// los-query.md). `reason=` is the refusal: `bad-argument`, `unknown-unit`, `not-in-world`, `different-maps` (a different
// map, or another instance of the same map: `same_map=1` needs one map AND one instance), `no-access` or `rate`; a key
// whose fact is unknown prints empty. A position with a unit side is on that unit's map and instance; a
// position-position query is answered on the first loaded copy of the named map and cannot name another instance.
//
// The sight call is the bots' own attack-target check: the "rti target" value filters through
// `bot->IsWithinLOSInMap(unit)` (ai/playerbot/strategy/values/RtiTargetValue.h:97) with the core's default
// LINEOFSIGHT_ALL_CHECKS, and the action behind "I dont see my rti attack target"
// (ai/playerbot/strategy/actions/AttackAction.cpp:163) answers when that value is null. This command makes the same call
// through compat/TbCompat_Unit.h (`IsWithinLOSInMapCompat(.., true)`), so the core's height offsets apply: the ray
// starts and ends at the player's z + collision height, or at a creature's hit-sphere point (ForeverCore
// src/server/game/Entities/Object/Object.cpp:534-558); a unit-to-position ray ends at the plain position. The query does
// NOT apply the attack gate's distance, friendliness or legality rules: it answers the geometry, and `dist=` lets the
// caller compare with its own sight distance.
//
// Access: the console and a GameMaster always; anyone else only for a unit of its own (its character, or a host bot in a
// run or party it leads or observes: the shared `.tb run` checks, CheckRunObserverAccess / CheckPartyLeadAccess) and
// never for another player's character. A GameMaster caller skips those checks (they grant anyway, and they log an INFO
// line), so a polling tool never floods the log. Rate: five queries a second per caller (host/TbLosQueryPure.h); console
// and SOAP have no character, so all tool callers share one bucket.
//
// Read-only: no movement, no target change, no combat state touched. Compiled with and without TORTOISEBOTS_SLICE1 (the
// default build answers `error code=engine-not-built`, as the other slice commands do). Threading: every function runs
// on the world thread (TbH3.h), so the per-caller bucket table needs no lock.
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace losq
{
bool HandleLos(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // `.tb los <a> <b> [map=<id>]` (see above)
}
}
