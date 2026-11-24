#pragma once

// Pure pacing and eligibility rules for the automatic auction-house market sweep. No core includes:
// forever/tools/test_ah_market_policy.cpp builds this header on its own.

#include <algorithm>
#include <cstdint>

namespace tbhost
{
namespace ah_market
{

// AiPlayerbot.AhMarketInterval and AiPlayerbot.AhMarketBatchSize are already clamped by
// PlayerbotAIConfig; the sweep re-applies the same bounds so a second reader cannot widen them.
inline constexpr std::uint32_t kMinIntervalSeconds = 5;
inline constexpr std::uint32_t kMaxIntervalSeconds = 3600;
inline constexpr std::uint32_t kMaxBatchSize = 5;
inline constexpr std::uint64_t kWalkTimeoutMs = 45000;

inline std::uint32_t ClampIntervalSeconds(std::uint32_t seconds)
{
    return std::clamp(seconds, kMinIntervalSeconds, kMaxIntervalSeconds);
}

inline std::uint32_t ClampBatchSize(std::uint32_t batch)
{
    return std::clamp(batch, 1u, kMaxBatchSize);
}

inline std::uint64_t IntervalMs(std::uint32_t seconds)
{
    return static_cast<std::uint64_t>(ClampIntervalSeconds(seconds)) * 1000;
}

// One sweep pass per interval; `lastCycleMs` 0 = no pass since start.
inline bool SweepCycleDue(std::uint64_t nowMs, std::uint64_t lastCycleMs, std::uint32_t intervalSeconds)
{
    return lastCycleMs == 0 || nowMs >= lastCycleMs + IntervalMs(intervalSeconds);
}

// A walk that has not reached the auctioneer by now is abandoned, as the capital errands do.
inline bool WalkTimedOut(std::uint64_t nowMs, std::uint64_t startedAtMs)
{
    return nowMs >= startedAtMs + kWalkTimeoutMs;
}

struct SweepCandidateInput
{
    bool freeBot = false;
    bool inWorld = false;
    bool alive = false;
    bool inCombat = false;
    bool casting = false;
    bool inDuel = false;
    bool grouped = false;
    bool openWorldMap = false;
    bool busyElsewhere = false; // a capital, profession, guild-mail or order errand owns the bot
    bool hasSellableStock = false;
};

// The sweep only touches a bot that is idle, alone and free; a bot with nothing to sell is not walked to an auctioneer.
// A traveling bot is a candidate: the market takes the next walk (TB's seller has no movement gate either), so the
// engine's own travel target does not keep every free bot out of the sweep.
inline bool IsSweepCandidate(SweepCandidateInput const& in)
{
    return in.freeBot && in.inWorld && in.alive && !in.inCombat && !in.casting &&
           !in.inDuel && !in.grouped && in.openWorldMap && !in.busyElsewhere && in.hasSellableStock;
}

// The next pass starts past the block just considered; an empty block steps over one bot so it cannot stick.
inline std::uint32_t AdvanceRotation(std::uint32_t start, std::uint32_t considered, std::uint32_t size)
{
    if (size == 0)
        return 0;
    std::uint32_t const step = considered ? considered : 1;
    return (start + step) % size;
}

} // namespace ah_market
} // namespace tbhost
