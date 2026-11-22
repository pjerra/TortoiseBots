#pragma once

// Core-free auto-tools policy for ForeverCore pool/free bots: maps learned
// gathering and crafting skills to the Classic 1.60 tools they require.

#include "ai/playerbot/policy/profession/ProfessionPickPolicy.h"

#include <array>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace autotools
{

inline constexpr uint32_t kItemMiningPick = 2901;
inline constexpr uint32_t kItemSkinningKnife = 7005;
inline constexpr uint32_t kItemBlacksmithHammer = 5956;
inline constexpr uint32_t kItemFishingPole = 6256;
inline constexpr uint32_t kItemArclightSpanner = 6219;
inline constexpr uint32_t kItemRunedCopperRod = 6218;

inline constexpr std::array<uint32_t, 8> kMiningPickIds = {
    756,    // Tunnel Pick
    778,    // Kobold Excavation Pick
    1819,   // Gouging Pick
    1893,   // Miner's Revenge
    1959,   // Cold Iron Pick
    2901,   // Mining Pick
    9465,   // Digmaster 5000
    20723,  // Brann's Trusty Pick
};

inline constexpr std::array<uint32_t, 3> kSkinningKnifeIds = {
    7005,   // Skinning Knife
    12709,  // Pip's Skinner / Finkle's Skinner
    19901,  // Zulian Slicer
};

inline constexpr std::array<uint32_t, 5> kEnchantingRodIds = {
    6218,   // Runed Copper Rod
    6339,   // Runed Silver Rod
    11130,  // Runed Golden Rod
    11145,  // Runed Truesilver Rod
    16207,  // Runed Arcanite Rod
};

struct ToolRule
{
    uint32_t skillId = 0;
    uint32_t grantItemId = 0;
};

inline constexpr std::array<ToolRule, 7> kProfessionToolRules = {{
    { prof::SkillMining, kItemMiningPick },
    { prof::SkillSkinning, kItemSkinningKnife },
    { prof::SkillBlacksmithing, kItemBlacksmithHammer },
    { prof::SkillFishing, kItemFishingPole },
    { prof::SkillEngineering, kItemBlacksmithHammer },
    { prof::SkillEngineering, kItemArclightSpanner },
    { prof::SkillEnchanting, kItemRunedCopperRod },
}};

template <typename HasItemFn, std::size_t N>
inline bool HasAnyItem(HasItemFn&& hasItem, std::array<uint32_t, N> const& ids)
{
    for (uint32_t id : ids)
    {
        if (hasItem(id))
            return true;
    }
    return false;
}

template <typename HasItemFn>
inline bool HasRequiredGatherTool(uint32_t skillId, HasItemFn&& hasItem)
{
    if (skillId == prof::SkillMining)
        return HasAnyItem(hasItem, kMiningPickIds);
    if (skillId == prof::SkillSkinning)
        return HasAnyItem(hasItem, kSkinningKnifeIds);
    return true;
}

template <typename HasItemFn>
inline bool HasToolForRule(ToolRule const& rule, HasItemFn&& hasItem)
{
    if (rule.skillId == prof::SkillMining)
        return HasAnyItem(hasItem, kMiningPickIds);
    if (rule.skillId == prof::SkillSkinning)
        return HasAnyItem(hasItem, kSkinningKnifeIds);
    if (rule.skillId == prof::SkillEnchanting)
        return HasAnyItem(hasItem, kEnchantingRodIds);
    return hasItem(rule.grantItemId);
}

template <typename HasSkillFn, typename HasItemFn>
inline std::vector<uint32_t> MissingToolsForSkills(HasSkillFn&& hasSkill, HasItemFn&& hasItem)
{
    std::vector<uint32_t> missing;
    auto const alreadyQueued = [&missing](uint32_t itemId)
    {
        for (uint32_t existing : missing)
        {
            if (existing == itemId)
                return true;
        }
        return false;
    };

    for (ToolRule const& rule : kProfessionToolRules)
    {
        if (!hasSkill(rule.skillId))
            continue;
        if (HasToolForRule(rule, hasItem))
            continue;
        if (!alreadyQueued(rule.grantItemId))
            missing.push_back(rule.grantItemId);
    }
    return missing;
}

} // namespace autotools
} // namespace TortoiseBots
