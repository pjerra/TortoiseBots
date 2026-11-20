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

enum class CraftPhase : std::uint8_t
{
    Idle = 0,
    Casting = 1,
    Done = 2,
    Failed = 3
};

struct CraftErrandFacts
{
    CraftPhase phase = CraftPhase::Idle;
    bool inWorld = false;
    bool alive = false;
    bool isCasting = false;
    bool tracksOutputItem = false;
    std::uint32_t itemsGainedThisRun = 0;
    std::uint32_t completedRuns = 0;
    std::uint32_t targetRuns = 1;
    std::uint32_t phaseElapsedMs = 0;
    std::uint32_t castTimeoutMs = 15000;
};

enum class CraftErrandAction : std::uint8_t
{
    None = 0,
    Wait = 1,
    CastNext = 2,
    FinishOk = 3,
    FinishFail = 4
};

struct CraftErrandStep
{
    CraftPhase nextPhase = CraftPhase::Idle;
    CraftErrandAction action = CraftErrandAction::None;
    char const* stateLabel = "";
    char const* failCode = "";
};

inline CraftErrandStep StepCraftErrand(CraftErrandFacts const& f)
{
    if (!f.inWorld)
        return { CraftPhase::Failed, CraftErrandAction::FinishFail, "fail:not-in-world", "not-in-world" };
    if (!f.alive)
        return { CraftPhase::Failed, CraftErrandAction::FinishFail, "fail:dead", "dead" };
    if (f.phase != CraftPhase::Casting)
        return { f.phase, CraftErrandAction::None, "", "" };

    if (f.tracksOutputItem)
    {
        if (f.itemsGainedThisRun > 0)
        {
            if (f.completedRuns + 1 >= f.targetRuns)
                return { CraftPhase::Done, CraftErrandAction::FinishOk, "ok", "" };
            return { CraftPhase::Casting, CraftErrandAction::CastNext, "pending:casting", "" };
        }
        if (f.phaseElapsedMs >= f.castTimeoutMs)
            return { CraftPhase::Failed, CraftErrandAction::FinishFail, "fail:timeout", "timeout" };
        if (!f.isCasting && f.phaseElapsedMs >= 1000)
            return { CraftPhase::Failed, CraftErrandAction::FinishFail, "fail:craft-no-item", "craft-no-item" };
        return { CraftPhase::Casting, CraftErrandAction::Wait, "pending:casting", "" };
    }

    if (f.phaseElapsedMs >= f.castTimeoutMs)
        return { CraftPhase::Failed, CraftErrandAction::FinishFail, "fail:timeout", "timeout" };
    if (!f.isCasting && f.phaseElapsedMs >= 500)
    {
        if (f.completedRuns + 1 >= f.targetRuns)
            return { CraftPhase::Done, CraftErrandAction::FinishOk, "ok", "" };
        return { CraftPhase::Casting, CraftErrandAction::CastNext, "pending:casting", "" };
    }
    return { CraftPhase::Casting, CraftErrandAction::Wait, "pending:casting", "" };
}

} // namespace prof
} // namespace TortoiseBots
