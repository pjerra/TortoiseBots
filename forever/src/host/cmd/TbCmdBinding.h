// Commander port, package P1 (the binding card) and P2 (binding lifecycle and persistence): see TbCmdBinding.h for the card, TbCmdBindingLifePure.h
// for the decisions and TbCmdBindingStore.h for the opt-in characters-database row.
//
// A commander CARD is read from h3's table (bot -> commander, host/TbH3.h) and the run table (host/run/TbRun.h): one per commander, one per run (a run's
// own binding is a card with system=true, never a second one). Beside the table the module keeps only what the table cannot: when a card was first seen,
// the lead `.tb master party <lead> <commander>` named, the flags the bind saw, the followers whose manual follow target the bind pointed at the lead
// (with their own value, put back on release) and, only when TortoiseBots.Commander.KeepBindings is on, the stored row that restores the binding after a
// restart.
//
//   .tb commander status [<commander>] [#token]     every card (or the one commander's): binding, member and order records, and the run record of a card that runs
//
// The lifecycle runs from hooks, not a poll: GroupScript's add/remove/leader/disband (TbScripts.cpp) end a binding whose lead, group or leadership
// changed, PlayerScript's login/logout handles the commander's grace and the lead's departure, UnitScript's death marks a dead lead, and the 1 Hz
// `Maintain` only enforces the two timers and restores rows whose last member just came back (the Turtle release sweep's role).
//
// The dashboard's COMMANDERS datagram is sent by the dash-runs collector (forever/src/dash/TbDashRuns.cpp, another package); it calls `AppendBindings`
// for the cards of every standing binding and `FillRunOrders` for the run cards it builds itself (forever/docs/CMD-P2-NOTES.md).
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
class Group;
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
// The run's order executor (host/run/TbRunOrders.cpp `Issue`), once per order it answered: the order string the policy sent and the answer.
void NoteRunOrder(ObjectGuid leader, std::string const& order, bool accepted, std::string const& code, std::string const& text);

// ---- P2 lifecycle hooks (TbScripts.cpp rows) ----
void NoteGroupAdd(Group* group, ObjectGuid guid);                                    // GroupScript::OnAddMember
void NoteGroupRemove(Group* group, ObjectGuid guid);                                 // GroupScript::OnRemoveMember (before the slot goes)
void NoteGroupLeader(Group* group, ObjectGuid newLeader);                            // GroupScript::OnChangeLeader
void NoteGroupDisband(Group* group);                                                 // GroupScript::OnDisband
void NoteDeath(Player* player);                                                      // UnitScript::OnUnitDeath (a player, any killer)
void NoteLogin(Player* player);                                                      // PlayerScript::OnLogin (commander back, or a member back)
void NoteLogout(Player* player);                                                     // PlayerScript::OnLogout (commander grace, lead departure, shutdown)
void Maintain();                                                                     // once a second: the timers, and the stored rows whose party is back
void LoadBindings();                                                                 // world start / config reload: the key, the graces and the stored rows

// The legacy host stand-ins (TbHostOwnership.cpp, commands/BotCommands.h): the commander commands this bot (h3 binds it, it stands in the lead's
// live group, and no group hook has recorded the binding's release for the next tick), and the bot is a member of the commander's own binding group.
// World thread, cheap, no database.
bool IsCommandedBy(Player* commander, Player* bot);
bool IsInOwnBindingGroup(Player* commander, Player* bot);
// P3: resolves `commander`'s standing binding (lead guid + the bound member guids that are still in the lead's live party). False when `commander`
// commands no bots, or a group hook recorded its release for the next tick.
bool ResolveBinding(ObjectGuid commander, ObjectGuid& lead, std::vector<ObjectGuid>& members);

// The cards as the dashboard's bindings, one per commander (a run's card included, without its `run` section: the run feed fills that, matched by the
// card's `lead` guid). `runLeadersShown` are the leaders (guid counters) of runs the caller reports itself (an ended run that lingers on the tab): a card
// whose lead is one of them is skipped, so the tab never shows a run twice. Appends to `out`.
void AppendBindings(std::vector<pure::CT::BindingInfo>& out, std::vector<uint32> const& runLeadersShown);
// Fills the `orders` of the run bindings the collector built itself from the card of the same commander (the run feed leaves them empty).
void FillRunOrders(std::vector<pure::CT::BindingInfo>& out);

// P5: the standing cards as the fight monitor feeds them, world thread, once a second: commander, the lead (bots[0]) and the bots with the class,
// role and subgroup the card shows. A projection of the same read `.tb commander status` makes, without its view work: the monitor needs the
// card's group and its members, nothing the card adds for a human reader.
std::vector<pure::CardView> StandingCards();
}
}
