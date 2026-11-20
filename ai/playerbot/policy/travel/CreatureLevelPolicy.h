#pragma once

#include <cstdint>

// The level of a creature spawn entry as travel reads it (W4 review fix, decision 4 of the W4 A1+A2 round).
//
// ForeverCore has no per-template level: the vanilla levels live in `creature_classic_level`, and a spawn without a row there
// (a retail-only spawn on maps 0/1) has NO known level. Unknown is not 0: 0 sits inside the grind band of a level 1 or 2 bot
// (the band minimum truncates to 0), so reading unknown as 0 offered a fresh bot a grind spot whose real level may be 60+.
// Unknown is therefore answered with kUnknown, a value above every level window, so every "is this creature's level within
// the bot's window" rule (grind band, quest objective cap, boss cap, gather skill) refuses it. A row that says level 0 is
// corrupt data and counts as unknown too.
//
// The route cost is the one reader that must not use kUnknown: it takes the maximum level near a link, and a creature with no
// rating adds no threat reading (ThreatLevelOf), instead of pricing the link as a level-2-billion camp.
namespace ai
{
    namespace travel_level
    {
        constexpr std::int32_t kUnknown = INT32_MAX;

        inline std::int32_t FromClassicRow(bool hasRow, std::uint32_t levelMax)
        {
            return (hasRow && levelMax > 0) ? static_cast<std::int32_t>(levelMax) : kUnknown;
        }

        inline bool IsKnown(std::int32_t level)
        {
            return level != kUnknown;
        }

        // The level a link's creature scan records (TravelNodePath::calculateCost): the known level, or 0 = "not rated".
        inline std::uint32_t ThreatLevelOf(std::int32_t level)
        {
            return IsKnown(level) && level > 0 ? static_cast<std::uint32_t>(level) : 0u;
        }
    }
}
