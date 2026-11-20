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

enum class GatherPhase : std::uint8_t
{
    Idle = 0,
    Walking = 1,
    Casting = 2,
    Looting = 3,
    Done = 4,
    Failed = 5
};

struct GatherErrandFacts
{
    GatherPhase phase = GatherPhase::Idle;
    bool inWorld = false;
    bool alive = false;
    bool inCombat = false;
    bool nodeExists = false;
    bool nodeReady = false;
    bool inInteractRange = false;
    bool isMoving = false;
    bool isCasting = false;
    bool lootAvailable = false;
    bool lootAttempted = false;
    std::uint32_t itemsGained = 0;
    std::uint32_t phaseElapsedMs = 0;
    std::uint32_t walkTimeoutMs = 20000;
    std::uint32_t castTimeoutMs = 10000;
    std::uint32_t lootTimeoutMs = 3000;
};

enum class GatherErrandAction : std::uint8_t
{
    None = 0,
    MoveToNode = 1,
    CastGather = 2,
    Wait = 3,
    LootNode = 4,
    FinishOk = 5,
    FinishFail = 6
};

struct GatherErrandStep
{
    GatherPhase nextPhase = GatherPhase::Idle;
    GatherErrandAction action = GatherErrandAction::None;
    char const* stateLabel = "";
    char const* failCode = "";
};

inline GatherErrandStep StepGatherErrand(GatherErrandFacts const& f)
{
    if (!f.inWorld)
        return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:not-in-world", "not-in-world" };
    if (!f.alive)
        return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:dead", "dead" };
    if (f.inCombat)
        return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:danger", "danger" };

    switch (f.phase)
    {
        case GatherPhase::Walking:
            if (!f.nodeExists || !f.nodeReady)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:node-gone", "node-gone" };
            if (f.inInteractRange)
                return { GatherPhase::Casting, GatherErrandAction::CastGather, "pending:casting", "" };
            if (f.phaseElapsedMs >= f.walkTimeoutMs)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:timeout", "timeout" };
            return {
                GatherPhase::Walking,
                f.isMoving ? GatherErrandAction::Wait : GatherErrandAction::MoveToNode,
                "pending:walking",
                ""
            };

        case GatherPhase::Casting:
            if (f.lootAvailable)
                return { GatherPhase::Looting, GatherErrandAction::LootNode, "pending:looting", "" };
            if (!f.nodeExists)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:node-gone", "node-gone" };
            if (f.phaseElapsedMs >= f.castTimeoutMs)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:timeout", "timeout" };
            if (!f.isCasting && f.phaseElapsedMs >= 1500)
                return {
                    GatherPhase::Failed,
                    GatherErrandAction::FinishFail,
                    f.nodeReady ? "fail:timeout" : "fail:node-gone",
                    f.nodeReady ? "timeout" : "node-gone"
                };
            return { GatherPhase::Casting, GatherErrandAction::Wait, "pending:casting", "" };

        case GatherPhase::Looting:
            if (f.itemsGained > 0)
                return { GatherPhase::Done, GatherErrandAction::FinishOk, "ok", "" };
            if (f.lootAttempted)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:no-loot", "no-loot" };
            if (f.lootAvailable)
                return { GatherPhase::Looting, GatherErrandAction::LootNode, "pending:looting", "" };
            if (!f.nodeExists)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:node-gone", "node-gone" };
            if (f.phaseElapsedMs >= f.lootTimeoutMs)
                return { GatherPhase::Failed, GatherErrandAction::FinishFail, "fail:timeout", "timeout" };
            return { GatherPhase::Looting, GatherErrandAction::Wait, "pending:looting", "" };

        default:
            return { f.phase, GatherErrandAction::None, "", "" };
    }
}

} // namespace prof
} // namespace TortoiseBots
