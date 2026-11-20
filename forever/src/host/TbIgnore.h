// `.tb ignore` (the leave-it-alone list): one memory-only list of mobs per
// commander, honoured by the engine's target picks and area spells through
// IsIgnoredMobCompat (compat/TbCompat_I13.h).
//
// `.tb ignore <bot|party [leader]> add <guid32hex> | remove <guid> | list | clear [#token]`
// (reply grammar version 2, TbH3.h: Enc/Rec/Reply). The owner is the bots'
// bound commander (`.tb master`), never the bots: every addressed bot must
// answer to the same commander. An explicit attack, pull, assist or focus
// order on a listed mob (TbOrders.cpp Run) takes it off the list; `.tb cc`
// on one is refused. Compiled with and without TORTOISEBOTS_SLICE1 (the
// default build answers `error code=engine-not-built`, as TbCc.cpp does).
// Threading: every function runs on the world thread (TbH3.h).
#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <cstdint>
#include <string>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3 { class Reply; }
namespace i13
{
// True while the bot's commander lists the unit (expiry and dead units the
// calling bot can see are pruned first). False when the bot has no commander.
bool IsIgnored(Player* bot, ObjectGuid guid);

// An explicit order names the unit: it leaves the bot's commander's list.
// True when an entry was dropped.
bool NoteExplicitTarget(Player* bot, ObjectGuid guid);

bool HandleIgnore(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // `.tb ignore ...` (see above)

// `.tb grammar selftest`: one example of every record this package writes.
void SelfTestRecords(h3::Reply& reply);
}
}
