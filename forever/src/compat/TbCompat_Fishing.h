// Plain bridge between the host fishing errand and the legacy fish-location
// store (ai/playerbot/TravelMgr.cpp). The store's types need the legacy
// preinclude, the host layer does not; this header carries only numbers.
#pragma once

#include <cstdint>

namespace tbcompat
{

struct FishingSpot
{
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t zone = 0;
    std::int32_t zoneSkill = 0;
};

// Nearest fish location on `mapId` within `maxDistanceYd` whose zone does not
// demand more than `maxZoneSkill` (a non-positive requirement always passes).
// False when the store holds none (travel_fishing compat disabled, the
// FISH_LOCATION table missing or every spot out of range).
bool NearestFishingSpot(std::uint32_t mapId, float x, float y, float z, float maxDistanceYd,
    std::uint32_t maxZoneSkill, FishingSpot& out);

// Whether the fish-location store holds at least one spot with a fishable
// zone. False when the FISH_LOCATION rows were never loaded.
bool FishingStoreLoaded();

// The area's fishing requirement, inherited from its parent areas; 0 when no
// level of the chain has a row. Quiet: a missing sub-area row is normal.
std::int32_t FishingBaseSkill(std::uint32_t areaId);

} // namespace tbcompat
