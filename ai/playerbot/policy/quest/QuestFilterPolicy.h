#pragma once

#include <cstdint>

namespace ai
{

// ForeverCore's quest_template holds all expansions (0..11) plus Forever's custom
// re-IDs (91xxx / 99xxx). Vanilla questing accepts only Expansion 0 or those custom bands,
// and requires both a spawned giver and a spawned ender.
inline bool IsForeverCustomQuestId(std::uint32_t questId)
{
    return (questId >= 91000u && questId < 92000u) || (questId >= 99000u && questId < 100000u);
}

inline bool IsVanillaOrCustomQuest(std::uint32_t questId, std::int32_t expansion, bool hasSpawnedGiver, bool hasSpawnedEnder)
{
    if (!hasSpawnedGiver || !hasSpawnedEnder)
        return false;
    return expansion == 0 || IsForeverCustomQuestId(questId);
}

// When two in-scope quests share a title, Forever's custom re-ID wins over the retail copy;
// if both or neither are custom, the lower ID wins.
inline bool PreferQuestByTitle(std::uint32_t candidateId, std::uint32_t existingId)
{
    if (existingId == 0u)
        return true;
    bool const candCustom = IsForeverCustomQuestId(candidateId);
    bool const existCustom = IsForeverCustomQuestId(existingId);
    if (candCustom != existCustom)
        return candCustom;
    return candidateId < existingId;
}

// An objective with no known source must be abandoned after a bounded number of failed ticks
// so the bot never loops on an unresolvable quest forever.
inline bool ShouldDropUnresolvableQuest(std::uint32_t failedTicks, std::uint32_t maxFailedTicks = 10u)
{
    return maxFailedTicks > 0u && failedTicks >= maxFailedTicks;
}

} // namespace ai
