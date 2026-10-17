#pragma once

#include <cstdint>

// Issue #1043: a bot's pet must start next to its owner.
//
// The core picks a summoned pet's spot a few yards from the owner and then
// snaps it to the ground. Under an overhang that ground lookup can land on the
// roof: at the Uldaman portal (map 0, about -6054, -2955, z 213) every hired
// hunter's pet appeared at z 419, some 205 yards above its owner, and the
// pet's own update unsummoned it on its first tick for being more than 120
// yards away (core Pet::Update). Call Pet then brought it back to the same roof
// every time, so the hunter never had a pet.
//
// The bot side checks each new pet once, right after it appears, and pulls it
// onto the owner when the spot is clearly wrong (BotManager::CheckNewPetSpawn:
// on the world thread after the bot's AI turn, which covers the resummon after
// a teleport, and from the map-thread player hook in host/BotPlayerAdapter.cpp
// for a summon cast that completes inside Player::Update and leaves the pet
// over 100 yards away, before the map updates its creatures). A pet is checked
// once per guid: every summon, Call Pet or post-teleport resummon makes a new
// pet object with a new guid, and a pet that has been out a while is left
// alone wherever it walks.
//
// Pure decision logic, unit-tested on its own (tools/test_pet_spawn_policy.cpp).

namespace TortoiseBots
{

// More than this many yards above or below the owner is a wrong floor.
inline constexpr float PET_SPAWN_MAX_HEIGHT_DIFF = 5.0f;
// More than this many yards away (3D) is not "next to the owner".
inline constexpr float PET_SPAWN_MAX_DISTANCE = 30.0f;

struct PetSpawnCheckInputs
{
    float dx = 0.0f; // pet minus owner, yards
    float dy = 0.0f;
    float dz = 0.0f;
    bool ownerSeesPet = true; // owner -> pet line of sight
};

// True when the new pet should be moved onto its owner.
inline bool ShouldPullNewPetToOwner(PetSpawnCheckInputs const& inputs)
{
    if (!inputs.ownerSeesPet)
        return true;
    float const dz = inputs.dz < 0.0f ? -inputs.dz : inputs.dz;
    if (dz > PET_SPAWN_MAX_HEIGHT_DIFF)
        return true;
    float const distSq = inputs.dx * inputs.dx + inputs.dy * inputs.dy + inputs.dz * inputs.dz;
    return distSq > PET_SPAWN_MAX_DISTANCE * PET_SPAWN_MAX_DISTANCE;
}

// The core unsummons a pet more than 120 yards from its owner on the pet's
// next update (Pet::Update). The map-thread hook only acts beyond this, so a
// pet at range or chasing a target never costs it the AI lookup.
inline constexpr float PET_UNSUMMON_GUARD_DISTANCE = 100.0f;

// True when the pet is far enough that its own update is about to unsummon it.
inline bool IsPetAboutToBeUnsummoned(PetSpawnCheckInputs const& inputs)
{
    float const distSq = inputs.dx * inputs.dx + inputs.dy * inputs.dy + inputs.dz * inputs.dz;
    return distSq > PET_UNSUMMON_GUARD_DISTANCE * PET_UNSUMMON_GUARD_DISTANCE;
}

// True when the owner's current pet has not been checked yet. 0 means no pet.
inline bool IsUncheckedPet(uint64_t currentPetGuid, uint64_t lastCheckedPetGuid)
{
    return currentPetGuid != 0 && currentPetGuid != lastCheckedPetGuid;
}

} // namespace TortoiseBots
