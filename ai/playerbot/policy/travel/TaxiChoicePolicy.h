#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ai
{

enum class TaxiChoiceResult : std::uint8_t
{
    Fly = 0,
    Walk = 1
};

struct TaxiChoiceDecision
{
    TaxiChoiceResult action = TaxiChoiceResult::Walk;
    std::string refuseCode;
};

// Pure decision for whether a bot may take a flight path or must walk.
// Never flies from or to an unknown node, never flies without a multi-node route,
// never flies while busy, and never flies when the quoted fare exceeds the bot's copper.
inline TaxiChoiceDecision DecideTaxiChoice(bool fromKnown, bool toKnown, std::size_t hopCount,
    std::uint64_t fareCopper, std::uint64_t botMoneyCopper, bool inCombat, bool rootedOrStunned)
{
    if (!fromKnown || !toKnown)
        return { TaxiChoiceResult::Walk, "unknown-node" };
    if (hopCount < 2)
        return { TaxiChoiceResult::Walk, "no-route" };
    if (inCombat || rootedOrStunned)
        return { TaxiChoiceResult::Walk, "busy" };
    if (fareCopper > botMoneyCopper)
        return { TaxiChoiceResult::Walk, "money" };
    return { TaxiChoiceResult::Fly, {} };
}

inline bool IsClassicContinentMap(std::uint32_t mapId)
{
    return mapId == 0 || mapId == 1;
}

struct PureFlightNode
{
    std::uint32_t nodeId = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool alliance = false;
    bool horde = false;
};

struct PureFlightEdge
{
    std::uint32_t fromNode = 0;
    std::uint32_t toNode = 0;
    std::uint32_t pathId = 0;
    std::uint32_t costCopper = 0;
};

struct PureFlightLink
{
    std::uint32_t fromNode = 0;
    std::uint32_t toNode = 0;
    std::uint32_t pathId = 0;
    std::uint32_t costCopper = 0;
    float distance = 0.0f;
};

// Pure builder for runtime flight links from core taxi nodes and paths (classic maps 0/1 only).
// W4's travel graph will consume this once merged; kept core-free so it can be unit-tested standalone.
inline std::vector<PureFlightLink> BuildClassicFlightLinks(
    std::vector<PureFlightNode> const& nodes,
    std::vector<PureFlightEdge> const& edges,
    bool forAlliance)
{
    auto findNode = [&](std::uint32_t id) -> PureFlightNode const*
    {
        for (PureFlightNode const& n : nodes)
            if (n.nodeId == id)
                return &n;
        return nullptr;
    };

    std::vector<PureFlightLink> out;
    for (PureFlightEdge const& e : edges)
    {
        if (!e.pathId || e.fromNode == e.toNode)
            continue;
        PureFlightNode const* from = findNode(e.fromNode);
        PureFlightNode const* to = findNode(e.toNode);
        if (!from || !to)
            continue;
        if (!IsClassicContinentMap(from->mapId) || !IsClassicContinentMap(to->mapId))
            continue;
        bool const fromFactionOk = forAlliance ? from->alliance : from->horde;
        bool const toFactionOk = forAlliance ? to->alliance : to->horde;
        if (!fromFactionOk || !toFactionOk)
            continue;
        float const dx = to->x - from->x;
        float const dy = to->y - from->y;
        float const dz = to->z - from->z;
        float const dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        out.push_back({ e.fromNode, e.toNode, e.pathId, e.costCopper, dist });
    }
    return out;
}

} // namespace ai
