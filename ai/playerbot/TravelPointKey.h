// The identity of a spawn point in the travel destination map (TravelMgr::pointsMap). Core-free: both cores build it in their
// travel compat pair (GuidKeyWL4).
//
// The Turtle guid is one 64-bit number and the legacy map keyed by it. The ForeverCore guid is two 64-bit halves, and its low
// half alone is only the spawn counter: creatures and game objects count their spawns in separate namespaces, so the same
// counter exists on both sides and on both continents. Keying by the low half alone therefore merges two unrelated spawns,
// and a destination then points at another object's coordinates and cached area. The composite keeps the whole identity
// (type and entry and map in the high half, spawn id in the low half) apart.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ai
{
    struct TravelPointKey
    {
        uint64_t high = 0;
        uint64_t low = 0;

        bool operator==(TravelPointKey const& other) const { return high == other.high && low == other.low; }
        bool operator!=(TravelPointKey const& other) const { return !(*this == other); }
    };

    // The Turtle side passes high = 0 and keeps the legacy identity exactly.
    inline TravelPointKey MakeTravelPointKey(uint64_t high, uint64_t low)
    {
        TravelPointKey key;
        key.high = high;
        key.low = low;
        return key;
    }

    struct TravelPointKeyHash
    {
        std::size_t operator()(TravelPointKey const& key) const
        {
            // The low half is the spawn counter and carries most of the spread; the high half is folded in.
            return std::hash<uint64_t>()(key.low ^ (key.high * 0x9E3779B97F4A7C15ull));
        }
    };
}
