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
//   move-direct   x y z map: a straight MovePoint(generatePath=false) to the point, the stay position written there (the lead of a direct step)
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
    bool fromRun = false;
};

// Everything the queue could refuse later, asked now and changing nothing: engine present, the legacy actions the verb needs registered, queue not full,
// and for a unit verb the unit still valid (same map, alive, attackable, in pull reach). False with a refusal code and a reason (the `.tb order` codes:
// no-engine, queue-full, action-not-registered, no-party, no-pull-strategy, target-not-found, target-dead, not-attackable, too-far, no-entrance ...).
bool PreflightOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail);

// Preflight, then queue (the bot's engine runs it at its next tick). `seq` is the order's number for `h3::LastOrder`.
bool QueueOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail, uint32& seq);
}
}
