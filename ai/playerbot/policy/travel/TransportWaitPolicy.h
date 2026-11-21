#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai
{

enum class TransportWaitStep : std::uint8_t
{
    Wait = 0,
    Board = 1,
    Leave = 2,
    GiveUp = 3,
    Approach = 4   // stopped but not yet on the deck: walk onto it, never jump
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
    float approachRange = 60.0f;
    bool hasDeckZ = true;
    float botZ = 0.0f;
    float deckZ = 0.0f;
    float maxDeckZDiff = 2.5f;
    bool approachPathFailed = false;
    std::uint32_t waitedMs = 0;
    std::uint32_t totalPathTimeMs = 0;
    std::uint32_t marginMs = 15000;
    std::uint32_t missingTimeoutMs = 30000;
};

inline bool IsOnTransportDeck(bool hasDeckZ, float botZ, float deckZ, float maxZDiff)
{
    if (!hasDeckZ)
        return false;
    return std::fabs(botZ - deckZ) <= maxZDiff;
}

inline bool IsApproachPathUsable(std::uint32_t pathType, std::size_t pointCount)
{
    constexpr std::uint32_t kNoPath = 0x08;
    return (pathType & kNoPath) == 0 && pointCount >= 2;
}

inline bool ShouldRescanTransport(
    bool transportFound,
    bool boarded,
    std::uint32_t waitedMs,
    std::uint32_t nextRescanMs)
{
    return !transportFound && !boarded && waitedMs >= nextRescanMs;
}

inline std::string RideOrderResult(
    bool active,
    bool boarded,
    bool arrived,
    bool offDeckRefused,
    std::string const& failCode)
{
    if (!failCode.empty())
        return "fail:" + failCode;
    if (arrived)
        return "ok";
    if (boarded)
        return "pending:aboard";
    if (offDeckRefused)
        return "pending:off-deck";
    if (active)
        return "pending:waiting";
    return {};
}

// Pure decision for waiting at a dock, boarding a stopped transport, leaving at the destination,
// or giving up after one full transport cycle plus margin (or missingTimeoutMs when cycle is unknown).
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

    if (in.approachPathFailed)
        return TransportWaitStep::GiveUp;

    bool const onDeck = IsOnTransportDeck(in.hasDeckZ, in.botZ, in.deckZ, in.maxDeckZDiff);
    if (in.transportFound && in.transportStopped && !in.atDestinationDock && in.distanceToDeck <= in.maxBoardRange && onDeck)
        return TransportWaitStep::Board;

    std::uint32_t const timeoutMs = (in.totalPathTimeMs > 0)
        ? (in.totalPathTimeMs + in.marginMs)
        : in.missingTimeoutMs;
    if (timeoutMs > 0 && in.waitedMs >= timeoutMs)
        return TransportWaitStep::GiveUp;

    if (in.transportFound && in.transportStopped && !in.atDestinationDock && in.distanceToDeck <= in.approachRange)
        return TransportWaitStep::Approach;

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
