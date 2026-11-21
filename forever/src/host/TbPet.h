// Slice 11, package P11: `.tb pet` and the pet state keys (forever/docs/SLICE11-NOTES.md; the reply grammar is forever/docs/TB-COMMANDS.md and
// TB-STATE-GRAMMAR-V2).
//
// `.tb pet <bot> give|call|dismiss|show` and `.tb pet <bot> order <words>` act on ONE host bot's pet. `give` makes a hunter's first pet (TbPetFactory.h:
// GiveHunterPet), `call` casts Call Pet through the engine, `dismiss` puts the pet away (a hunter's by its dismiss spell, a warlock's by the pet command),
// `order` hands the words to the engine's chat `pet` action (`autocast torment`, `defensive`, `stay`...), `show` lists what the pet knows and how it is set.
// Every reply is read back from the world after the change (never what was asked), in reply grammar 2 through the ONE encoder (TbH3.h).
//
// Compiled with and without TORTOISEBOTS_SLICE1: the default build answers `error code=engine-not-built`, as TbH4.cpp and TbCc.cpp do.
// Threading: every function runs on the world thread (TbH3.h).
#pragma once

#include "ChatCommand.h"
#include "Define.h"

#include <string>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3 { class Reply; class Rec; }
namespace p11
{
bool HandlePet(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // .tb pet <bot> give|call|dismiss|show | order <words> [#token]

// Defined in the slice build only (TbPartyState.cpp / TbH3.cpp call them).
// `.tb state` keys appended to every bot record of a hunter or a warlock: pet=<entry>:<alive|dead>:<hp pct> (empty with no pet: grammar addendum A6 allows no `none` word; dead also when the body is gone and the stable
// row has Health 0), petreact=<passive|defensive|aggressive>, petauto=<spell ids on autocast>, loyal=<loyalty level 1-6> (a hunter's pet), ammo=<item>:<count>
// (a hunter), shards=<count> (a warlock).
void AppendPetState(h3::Rec& rec, Player* bot);
// `.tb grammar selftest`: one example of every record this package adds.
void SelfTestRecords(h3::Reply& reply);
}
}
