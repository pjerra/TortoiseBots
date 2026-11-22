// Core-free decision predicates for automatic class-spell training (forever/src/host/TbAutoTrain.cpp).
// Tested standalone by forever/tools/test_autotrain.cpp.
#pragma once

#include <cstdint>

namespace tbhost
{
namespace autotrain
{

// The 1.12-era spell ids of the class skill lines end below 100000; retail-era spells sit far above it.
inline constexpr uint32_t kClassicSpellIdEnd = 100000;

inline bool ShouldSkipRetailChainless(uint32_t spellId, uint32_t spellRank)
{
    return spellRank == 0 && spellId >= kClassicSpellIdEnd;
}

inline bool ShouldRemovePostTrain(uint32_t spellId, uint32_t spellRank, bool knownBefore)
{
    return !knownBefore && ShouldSkipRetailChainless(spellId, spellRank);
}

inline bool ShouldTeachClassLineSpell(uint8_t spellLevel, uint8_t botLevel,
    uint32_t spellFamily, uint32_t classFamily, bool alreadyKnown,
    uint32_t spellId, uint32_t spellRank)
{
    if (alreadyKnown || spellLevel == 0 || spellLevel > botLevel || spellFamily != classFamily)
        return false;
    return !ShouldSkipRetailChainless(spellId, spellRank);
}

inline bool ShouldAutoTrainOnEvent(bool flagEnabled, bool isBot)
{
    return flagEnabled && isBot;
}

inline uint32_t NetTrainerTaught(uint32_t trainerTaught, uint32_t removedCount)
{
    return trainerTaught > removedCount ? trainerTaught - removedCount : 0;
}

} // namespace autotrain
} // namespace tbhost
