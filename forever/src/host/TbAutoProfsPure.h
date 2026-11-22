// Core-free decision rules for autonomous level-5 profession grants on pool bots.
// Standalone-tested by forever/tools/test_auto_profs.cpp.
#pragma once

#include "ai/playerbot/policy/profession/ProfessionPickPolicy.h"
#include "runtime/ProfessionGrantPolicy.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace autoprofs
{

// Classic Apprentice (rank 1) spell IDs verified against ForeverCore's trainer_spell table.
inline constexpr std::uint32_t SpellApprenticeFirstAid      = 3273;
inline constexpr std::uint32_t SpellApprenticeCooking       = 2550;
inline constexpr std::uint32_t SpellApprenticeBlacksmithing = 2018;
inline constexpr std::uint32_t SpellApprenticeLeatherworking = 2108;
inline constexpr std::uint32_t SpellApprenticeAlchemy       = 2259;
inline constexpr std::uint32_t SpellApprenticeHerbalism     = 2366;
inline constexpr std::uint32_t SpellApprenticeMining        = 2575;
inline constexpr std::uint32_t SpellApprenticeTailoring     = 3908;
inline constexpr std::uint32_t SpellApprenticeEngineering   = 4036;
inline constexpr std::uint32_t SpellApprenticeEnchanting    = 7411;
inline constexpr std::uint32_t SpellApprenticeSkinning      = 8613;

inline std::uint32_t ApprenticeSpellForSkill(std::uint32_t skillId)
{
    switch (skillId)
    {
        case prof::SkillFirstAid:       return SpellApprenticeFirstAid;
        case prof::SkillCooking:        return SpellApprenticeCooking;
        case prof::SkillBlacksmithing:  return SpellApprenticeBlacksmithing;
        case prof::SkillLeatherworking: return SpellApprenticeLeatherworking;
        case prof::SkillAlchemy:        return SpellApprenticeAlchemy;
        case prof::SkillHerbalism:      return SpellApprenticeHerbalism;
        case prof::SkillMining:         return SpellApprenticeMining;
        case prof::SkillTailoring:      return SpellApprenticeTailoring;
        case prof::SkillEngineering:    return SpellApprenticeEngineering;
        case prof::SkillEnchanting:     return SpellApprenticeEnchanting;
        case prof::SkillSkinning:       return SpellApprenticeSkinning;
        default:                        return 0;
    }
}

// Deterministic, class-sensible primary profession pair keyed by bot guid counter.
// Every pair is either gathering + matching craft, two gatherings, or Tailoring + Enchanting (1 in 3 cloth bots).
inline std::pair<std::uint32_t, std::uint32_t> PickAutoProfessionPair(std::uint8_t classId, std::uint64_t guidCounter)
{
    switch (classId)
    {
        case prof::ClassWarrior:
        case prof::ClassPaladin:
        {
            static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 2> kPlatePairs = {{
                { prof::SkillMining, prof::SkillBlacksmithing },
                { prof::SkillMining, prof::SkillEngineering },
            }};
            return kPlatePairs[guidCounter % kPlatePairs.size()];
        }
        case prof::ClassHunter:
        case prof::ClassRogue:
        case prof::ClassShaman:
        case prof::ClassDruid:
        {
            static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 3> kLeatherMailPairs = {{
                { prof::SkillSkinning, prof::SkillLeatherworking },
                { prof::SkillMining,   prof::SkillEngineering },
                { prof::SkillSkinning, prof::SkillMining },
            }};
            return kLeatherMailPairs[guidCounter % kLeatherMailPairs.size()];
        }
        case prof::ClassPriest:
        case prof::ClassMage:
        case prof::ClassWarlock:
        {
            std::uint32_t const tailorRoll = static_cast<std::uint32_t>(guidCounter % TAILOR_ENCHANT_SHARE_DENOMINATOR);
            if (RollsTailorEnchantPair(classId, tailorRoll))
                return { prof::SkillTailoring, prof::SkillEnchanting };

            static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 4> kClothGatherPairs = {{
                { prof::SkillHerbalism, prof::SkillAlchemy },
                { prof::SkillHerbalism, prof::SkillMining },
                { prof::SkillMining,    prof::SkillSkinning },
                { prof::SkillHerbalism, prof::SkillSkinning },
            }};
            return kClothGatherPairs[(guidCounter / TAILOR_ENCHANT_SHARE_DENOMINATOR) % kClothGatherPairs.size()];
        }
        default:
            return { 0u, 0u };
    }
}

struct AutoProfInputs
{
    std::uint32_t level = 1;
    std::uint8_t classId = 0;
    std::uint64_t guidCounter = 0;
    bool isPoolBot = false;
    bool hasPrimaryProfession = false;
    bool hasFirstAid = false;
    bool hasCooking = false;
};

struct AutoProfPlan
{
    std::vector<std::uint32_t> skillsToLearn;
};

inline AutoProfPlan PlanAutoProfessions(AutoProfInputs const& in)
{
    AutoProfPlan plan;
    if (!in.isPoolBot || in.level < PRIMARY_PROFESSION_MIN_LEVEL)
        return plan;

    ProfessionGrantInputs grantIn;
    grantIn.level = in.level;
    grantIn.isPoolBot = in.isPoolBot;
    grantIn.hasPrimaryProfession = in.hasPrimaryProfession;

    if (DecideProfessionGrant(grantIn) == ProfessionGrantDecision::GrantAll)
    {
        auto const pair = PickAutoProfessionPair(in.classId, in.guidCounter);
        if (pair.first != 0)
            plan.skillsToLearn.push_back(pair.first);
        if (pair.second != 0 && pair.second != pair.first)
            plan.skillsToLearn.push_back(pair.second);
    }

    if (!in.hasFirstAid)
        plan.skillsToLearn.push_back(prof::SkillFirstAid);
    if (!in.hasCooking)
        plan.skillsToLearn.push_back(prof::SkillCooking);

    return plan;
}

} // namespace autoprofs
} // namespace TortoiseBots
