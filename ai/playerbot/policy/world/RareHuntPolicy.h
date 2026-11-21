#pragma once

#include <cstdint>

namespace TortoiseBots::world
{
    constexpr uint32_t kCreatureRankNormal = 0;
    constexpr uint32_t kCreatureRankElite = 1;
    constexpr uint32_t kCreatureRankRareElite = 2;
    constexpr uint32_t kCreatureRankWorldBoss = 3;
    constexpr uint32_t kCreatureRankRare = 4;

    inline bool IsHuntableRareRank(uint32_t rank, bool canFightElite)
    {
        if (rank == kCreatureRankRare)
            return true;
        if (rank == kCreatureRankRareElite)
            return canFightElite;
        return false;
    }

    inline bool IsRareOfLevel(int botLevel, int targetLevel, int maxLevelOver, int maxLevelBelow = 3)
    {
        return targetLevel >= botLevel - maxLevelBelow && targetLevel <= botLevel + maxLevelOver;
    }

    inline bool ShouldHuntRare(bool huntRaresEnabled, bool isFreeBot, bool hasRealPlayerMaster,
        bool inBattleGround, uint32_t rank, bool canFightElite, int botLevel, int targetLevel,
        int maxLevelOver)
    {
        if (!huntRaresEnabled || !isFreeBot || hasRealPlayerMaster || inBattleGround)
            return false;
        if (!IsHuntableRareRank(rank, canFightElite))
            return false;
        return IsRareOfLevel(botLevel, targetLevel, maxLevelOver);
    }
}
