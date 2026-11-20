#pragma once

// W7: Pure craft-for-self policy (bags, bandages, upgrades; reagents, tools, skill, bag slots).
// Core-free so forever/tools/test_w7_professions.cpp tests it standalone.

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace prof
{

enum class CraftPurpose : std::uint8_t
{
    Explicit = 0,
    Bag = 1,
    Bandage = 2,
    Upgrade = 3,
    SkillUp = 4
};

struct ReagentCheck
{
    std::uint32_t itemId = 0;
    std::uint32_t need = 0;
    std::uint32_t have = 0;
};

struct CraftFacts
{
    std::uint32_t spellId = 0;
    bool known = false;
    std::uint32_t skillId = 0;
    std::uint32_t reqSkill = 0;
    std::uint32_t botSkill = 0;
    bool hasRequiredTool = true;
    std::uint32_t freeBagSlots = 0;
    bool createsBag = false;
    bool hasFreeEquipBagSlot = false;
    bool isUpgradeForSelf = false;
    bool isBandageOrConsumable = false;
    CraftPurpose purpose = CraftPurpose::Explicit;
    std::vector<ReagentCheck> reagents;
};

struct CraftDecision
{
    bool allow = false;
    char const* code = "ok";
};

inline CraftDecision EvaluateCraftForSelf(CraftFacts const& f)
{
    if (f.spellId == 0 || !f.known)
        return { false, "unknown-recipe" };
    if (f.skillId != 0 && f.botSkill < f.reqSkill)
        return { false, "skill" };
    if (!f.hasRequiredTool)
        return { false, "tool" };
    for (ReagentCheck const& r : f.reagents)
    {
        if (r.have < r.need)
            return { false, "reagents" };
    }
    if (f.freeBagSlots == 0)
        return { false, "bags" };
    if (f.createsBag && !f.hasFreeEquipBagSlot)
        return { false, "bags" };

    if (f.purpose != CraftPurpose::Explicit)
    {
        bool const useful = (f.createsBag && f.hasFreeEquipBagSlot)
            || f.isUpgradeForSelf
            || f.isBandageOrConsumable
            || f.purpose == CraftPurpose::SkillUp;
        if (!useful)
            return { false, "not-useful" };
    }

    return { true, "ok" };
}

} // namespace prof
} // namespace TortoiseBots
