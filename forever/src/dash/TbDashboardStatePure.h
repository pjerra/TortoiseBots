#pragma once

// The dashboard's macro-state words and the idle/busy/stalled refinement, shared by the roster sender and its test.
// A bot that stands still is busy while it did real work recently, and stalled when its only recent activity was
// churn (an active travel target or a changing last-action name). Kept core-free so the rules are testable alone.

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace dashboard_state
{
enum MacroState : std::uint8_t
{
    Combat = 0,
    Moving = 1,
    Busy = 2,
    Stalled = 3,
    Resting = 4,
    Dead = 5,
    Idle = 6,
    StateCount = 7,
};

constexpr std::uint32_t IdleAfterMs = 45000;
constexpr std::uint32_t RecentFightMs = 60000;

inline char const* Name(std::uint8_t state)
{
    switch (state)
    {
        case Combat: return "combat";
        case Moving: return "moving";
        case Busy: return "busy";
        case Stalled: return "stalled";
        case Resting: return "resting";
        case Dead: return "dead";
        default: return "idle";
    }
}

// A base state of Idle is refined by the activity clock: anything inside IdleAfterMs is busy, unless the whole run
// was churn and no fight or XP gain happened inside RecentFightMs (then it is stalled). All times are the same
// millisecond clock; 0 means never observed.
inline std::uint8_t RefineIdle(std::uint8_t base, std::uint32_t nowMs, std::uint32_t lastActivityMs,
    std::uint32_t churnSinceMs, std::uint32_t lastCombatMs, std::uint32_t lastXpMs)
{
    if (base != Idle)
        return base;
    if (lastActivityMs != 0 && (nowMs - lastActivityMs) < IdleAfterMs)
    {
        bool const churnOnly = churnSinceMs != 0 && (nowMs - churnSinceMs) >= IdleAfterMs;
        bool const recentFight = (lastCombatMs != 0 && (nowMs - lastCombatMs) < RecentFightMs) ||
            (lastXpMs != 0 && (nowMs - lastXpMs) < RecentFightMs);
        return churnOnly && !recentFight ? Stalled : Busy;
    }
    return Idle;
}
}
}
