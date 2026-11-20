// Commander port, package P1 (the binding card): FOREVER-COMMANDER-PORT-SPEC.md sections 3-5 and 10 (D1: h3's table stays the only binding owner;
// D2: character guids only, GroupIdW2 only as an in-memory key and the JSON `group`).
//
// A commander CARD is read from h3's table (bot -> commander, host/TbH3.h) and the run table (host/run/TbRun.h): one per commander, one per run (a run's
// own binding is a card with system=true, never a second one). Beside the table the module keeps only what the table cannot: when a card was first seen,
// the lead `.tb master party <lead> <commander>` named, and the log of the commander's last orders (the module's per-card OrderStore; the Turtle
// OrderLog's twenty-commander ring is not used here).
//
//   .tb commander status [<commander>] [#token]     every card (or the one commander's): binding, member and order records, and the run record of a card that runs
//
// The dashboard's COMMANDERS datagram is sent by the dash-runs collector (forever/src/dash/TbDashRuns.cpp, another package); it calls `AppendBindings` for
// the cards of every standing binding (see forever/docs/CMD-P1-NOTES.md for the two lines it needs).
//
// Core headers only here; the engine-facing parts are in TbCmdBinding.cpp behind TORTOISEBOTS_SLICE1. Without the option `.tb commander` answers
// "the engine is not built in" and the hooks do nothing. World thread only: plain data, no lock.
#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"
#include "host/cmd/TbCmdBindingPure.h"

#include <string>
#include <utility>
#include <vector>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace cmd
{
bool HandleCommander(ChatHandler* handler, Trinity::ChatCommands::Tail args);

// ---- hooks (one line each in the file that calls them) ----
// HandleMaster (TbOrders.cpp), after the bindings were written: `bots` are the bots the command addressed, `leader` the party leader the command named
// (null for a single bot or a `party` without a leader), `commander` the new commander (null: release).
void NoteMaster(std::vector<Player*> const& bots, Player* leader, Player* commander);
// HandleOrder (TbOrders.cpp), once per command: one entry per addressed bot, its refusal code (empty when it queued the order). Logged once per card the
// addressed bots stand on, each with its own counts and first refusal.
void NoteOrder(std::vector<std::pair<Player*, std::string>> const& results, std::string const& verb, std::string const& arg);
// The run tick (host/run/TbRun.cpp), after `run::Issue` answered: the order string the policy sent and the executor's answer.
void NoteRunOrder(ObjectGuid leader, std::string const& order, bool accepted, std::string const& code, std::string const& text);

// The cards as the dashboard's bindings, one per commander (a run's card included, without its `run` section: the run feed fills that, matched by the
// card's `lead` guid). `runLeadersShown` are the leaders (guid counters) of runs the caller reports itself (an ended run that lingers on the tab): a card
// whose lead is one of them is skipped, so the tab never shows a run twice. Appends to `out`.
void AppendBindings(std::vector<pure::CT::BindingInfo>& out, std::vector<uint32> const& runLeadersShown);
}
}
