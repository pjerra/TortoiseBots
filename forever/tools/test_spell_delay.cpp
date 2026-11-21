// Standalone check of host/TbSpellDelayPure.h: how much a pushback the core reported (SMSG_SPELL_DELAYED) adds to the engine's wait. No game types.
//   cd <worktree> && clang++ -std=c++20 -I forever/src forever/tools/test_spell_delay.cpp -o /tmp/test_spell_delay && /tmp/test_spell_delay
#include "host/TbSpellDelayPure.h"

#include <cstdint>
#include <cstdio>
#include <limits>

using TortoiseBots::sd::FailureWaitMs;
using TortoiseBots::sd::PushbackExtraMs;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, char const* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
}

int main()
{
    // The legacy packet case (PlayerbotAIPackets.cpp:202-217) adds a delay of at most 1000 ms to the engine's wait: the bar the engine waits
    // on keeps its length, so the next action does not start before the bar ends.
    Check(PushbackExtraMs(500) == 500, "pushback_500_extends_wait_500");
    // Both ends of the accepted range, and what the core itself sends (500 ms by default, Spell::Delayed at the pin).
    Check(PushbackExtraMs(1) == 1, "pushback_1_extends_wait_1");
    Check(PushbackExtraMs(1000) == 1000, "pushback_1000_extends_wait_1000");
    // Outside the range the legacy handler ignored the packet: the wait is not touched (0 extra), never shortened or invented.
    Check(PushbackExtraMs(0) == 0, "pushback_0_adds_nothing");
    Check(PushbackExtraMs(1001) == 0, "pushback_1001_adds_nothing");
    Check(PushbackExtraMs(-1) == 0, "pushback_negative_adds_nothing");
    Check(PushbackExtraMs(std::numeric_limits<int64_t>::min()) == 0, "pushback_int64_min_adds_nothing");
    Check(PushbackExtraMs(std::numeric_limits<int64_t>::max()) == 0, "pushback_int64_max_adds_nothing");

    // The failure re-arm on a millisecond clock. A 3000 ms bar (engine wait 3000 + react delay) started at t = 10000 ms; the target dies 400 ms
    // in, so the failure lands in the same wall second as the cast start. The legacy whole-second rule does nothing there (the wait would stay
    // ~3 s); the host's rule cuts it to the rest of the 1500 ms global cooldown.
    uint32_t const barWaitMs = 3000 + 100;
    Check(FailureWaitMs(10000, 10400, 1500, 100) == 1100, "failure_same_second_cuts_wait_to_gcd_rest");
    Check(FailureWaitMs(10000, 10400, 1500, 100) < barWaitMs, "failure_same_second_wait_is_cut_below_the_bar");
    Check(FailureWaitMs(10000, 10000, 1500, 100) == 1500, "failure_at_cast_start_waits_the_whole_gcd");
    Check(FailureWaitMs(10000, 11500, 1500, 100) == 100, "failure_at_gcd_end_waits_react_delay");
    Check(FailureWaitMs(10000, 13000, 1500, 100) == 100, "failure_after_gcd_waits_react_delay");
    Check(FailureWaitMs(10000, 10400, 0, 100) == 100, "failure_without_gcd_waits_react_delay");
    Check(FailureWaitMs(10000, 10400, -5, 100) == 100, "failure_negative_gcd_waits_react_delay");
    Check(FailureWaitMs(0xFFFFFF00u, 0x100u, 1500, 100) == 1500 - 0x200u, "failure_across_ms_clock_wrap");

    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
