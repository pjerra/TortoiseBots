#pragma once

// W7: Pure gather node policy (lock skill vs bot skill, readiness, range, bags, tool, danger).
// Core-free so forever/tools/test_w7_professions.cpp tests it standalone.

#include <cstdint>

namespace TortoiseBots
{
namespace prof
{

enum class GatherDifficulty : std::uint8_t
{
    Impossible = 0,
    Orange = 1,
    Yellow = 2,
    Green = 3,
    Grey = 4
};

struct GatherNodeFacts
{
    bool exists = false;
    bool spawned = false;
    bool ready = false;
    std::uint32_t lockSkillId = 0;
    std::uint32_t reqSkill = 0;
    std::uint32_t botSkill = 0;
    bool hasSkill = false;
    std::uint32_t freeBagSlots = 0;
    bool hasRequiredTool = true;
    bool dangerNearby = false;
    float distanceYd = 0.0f;
    float maxDistanceYd = 150.0f;
    bool sameZone = true;
};

struct GatherDecision
{
    bool allow = false;
    char const* code = "ok";
    GatherDifficulty difficulty = GatherDifficulty::Impossible;
};

inline GatherDifficulty ClassifyGatherDifficulty(std::uint32_t botSkill, std::uint32_t reqSkill)
{
    if (botSkill < reqSkill)
        return GatherDifficulty::Impossible;
    std::uint32_t const delta = botSkill - reqSkill;
    if (delta < 25)
        return GatherDifficulty::Orange;
    if (delta < 50)
        return GatherDifficulty::Yellow;
    if (delta < 100)
        return GatherDifficulty::Green;
    return GatherDifficulty::Grey;
}

inline GatherDecision EvaluateGatherNode(GatherNodeFacts const& f)
{
    if (!f.exists || !f.spawned)
        return { false, "not-found", GatherDifficulty::Impossible };
    if (!f.ready)
        return { false, "looted", GatherDifficulty::Impossible };
    if (!f.sameZone)
        return { false, "zone", GatherDifficulty::Impossible };
    if (f.distanceYd > f.maxDistanceYd)
        return { false, "range", GatherDifficulty::Impossible };
    if (f.lockSkillId != 0 && !f.hasSkill)
        return { false, "no-prof", GatherDifficulty::Impossible };
    if (f.lockSkillId != 0 && f.botSkill < f.reqSkill)
        return { false, "skill", GatherDifficulty::Impossible };
    if (!f.hasRequiredTool)
        return { false, "tool", GatherDifficulty::Impossible };
    if (f.freeBagSlots == 0)
        return { false, "bags", GatherDifficulty::Impossible };
    if (f.dangerNearby)
        return { false, "danger", GatherDifficulty::Impossible };

    return { true, "ok", ClassifyGatherDifficulty(f.botSkill, f.reqSkill) };
}

} // namespace prof
} // namespace TortoiseBots
