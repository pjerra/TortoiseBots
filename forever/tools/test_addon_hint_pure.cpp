// The add-on hint rule without the core (forever/src/host/TbHostAddonHintPure.h).
//   g++ -std=c++17 -Wall -Wextra -I. -Iforever/src forever/tools/test_addon_hint_pure.cpp -o /tmp/t && timeout 30 /tmp/t
#include "forever/src/host/TbHostAddonHintPure.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::addonhint::pure;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main()
{
    // 1. A character already told (the store row, or seen with the add-on
    //    before) never gets the line, however long it stays.
    {
        CHECK(Decide(true, false, 0, 0, 45000) == HintStep::Wait);
        CHECK(Decide(true, false, 0, 100, 45000) == HintStep::Wait);
        CHECK(Decide(true, false, 0, 45000, 45000) == HintStep::Wait);
        CHECK(Decide(true, false, 0, 0xFFFFFFFFu, 45000) == HintStep::Wait);
    }
    // 2. A TBM message arrived this session: the entry is dropped, even before
    //    the delay and even when the store has no row.
    {
        CHECK(Decide(false, true, 0, 0, 45000) == HintStep::DropSeen);
        CHECK(Decide(false, true, 0, 44, 45000) == HintStep::DropSeen);
        CHECK(Decide(true, true, 0, 45000, 45000) == HintStep::DropSeen);
    }
    // 2b. An elevated account without the add-on gets nothing, even when the
    //     delay has passed; a normal player at the same clock is told.
    {
        CHECK(Decide(false, false, 0, 45000, 45000, true) == HintStep::DropStaff);
        CHECK(Decide(false, false, 0, 0xFFFFFFFFu, 45000, true) == HintStep::DropStaff);
        CHECK(Decide(true, false, 0, 45000, 45000, true) == HintStep::DropStaff);
        CHECK(Decide(false, false, 0, 44999, 45000, true) != HintStep::Send);
        CHECK(Decide(false, false, 0, 45000, 45000, false) == HintStep::Send);
        CHECK(Decide(false, false, 0, 0, 45000, false) == HintStep::Wait);
    }
    // 3. The delay itself: one millisecond short is Wait, at it is Send.
    {
        CHECK(Decide(false, false, 1000, 1000 + 44999, 45000) == HintStep::Wait);
        CHECK(Decide(false, false, 1000, 1000 + 44999, 45000) != HintStep::Send);
        CHECK(Decide(false, false, 1000, 1000 + 45000, 45000) == HintStep::Send);
        CHECK(Decide(false, false, 0, 0, 45000) == HintStep::Wait);
    }
    // 4. The millisecond clock wraps (uint32 subtraction): a login just before
    //    the wrap still measures the delay that passed after it.
    {
        uint32_t const login = 0xFFFFF000u;   // ~4 s before the wrap
        CHECK(Decide(false, false, login, login + 44999u, 45000) == HintStep::Wait);
        CHECK(Decide(false, false, login, login + 45000u, 45000) == HintStep::Send);
        CHECK(Decide(false, false, 0xFFFF0000u, 0x00001000u, 45000) == HintStep::Send);   // 69,632 ms across the wrap
        CHECK(Decide(false, false, 0xFFFFFFF0u, 0x0000000Fu, 45000) == HintStep::Wait);   // 31 ms across the wrap
    }
    // 5. The delay clamp 10 .. 300 seconds.
    {
        CHECK(ClampDelaySeconds(0) == 10u);
        CHECK(ClampDelaySeconds(-5) == 10u);
        CHECK(ClampDelaySeconds(9) == 10u);
        CHECK(ClampDelaySeconds(10) == 10u);
        CHECK(ClampDelaySeconds(45) == 45u);
        CHECK(ClampDelaySeconds(300) == 300u);
        CHECK(ClampDelaySeconds(301) == 300u);
        CHECK(ClampDelaySeconds(999) == 300u);
    }
    // 6. The line: carries the "add-on" word the scenario greps for, the url
    //    when there is one, and no "Get it at" clause when the url is empty.
    {
        std::string const full = HintText("https://example.invalid/tbfm");
        CHECK(full.find("add-on") != std::string::npos);
        CHECK(full.find("TortoiseBots: ") == 0);
        CHECK(full.find("Get it at https://example.invalid/tbfm") != std::string::npos);
        CHECK(full.find("(shown once)") != std::string::npos);
        CHECK(full.find("TortoiseBotsForeverManager") != std::string::npos);

        std::string const bare = HintText("");
        CHECK(bare.find("add-on") != std::string::npos);
        CHECK(bare.find("Get it at") == std::string::npos);
        CHECK(bare.find("(shown once)") != std::string::npos);
        CHECK(full.size() > bare.size());
    }
    if (failures)
    {
        std::printf("test_addon_hint_pure: %d failed\n", failures);
        return 1;
    }
    std::printf("test_addon_hint_pure: all checks passed\n");
    return 0;
}
