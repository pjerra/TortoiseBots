// Pure decision for automatic starter hunter pets on level-up and login catch-up.
// Core-free: tested standalone by forever/tools/test_auto_hunter_pet.cpp and called by TbAutoHunterPet.cpp.
#pragma once

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace autohunterpet
{

constexpr uint8_t kMinLevel = 10;

enum class Verdict : uint8_t
{
    Grant = 0,
    SkipDisabled,
    SkipNotBot,
    SkipNotHunter,
    SkipLevelTooLow,
    SkipNotInWorld,
    SkipHasLivePet,
    SkipHasStablePet,
};

struct Input
{
    bool enabled = true;
    bool isBot = false;
    bool isHunter = false;
    uint8_t level = 0;
    bool inWorld = false;
    bool teleportingFar = false;
    bool hasLivePet = false;
    bool hasActivePet = false;
    bool hasStabledPet = false;
};

inline Verdict Decide(Input const& in)
{
    if (!in.enabled)
        return Verdict::SkipDisabled;
    if (!in.isBot)
        return Verdict::SkipNotBot;
    if (!in.isHunter)
        return Verdict::SkipNotHunter;
    if (in.level < kMinLevel)
        return Verdict::SkipLevelTooLow;
    if (!in.inWorld || in.teleportingFar)
        return Verdict::SkipNotInWorld;
    if (in.hasLivePet)
        return Verdict::SkipHasLivePet;
    if (in.hasActivePet || in.hasStabledPet)
        return Verdict::SkipHasStablePet;
    return Verdict::Grant;
}

inline char const* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Grant:            return "grant";
        case Verdict::SkipDisabled:     return "skip-disabled";
        case Verdict::SkipNotBot:       return "skip-not-bot";
        case Verdict::SkipNotHunter:    return "skip-not-hunter";
        case Verdict::SkipLevelTooLow:  return "skip-level-too-low";
        case Verdict::SkipNotInWorld:   return "skip-not-in-world";
        case Verdict::SkipHasLivePet:   return "skip-has-live-pet";
        case Verdict::SkipHasStablePet: return "skip-has-stable-pet";
    }
    return "unknown";
}

struct PetControlSpell
{
    uint32_t spellId;
    char const* name;
};

// Classic 1.60 hunter pet-control spells taught alongside the starter pet / on login catch-up.
inline constexpr PetControlSpell kPetControlSpells[] =
{
    { 883,  "Call Pet" },
    { 982,  "Revive Pet" },
    { 2641, "Dismiss Pet" },
    { 6991, "Feed Pet" },
    { 1515, "Tame Beast" },
    { 5149, "Beast Training" },
};

inline bool EligibleForPetControlSpells(Input const& in)
{
    return in.enabled && in.isBot && in.isHunter && in.level >= kMinLevel && in.inWorld && !in.teleportingFar;
}

template <typename HasSpellFn>
inline std::vector<PetControlSpell> MissingPetControlSpells(Input const& in, HasSpellFn&& hasSpell)
{
    std::vector<PetControlSpell> missing;
    if (!EligibleForPetControlSpells(in))
        return missing;
    for (PetControlSpell const& spell : kPetControlSpells)
    {
        if (!hasSpell(spell.spellId))
            missing.push_back(spell);
    }
    return missing;
}

} // namespace autohunterpet
} // namespace TortoiseBots
