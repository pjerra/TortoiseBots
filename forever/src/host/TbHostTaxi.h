#pragma once

#include "ObjectGuid.h"

#include <cstdint>
#include <string>
#include <vector>

class Creature;
class Player;

namespace tbhost
{
namespace taxi
{

struct TaxiQuote
{
    bool ok = false;
    uint32_t fromNode = 0;
    uint32_t toNode = 0;
    uint32_t pathId = 0;
    uint32_t costCopper = 0;
    std::vector<uint32_t> nodes;
    std::string refuseCode;
};

struct FlightLinkInfo
{
    uint32_t fromNode = 0;
    uint32_t toNode = 0;
    uint32_t pathId = 0;
    uint32_t costCopper = 0;
    float distance = 0.0f;
};

// Finds the nearest alive, non-hostile flight master within range on the bot's current map.
Creature* NearestFlightMaster(Player const* bot, float range = 30.0f);

// Returns the taxi node id associated with a flight master for the bot's team (0 if none).
uint32_t NodeOfFlightMaster(Player const* bot, Creature const* npc);

// Learns the flight master's node via typed TaxiQueryAvailableNodes into the core handler.
bool Learn(Player* bot, Creature* npc, uint32_t* outNodeId = nullptr);

// Returns all taxi node ids currently known by the bot.
std::vector<uint32_t> KnownNodes(Player const* bot);

// Returns the count of taxi nodes currently known by the bot.
uint32_t KnownNodeCount(Player const* bot);

// Resolves a node argument (numeric id or case-insensitive substring of the node name).
uint32_t ResolveNodeArg(Player const* bot, std::string const& arg);

// Quotes a complete route and fare from the flight master's node to toNode.
TaxiQuote Quote(Player* bot, Creature* npc, uint32_t toNode);

// Activates a flight through the core's HandleActivateTaxiOpcode, paying the core's fare.
bool Fly(Player* bot, Creature* npc, uint32_t toNode, std::string* failCode = nullptr);

// Returns true while the bot is in flight.
bool InFlight(Player const* bot);

// Per-tick landing watch: logs [W5] fly-landed when UNIT_FLAG_ON_TAXI / IsInFlight clears.
void TickLandingWatch(Player* bot);

// Builds runtime flight links from the core's taxi DB2 stores for classic maps (0, 1).
std::vector<FlightLinkInfo> BuildRuntimeFlightLinks(bool forAlliance);

} // namespace taxi
} // namespace tbhost
