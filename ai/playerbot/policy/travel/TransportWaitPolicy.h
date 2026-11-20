#pragma once

#include <cstdint>
#include <vector>

namespace ai
{

enum class TransportWaitStep : std::uint8_t
{
    Wait = 0,
    Board = 1,
    Leave = 2,
    GiveUp = 3
};

struct TransportWaitInput
{
    bool mapReady = false;
    bool onBoard = false;
    bool transportFound = false;
    bool transportStopped = false;
    bool atDestinationDock = false;
    float distanceToDeck = 0.0f;
    float maxBoardRange = 15.0f;
    std::uint32_t waitedMs = 0;
    std::uint32_t totalPathTimeMs = 0;
    std::uint32_t marginMs = 15000;
};

// Pure decision for waiting at a dock, boarding a stopped transport, leaving at the destination,
// or giving up after one full transport cycle plus margin.
inline TransportWaitStep DecideTransportWait(TransportWaitInput const& in)
{
    if (!in.mapReady)
        return TransportWaitStep::Wait;

    if (in.onBoard)
    {
        if (in.transportFound && in.transportStopped && in.atDestinationDock)
            return TransportWaitStep::Leave;
        return TransportWaitStep::Wait;
    }

    if (in.transportFound && in.transportStopped && !in.atDestinationDock && in.distanceToDeck <= in.maxBoardRange)
        return TransportWaitStep::Board;

    if (in.totalPathTimeMs > 0 && in.waitedMs >= in.totalPathTimeMs + in.marginMs)
        return TransportWaitStep::GiveUp;

    return TransportWaitStep::Wait;
}

// Type-11 animated transports (e.g. Deeprun Tram) pause at stations via consecutive animation
// nodes that share identical coordinates.
inline bool IsTransportAnimPaused(
    float x1, float y1, float z1,
    float x2, float y2, float z2)
{
    constexpr float kEps = 0.001f;
    float const dx = x1 - x2;
    float const dy = y1 - y2;
    float const dz = z1 - z2;
    return (dx * dx + dy * dy + dz * dz) <= (kEps * kEps);
}

// A boarded rider has left the start dock only after changing maps or moving past minLeaveDist
// while the transport is in motion (prevents immediate Leave if boarded near maxBoardRange).
inline bool HasLeftStartDock(
    bool sameMap,
    bool transportStopped,
    float distFromStart,
    float minLeaveDist)
{
    if (!sameMap)
        return true;
    return !transportStopped && distFromStart > minLeaveDist;
}

// Only transports whose routes stay within classic maps (0, 1, or 369 for the tram if present)
// are allowed by default; custom transports require an explicit allow-list entry.
inline bool IsAllowedClassicTransportRoute(
    std::uint32_t entry,
    std::vector<std::uint32_t> const& legMapIds,
    std::vector<std::uint32_t> const& customAllowedEntries,
    bool allowTramMap)
{
    for (std::uint32_t allowed : customAllowedEntries)
        if (allowed == entry)
            return true;

    if (legMapIds.empty())
        return false;

    for (std::uint32_t mapId : legMapIds)
    {
        if (mapId == 0 || mapId == 1)
            continue;
        if (allowTramMap && mapId == 369)
            continue;
        return false;
    }
    return true;
}

} // namespace ai
