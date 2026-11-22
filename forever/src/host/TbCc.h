// Slice 8, host package H8: crowd control and strategy setup (FOREVER-SLICE-MAGE-ROGUE-SPEC section 6; forever/docs/SLICE8-H-NOTES.md).
//
// `.tb cc <bot> <mark> <unit guid | target> [family]` and `.tb cc <bot> clear [<mark> <unit guid>]` give one bot a crowd-control mark; `.tb strategy <bot>
// co|nc <+name,-name ...>` changes the bot's strategies (test setup, tag SETUP). Both answer in reply grammar version 2 through the ONE encoder (TbH3.h:
// Enc/Rec/Reply), refuse a bad `#token` first (RequireBuilt), and say only what was read back from the world after the change.
//
// ApplyCcOrder / ClearCcOrder are the same work as a CALL (package G8's run verbs `cc <mark> <guid> <bot> [family]` and `cc clear` use them). They are the
// REWRITE of Turtle's RunCcOrder (commands/Action.cpp:581-835) and `.bot cmd cc` (commands/Commander.cpp:6870-6950), not a port: no cast is started here
// (the mage's `cc` strategy polymorphs the `rti cc` unit when the fight starts), nothing is saved to a database (the host keeps no bot store).
//
// Compiled with and without TORTOISEBOTS_SLICE1 (the default build answers `error code=engine-not-built`, as TbH4.cpp does).
// Threading: every function runs on the world thread (TbH3.h).
#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <string>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3 { class Reply; class Rec; }
namespace h8
{
// Put a crowd-control mark on `unit` for `bot`: the raid icon `mark` is set on the bot's party (changed by `leader`, else by the bot), the bot's `rti cc`
// is `mark` and its `run cc family` is `family` ("" or "none" = no family gate), every other party bot that held `mark` as its crowd-control mark loses it
// (exclusive ownership, as the legacy order), and every damage bot of the party that focused `mark` (`rti`) is moved to skull. The change is READ BACK and
// undone completely when it does not hold. True on success. On false `code` is one of: engine-not-built no-engine bad-mark bad-family no-party
// target-not-found bad-target target-dead not-attackable failed, and `detail` (when not null) says why. `mark` is a raid mark word (star circle diamond
// triangle moon square cross); skull is the party's kill mark and never a crowd-control mark.
bool ApplyCcOrder(Player* leader, Player* bot, std::string const& mark, ObjectGuid unit, std::string const& family, std::string& code, std::string* detail = nullptr);

// Reset the bot's `rti cc` and `run cc family` to "none". When `mark` and `unit` are given (non-empty) the party's icon `mark` is removed, but ONLY while it
// still holds `unit` (a later order may have moved it); `iconRemoved` says whether it was. With neither (G8) the icon of the bot's OWN crowd-control mark
// (`rti cc`, read before the reset) is removed whatever it holds, so a bare `clear` leaves no icon behind. A mark with no unit is a values-only reset, so it
// needs no party (a bot that left the group is still taken off its run mark). Every write is read back and undone completely when it does not hold: a refused
// clear changes nothing. Codes as above (engine-not-built no-engine bad-mark no-party failed; no-party only when a named unit's icon must be looked up).
bool ClearCcOrder(Player* leader, Player* bot, std::string const& mark, ObjectGuid unit, std::string& code, std::string* detail = nullptr, bool* iconRemoved = nullptr);

// TB's CC_ASSIGN gate (commands/Info.cpp:495): a bot is listed only while it
// holds the "cc" combat strategy. Fresh bots read "rti cc" moon by default,
// so the mark alone must not list them.
bool HasCombatCcStrategy(Player* bot);

// The bot's `rti cc` and `run cc family` ("" for "none" or when the bot has no engine), as `.tb state` reads them.
void CcOrderValues(Player* bot, std::string& mark, std::string& family);

// Crowd-control permissions (`.tb cc <bot|party [leader]> [class] allow|deny <kind>... | list`): the memory-only
// per-(commander account, class) store of denied kinds. A class of 0 is the all-classes entry an order without a
// class word writes; a missing entry reads as fear denied. The host pushes each bot's effective mask into its engine,
// whose cast check then skips a denied spell and moves the rotation on.
uint32 CcDeniedForOwner(uint32 owner, uint8 botClass);
uint64 CcDeniedVersion();
uint32 CcOwnerOf(Player* bot, Player* sender);
void CcSetDenied(uint32 owner, uint8 botClass, uint32 mask);
void CcSyncBot(Player* bot);

bool HandleCc(ChatHandler* handler, Trinity::ChatCommands::Tail args);         // .tb cc <bot> <mark> <unit guid|target> [family] | .tb cc <bot> clear [<mark> <unit guid>] [#token]
bool HandleStrategy(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // .tb strategy <bot> co|nc <+name,-name ...> [#token]   (SETUP)

// Defined in the slice build only (TbPartyState.cpp / TbH3.cpp call them).
// `.tb state` keys appended to every bot record that has an engine: rticc= (the bot's crowd-control mark, empty for "none"), ccfam= (the run's family gate, empty
// for "none"), held=<creature entry>:<mark> (only while the unit on that mark exists and a breakable crowd control holds it now), combo= (a rogue's combo points).
void AppendCcState(h3::Rec& rec, Player* bot);
// `.tb grammar selftest`: one example of every record this package adds.
void SelfTestRecords(h3::Reply& reply);
}
}
