// Human-demand BG backfill policy tests.
// Ported donor behaviour: mod-playerbots BGJoinAction::shouldJoinBg
// (src/Ai/Base/Actions/BattleGroundJoinAction.cpp:290-305) fills both teams.

#include "../runtime/HumanBgDemandPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::HumanBgSideNeed;
using TortoiseBots::HumanBgSideTarget;
using TortoiseBots::HumanBgSideWantsBot;
using TortoiseBots::kHumanBgDemandIntervalMs;
using TortoiseBots::kHumanBgDemandMaxPerCheck;
using TortoiseBots::kHumanBgDemandPerSidePerCheck;

int main()
{
    std::cout << "Starting TortoiseBots human BG demand policy tests...\n";

    // Target is the template team size, never below the core minimum.
    CHECK(HumanBgSideTarget(4, 10) == 10);   // WSG
    CHECK(HumanBgSideTarget(5, 15) == 15);   // AB
    CHECK(HumanBgSideTarget(15, 40) == 40);  // AV
    CHECK(HumanBgSideTarget(4, 0) == 4);     // broken template max

    // Solo Alliance human in WSG: BOTH sides want bots (the old rule filled
    // only Horde). Own side needs 9 more, the other side 10.
    std::uint32_t target = HumanBgSideTarget(4, 10);
    CHECK(HumanBgSideNeed(target, 1) == 9);
    CHECK(HumanBgSideNeed(target, 0) == 10);
    // Waiting bots count toward the side; a full side needs nothing.
    CHECK(HumanBgSideNeed(target, 10) == 0);
    CHECK(HumanBgSideNeed(target, 12) == 0);

    // Per-check share per side keeps both sides growing together.
    CHECK(HumanBgSideWantsBot(9, 0));
    CHECK(HumanBgSideWantsBot(9, kHumanBgDemandPerSidePerCheck - 1));
    CHECK(!HumanBgSideWantsBot(9, kHumanBgDemandPerSidePerCheck));
    CHECK(!HumanBgSideWantsBot(0, 0));

    // Pace: one check lifts a solo WSG queue past the core minimum on both
    // sides (1 human + 5 own bots >= 4, 5 enemy bots >= 4).
    CHECK(1 + kHumanBgDemandPerSidePerCheck >= 4);
    CHECK(kHumanBgDemandPerSidePerCheck >= 4);
    CHECK(kHumanBgDemandMaxPerCheck >= 2 * kHumanBgDemandPerSidePerCheck);
    // AV minimum (15 a side) within a few checks, well under two minutes.
    std::uint32_t checksForAv = (15 + kHumanBgDemandPerSidePerCheck - 1) / kHumanBgDemandPerSidePerCheck;
    CHECK(checksForAv * kHumanBgDemandIntervalMs <= 30000);

    std::cout << "All human BG demand policy tests passed.\n";
    return 0;
}
