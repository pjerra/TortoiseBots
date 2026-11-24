// Slice 4, host package H4: the commands of "instance and death" (forever/docs/TB-COMMANDS.md, section 8; forever/docs/SLICE4-H-NOTES.md).
//
// `.tb trigger`, `.tb enter` (entering a dungeon as a group through its area trigger), `.tb instance <leader> info|reset|leave`, `.tb tele`, `.tb revive`,
// `.tb kill`, `.tb gm`. All of them answer in reply grammar version 2 through the ONE encoder (TbH3.h: Enc/Rec/Reply). Core headers only; compiled with and
// without TORTOISEBOTS_SLICE1 (the default build answers `error code=engine-not-built`, as the slice-3 stub does).
//
// Threading: every handler runs on the world thread (TbH3.h).
#pragma once

#include "ChatCommand.h"

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3 { class Rec; class Reply; }
namespace h4
{
// One teleport of `source` next to `dest` (the destination's map; into an instance: the destination's instance). This is the ONE teleport path:
// `.tb tele <character> to <character>` and `.bot summon` both call it, so the map, instance, combat and death rules exist once (player-command
// spec D-8). True = the core accepted the teleport; the record carries status=started|refused, and on a refusal code= and msg= (grammar v2, TbH3.h).
bool TeleportBotNextTo(Player* source, Player* dest, bool revive, h3::Rec& rec);
// `.tb grammar selftest`: one fixed example of every record kind and key this package adds (called by h3::HandleGrammar; defined in the slice build only).
void SelfTestRecords(h3::Reply& reply);

bool HandleTrigger(ChatHandler* handler, Trinity::ChatCommands::Tail args);    // .tb trigger <area trigger id> [#token]
// .tb enter <bot|party [leader]> [trigger <id> | map <id>] [direct] [#token]   (the older `.tb enter <character> <map id>` is TbScripts.cpp's)
bool HandleEnter(ChatHandler* handler, Trinity::ChatCommands::Tail args);
bool HandleInstance(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // .tb instance <leader> info [map <id>] | reset | leave [#token]
// .tb tele <character> to <character> [revive]   |   .tb tele <bot|party [leader]> <map> <x> <y> <z> [o] [revive]   [#token]
bool HandleTele(ChatHandler* handler, Trinity::ChatCommands::Tail args);
bool HandleRevive(ChatHandler* handler, Trinity::ChatCommands::Tail args);     // .tb revive <bot|party [leader]> [#token]   (PRACTICE)
bool HandleKill(ChatHandler* handler, Trinity::ChatCommands::Tail args);       // .tb kill <bot|party [leader]> [#token]
bool HandleGm(ChatHandler* handler, Trinity::ChatCommands::Tail args);         // .tb gm <character> on|off [#token]
}
}
