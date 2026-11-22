// Pure decision for automatic starter hunter pets on level-up and login catch-up.
// Core-free: tested standalone by forever/tools/test_auto_hunter_pet.cpp and called by TbAutoHunterPet.cpp.
#pragma once

#include <cstdint>

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

} // namespace autohunterpet
} // namespace TortoiseBots
