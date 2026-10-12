#include "../ai/playerbot/GatherSpotPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AddDeathAvoidSpot;
using ai::DeathAvoidSpot;
using ai::GatherPointInSight;
using ai::IsDeathPositionAvoided;
using ai::kEmptyGatherSpotMs;
using ai::kEmptyGatherSpotRadiusYd;
using ai::kEmptyGatherSpots;
using ai::ShouldMarkGatherSpotEmpty;

int main()
{
    std::cout << "Starting TortoiseBots gather spot tests...\n";

    // Own point in sight: same map, and the 60 yd scan around the bot must
    // still cover the 10 yd spot radius around the point (<= 50 yd).
    CHECK(GatherPointInSight(true, 49.0f, 60.0f));
    CHECK(GatherPointInSight(true, 50.0f, 60.0f));
    CHECK(!GatherPointInSight(true, 51.0f, 60.0f));
    CHECK(!GatherPointInSight(true, 60.0f, 60.0f));
    CHECK(!GatherPointInSight(false, 5.0f, 60.0f));

    // Only a point judged by sight, still gatherable, with no node on it is
    // recorded empty: a live node, a pooled alternate or a skill/level expiry
    // never marks the spot.
    CHECK(ShouldMarkGatherSpotEmpty(true, true, false));
    CHECK(!ShouldMarkGatherSpotEmpty(true, true, true));
    CHECK(!ShouldMarkGatherSpotEmpty(true, false, false));
    CHECK(!ShouldMarkGatherSpotEmpty(false, true, false));

    std::uint32_t const NOW = 1'000'000;
    std::uint32_t const MAP = 1;

    {
        // A spot seen empty is refused for its window, co-located pool
        // alternates included; a spawn 30 yd away stays open.
        DeathAvoidSpot spots[kEmptyGatherSpots] = {};
        AddDeathAvoidSpot(spots, kEmptyGatherSpots, MAP, -7000.0f, -3000.0f, NOW, kEmptyGatherSpotMs, kEmptyGatherSpotRadiusYd);
        CHECK(IsDeathPositionAvoided(MAP, -7000.0f, -3000.0f, NOW + 1000, spots, kEmptyGatherSpots));
        CHECK(IsDeathPositionAvoided(MAP, -7002.0f, -2998.0f, NOW + 1000, spots, kEmptyGatherSpots));
        CHECK(!IsDeathPositionAvoided(MAP, -7030.0f, -3000.0f, NOW + 1000, spots, kEmptyGatherSpots));
        CHECK(!IsDeathPositionAvoided(0, -7000.0f, -3000.0f, NOW + 1000, spots, kEmptyGatherSpots));
        // Respawn window over: walkable again.
        CHECK(!IsDeathPositionAvoided(MAP, -7000.0f, -3000.0f, NOW + kEmptyGatherSpotMs + 1, spots, kEmptyGatherSpots));
    }

    {
        // A sweep of more empty spots than slots keeps the newest ones.
        DeathAvoidSpot spots[kEmptyGatherSpots] = {};
        for (std::uint32_t i = 0; i <= kEmptyGatherSpots; ++i)
            AddDeathAvoidSpot(spots, kEmptyGatherSpots, MAP, 100.0f * i, 0.0f, NOW + i, kEmptyGatherSpotMs, kEmptyGatherSpotRadiusYd);
        CHECK(!IsDeathPositionAvoided(MAP, 0.0f, 0.0f, NOW + 100, spots, kEmptyGatherSpots));
        for (std::uint32_t i = 1; i <= kEmptyGatherSpots; ++i)
            CHECK(IsDeathPositionAvoided(MAP, 100.0f * i, 0.0f, NOW + 100, spots, kEmptyGatherSpots));
    }

    std::cout << "All gather spot tests passed.\n";
    return 0;
}
