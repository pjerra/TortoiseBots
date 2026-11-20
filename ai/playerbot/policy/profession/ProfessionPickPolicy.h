#pragma once

// W7: Pure profession pick policy (class -> primary profession pair, name/id helpers).
// Core-free so forever/tools/test_w7_professions.cpp tests it standalone.

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace prof
{

inline constexpr std::uint32_t SkillBlacksmithing = 164;
inline constexpr std::uint32_t SkillLeatherworking = 165;
inline constexpr std::uint32_t SkillAlchemy = 171;
inline constexpr std::uint32_t SkillHerbalism = 182;
inline constexpr std::uint32_t SkillMining = 186;
inline constexpr std::uint32_t SkillTailoring = 197;
inline constexpr std::uint32_t SkillEngineering = 202;
inline constexpr std::uint32_t SkillEnchanting = 333;
inline constexpr std::uint32_t SkillSkinning = 393;
inline constexpr std::uint32_t SkillFirstAid = 129;
inline constexpr std::uint32_t SkillCooking = 185;
inline constexpr std::uint32_t SkillFishing = 356;

inline constexpr std::uint8_t ClassWarrior = 1;
inline constexpr std::uint8_t ClassPaladin = 2;
inline constexpr std::uint8_t ClassHunter = 3;
inline constexpr std::uint8_t ClassRogue = 4;
inline constexpr std::uint8_t ClassPriest = 5;
inline constexpr std::uint8_t ClassShaman = 7;
inline constexpr std::uint8_t ClassMage = 8;
inline constexpr std::uint8_t ClassWarlock = 9;
inline constexpr std::uint8_t ClassDruid = 11;

// Settled primary profession pairs per class (gathering + matching craft).
inline std::pair<std::uint32_t, std::uint32_t> ProfessionPairForClass(std::uint8_t classId)
{
    switch (classId)
    {
        case ClassWarrior:
        case ClassPaladin:
            return { SkillMining, SkillBlacksmithing };
        case ClassHunter:
        case ClassRogue:
        case ClassDruid:
        case ClassShaman:
            return { SkillSkinning, SkillLeatherworking };
        case ClassMage:
        case ClassWarlock:
            return { SkillHerbalism, SkillAlchemy };
        case ClassPriest:
            return { SkillTailoring, SkillEnchanting };
        default:
            return { 0u, 0u };
    }
}

inline bool IsPrimaryProfession(std::uint32_t skillId)
{
    switch (skillId)
    {
        case SkillBlacksmithing:
        case SkillLeatherworking:
        case SkillAlchemy:
        case SkillHerbalism:
        case SkillMining:
        case SkillTailoring:
        case SkillEngineering:
        case SkillEnchanting:
        case SkillSkinning:
            return true;
        default:
            return false;
    }
}

inline bool IsSupportedProfession(std::uint32_t skillId)
{
    return IsPrimaryProfession(skillId) || skillId == SkillFirstAid || skillId == SkillCooking;
}

inline std::uint32_t PickNextProfession(std::uint8_t classId, std::uint32_t freePrimaryPoints, bool hasFirst, bool hasSecond)
{
    if (freePrimaryPoints == 0)
        return 0;
    auto const pair = ProfessionPairForClass(classId);
    if (pair.first != 0 && !hasFirst)
        return pair.first;
    if (pair.second != 0 && !hasSecond)
        return pair.second;
    return 0;
}

inline std::vector<std::uint32_t> PickProfessionsToLearn(std::uint8_t classId, std::uint32_t freePrimaryPoints,
    std::vector<std::uint32_t> const& knownSkills)
{
    std::vector<std::uint32_t> out;
    if (freePrimaryPoints == 0)
        return out;
    auto const hasSkill = [&](std::uint32_t id)
    {
        for (std::uint32_t k : knownSkills)
            if (k == id)
                return true;
        return false;
    };
    auto const pair = ProfessionPairForClass(classId);
    std::uint32_t remaining = freePrimaryPoints;
    if (pair.first != 0 && !hasSkill(pair.first) && remaining > 0)
    {
        out.push_back(pair.first);
        --remaining;
    }
    if (pair.second != 0 && !hasSkill(pair.second) && remaining > 0)
    {
        out.push_back(pair.second);
        --remaining;
    }
    return out;
}

inline char const* SkillNameFromId(std::uint32_t skillId)
{
    switch (skillId)
    {
        case SkillBlacksmithing: return "blacksmithing";
        case SkillLeatherworking: return "leatherworking";
        case SkillAlchemy: return "alchemy";
        case SkillHerbalism: return "herbalism";
        case SkillMining: return "mining";
        case SkillTailoring: return "tailoring";
        case SkillEngineering: return "engineering";
        case SkillEnchanting: return "enchanting";
        case SkillSkinning: return "skinning";
        case SkillFirstAid: return "first-aid";
        case SkillCooking: return "cooking";
        default: return "unknown";
    }
}

inline std::uint32_t SkillIdFromName(std::string_view raw)
{
    std::string s;
    s.reserve(raw.size());
    for (char c : raw)
    {
        if (c == '_' || c == ' ')
            s.push_back('-');
        else
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (s == "fishing" || s == "356")
        return 0;
    if (s == "blacksmithing" || s == "bs" || s == "164") return SkillBlacksmithing;
    if (s == "leatherworking" || s == "lw" || s == "165") return SkillLeatherworking;
    if (s == "alchemy" || s == "alch" || s == "171") return SkillAlchemy;
    if (s == "herbalism" || s == "herb" || s == "182") return SkillHerbalism;
    if (s == "mining" || s == "mine" || s == "186") return SkillMining;
    if (s == "tailoring" || s == "tailor" || s == "197") return SkillTailoring;
    if (s == "engineering" || s == "eng" || s == "202") return SkillEngineering;
    if (s == "enchanting" || s == "ench" || s == "333") return SkillEnchanting;
    if (s == "skinning" || s == "skin" || s == "393") return SkillSkinning;
    if (s == "first-aid" || s == "firstaid" || s == "129") return SkillFirstAid;
    if (s == "cooking" || s == "cook" || s == "185") return SkillCooking;
    return 0;
}

} // namespace prof
} // namespace TortoiseBots
