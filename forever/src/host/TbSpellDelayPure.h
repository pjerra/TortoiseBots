// S2: how much a pushback the core reported on the bot's own cast bar (SMSG_SPELL_DELAYED) adds to the engine's wait. Core-free, so a unit
// test proves it (forever/tools/test_spell_delay.cpp); TbEngineBrain::DeliverCastResult asks it before it lengthens the wait.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace sd
{
// The legacy packet case (PlayerbotAIPackets.cpp:202-217) added the reported delay to the engine's internal update when it was at most
// 1000 ms and ignored anything above; 0 adds nothing. A negative or out-of-range value adds nothing too: an unknown delay never shortens
// the wait the engine keeps for a cast bar, and the bar itself keeps its core length either way.
inline uint32_t PushbackExtraMs(int64_t delayMs)
{
    return delayMs >= 1 && delayMs <= 1000 ? uint32_t(delayMs) : 0U;
}

// What the engine waits after a cast of its own ended badly (the async SMSG_CAST_FAILED / SMSG_SPELL_FAILURE): the rest of the global
// cooldown, or the react delay when the cooldown is spent. The same rule as PlayerbotAI::SpellInterrupted (PlayerbotAI.cpp:3172-3194), but
// on a millisecond clock: the legacy one counts whole wall seconds (time(0)) and does nothing when the failure lands in the second the
// cast started, which leaves the engine waiting out the whole bar. Wrap-safe (getMSTime wraps at 2^32 ms).
inline uint32_t FailureWaitMs(uint32_t startMs, uint32_t nowMs, int32_t globalCooldownMs, uint32_t reactDelayMs)
{
    uint32_t const spentMs = nowMs - startMs;
    uint32_t const gcd = globalCooldownMs > 0 ? uint32_t(globalCooldownMs) : 0U;
    return spentMs < gcd ? gcd - spentMs : reactDelayMs;
}
}
}
