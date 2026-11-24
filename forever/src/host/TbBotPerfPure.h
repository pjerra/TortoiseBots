// The periodic BOTPERF line's numbers, without the core. TBF #142 ports TB's line
// (runtime/BotManager.cpp in TB) onto the window the world hook already measures for
// `.tb perf`: the module pass cost and the world tick it ran inside.
#pragma once

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace perf
{
// One BOTPERF window, accumulated per world tick and reset when the line is written.
struct Window
{
    std::uint32_t elapsedMs = 0;
    std::uint32_t ticks = 0;
    std::uint64_t passSumUs = 0;
    std::uint32_t maxPassUs = 0;
    std::uint64_t worldSumMs = 0;
};

inline std::uint32_t AveragePassUs(Window const& window)
{
    return window.ticks ? static_cast<std::uint32_t>(window.passSumUs / window.ticks) : 0;
}

inline std::uint32_t AverageWorldMs(Window const& window)
{
    return window.ticks ? static_cast<std::uint32_t>(window.worldSumMs / window.ticks) : 0;
}

// What the core and everything but this module spent per tick: the world tick minus the bot pass.
// Never negative (a pass that outran the last world update reads 0).
inline std::uint32_t CoreMs(Window const& window)
{
    std::uint64_t const worldUs = static_cast<std::uint64_t>(AverageWorldMs(window)) * 1000;
    std::uint64_t const passUs = AveragePassUs(window);
    return worldUs > passUs ? static_cast<std::uint32_t>((worldUs - passUs) / 1000) : 0;
}

// `passUs`/`maxUs` are the module pass in microseconds, `coreMs` the rest of the world tick, and the
// six counts are the fleet the line was measured on.
inline std::string BotPerfLine(Window const& window, std::uint32_t bots, std::uint32_t engines, std::uint32_t owned,
    std::uint32_t mastered, std::uint32_t freeBots, std::uint32_t pool)
{
    return "TortoiseBots: BOTPERF passUs=" + std::to_string(AveragePassUs(window)) +
        " maxUs=" + std::to_string(window.maxPassUs) +
        " ticks=" + std::to_string(window.ticks) +
        " coreMs=" + std::to_string(CoreMs(window)) +
        " bots=" + std::to_string(bots) +
        " engines=" + std::to_string(engines) +
        " owned=" + std::to_string(owned) +
        " mastered=" + std::to_string(mastered) +
        " free=" + std::to_string(freeBots) +
        " pool=" + std::to_string(pool);
}
} // namespace perf
} // namespace TortoiseBots
