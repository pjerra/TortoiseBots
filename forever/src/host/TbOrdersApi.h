// Slice 6, package O6: the host's order queue as a CALL, for the run executor (run/TbRunOrders.cpp). `.tb order` (TbOrders.cpp) answers a command; the run
// leader's executor needs the same queue without a command line: it judges an order, asks every bot's pre-flight, queues, and later reads the bots' results
// (h3::LastOrder, TbH3.h). Defined in TbOrders.cpp with TORTOISEBOTS_SLICE1 only. World thread only.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <string>

class Player;

namespace TortoiseBots
{
namespace h3
{
// One queued order. Verbs: follow stay come attack pull endpull release (the `.tb order` verbs) and the run verbs
//   move-direct   x y z map [instance]: a straight MovePoint(generatePath=false) to the point, the stay position written there (the lead of a direct step);
//                 the executor refuses it when the bot is no longer in that Map instance
//   pullback      unit: `pullback my target`, the party hold, the tank's stay position written at its own spot (the pull anchor)
//   focus         unit: the skull on the unit for the whole fight, every bot attacks its skull target
//   rest          (none): drink / eat until full, out of combat
// `come` takes x y z map (an explicit spot). `fromRun`: the order is the run leader's, so its owner is the bot itself and the anchors of a pull hold are the
// tank's own spot, never the bound commander's (the commander may stand outside the instance).
struct OrderSpec
{
    std::string verb;
    std::string arg;
    ObjectGuid unit;
    float x = 0, y = 0, z = 0;
    uint32 map = 0;
    uint32 instance = 0;        // move-direct: the Map instance the point was made in (0 = not recorded); two copies of a dungeon share the map id
    bool fromRun = false;
    bool disengage = false;     // come: a fall back. Stops the swing, the cast and the pet's attack, and holds the bot passive on the way.
    bool placed = false;        // come: a placement's own order (`.tb cmd place`). Only this one keeps the combat-time behaviour: no Reset mid-fight and no `stay` in the combat state.
    uint32 passiveToken = 0;    // come: a walk back carries its row's passive owner; it adds `passive` only while that token still owns the record (0: the fall back's own come)
};

// Everything the queue could refuse later, asked now and changing nothing: engine present, the legacy actions the verb needs registered, queue not full,
// and for a unit verb the unit still valid (same map, alive, attackable, in pull reach). False with a refusal code and a reason (the `.tb order` codes:
// no-engine, queue-full, action-not-registered, no-party, no-pull-strategy, target-not-found, target-dead, not-attackable, too-far, no-entrance ...).
bool PreflightOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail);

// Preflight, then queue (the bot's engine runs it at its next tick). `seq` is the order's number for `h3::LastOrder`.
bool QueueOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail, uint32& seq);

// TBF-B1 (review finding 7): the bot's per-bot order state goes with it at logout: the order record and its transition ring, and the
// accept and role-check wait clocks (the three maps the review found growing with every bot guid). Called from BotMgr::OnPlayerLogout.
void ForgetBotRecords(ObjectGuid bot);

// The two role calls the dungeon finder's fill needs, the same ones `.tb role` makes. `BotNaturalRoles` is the bot's own spec mask
// (ai::BotRoles from AiFactory::GetPlayerRoles; 0 for no bot). `SetBotRole` commits a role now: the stored role, the role map, the
// strategy snapshots and the kit rebuild, then `follow` of its master; a tank order on a class this slice gives no tank kit is
// refused (empty string = done, else the refusal code and detail, as `.tb role`). World thread only.
uint8 BotNaturalRoles(Player* bot);
bool SetBotRole(Player* bot, uint8 role, std::string& code, std::string& detail);

// R6 (review item 1): the run's cancel. `CancelRunOrders` removes the orders the run's executor queued for this bot (OrderSpec::fromRun) and have not run yet
// (a hand-typed `.tb order` stays); the bot's last result becomes `fail:cancelled`. Returns how many. `EndRunPull` ends a pull the run started on this bot: the
// pull strategy's "end pull" (tank) and the bot's own pull join window / hold (every member), so no pull hold or pullback return outlives the run.
// Both are safe on a bot with no queue, no engine or no pull. World thread only.
uint32 CancelRunOrders(ObjectGuid bot);
void EndRunPull(Player* bot);

// Issue #15: clears the stay a `place`/`fall back` `come` order wrote, but only while that order (by `seq`) is still the bot's last one and it was a
// `come`: a bot whose commander changed is left to its new master, never given an order by the old one. Safe with no engine.
bool ReleasePlaceStay(Player* bot, uint32 seq);

// Issue #15: a fall back's `come` holds the bot passive so it stops fighting on the way. Releases that passivity (and the combat-state stay) once
// the bot reaches its fall-back spot, but only while the fall-back order (by `seq`) is still its last one. Safe with no engine.
bool ReleasePlaceDisengage(Player* bot, uint32 seq);

// Issue #15 F1: drops the emergency `passive` of the fall back's own `come` (by `seq`), whatever order owns the bot now, and only while no newer
// order that sets `passive` itself is the bot's last one: `hold`, `flee` and an owner's `+passive` keep their own, so a fleeing bot never starts
// fighting again. A passive the fall back did not set (the owner's, a hold's, a flee's) is never touched; the row's own walk-back `come` carries
// the record. Returns true when the row has no passive record left to serve (released, or never the row's), i.e. the row may stop disengaging;
// false only with no engine (a bot that is away). Safe with no engine.
bool ReleasePlacePassive(Player* bot, uint32 seq);

// Issue #15: an `attack`/`pull`/`pullback`/`resume` ended a fall back. Drops the emergency `passive` and `stay` ONLY while the stay position still
// is the row's spot (the placement's own), so a movement order that wrote its own stay is never touched. Never queues an order. Safe with no engine.
bool ReleasePlaceHold(Player* bot, float x, float y, float z);

// R6 (review item 5): registers (on) or drops (off) a run's leader: while registered, the bot's engine hook keeps it from following its commander when it is out of
// combat with no pull under way (it holds on `stay` where it stands). The run calls it at start and at its end.
void HoldRunLead(ObjectGuid bot, bool on);
}
}
