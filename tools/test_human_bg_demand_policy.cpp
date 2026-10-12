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
using TortoiseBots::kHumanBgDemandMargin;
using TortoiseBots::kHumanBgDemandMaxPerCheck;
using TortoiseBots::kHumanBgDemandPerSidePerCheck;

int main()
{
    std::cout << "Starting TortoiseBots human BG demand policy tests...\n";

    // Target is the core match minimum plus the margin, not the team size.
    CHECK(HumanBgSideTarget(4, 10) == 4 + kHumanBgDemandMargin);    // WSG
    CHECK(HumanBgSideTarget(5, 15) == 5 + kHumanBgDemandMargin);    // AB
    CHECK(HumanBgSideTarget(15, 40) == 15 + kHumanBgDemandMargin);  // AV
    CHECK(HumanBgSideTarget(4, 0) == 4 + kHumanBgDemandMargin);     // broken template max
    CHECK(HumanBgSideTarget(4, 4) == 4);                            // never above a full side
    CHECK(HumanBgSideTarget(1, 10) == 1);                           // margin stays below min
    CHECK(HumanBgSideTarget(0, 10) == 0);
    // Alterac.MinPlayersInQueue raises the base so AV can still form.
    CHECK(HumanBgSideTarget(15, 40, 20) == 20 + kHumanBgDemandMargin);
    CHECK(HumanBgSideTarget(15, 40, 5) == 15 + kHumanBgDemandMargin);

    // No second match from leftovers: the core takes at least the minimum
    // per side into the human's match, so what stays queued per side is at
    // most target - min, which must stay below the minimum.
    for (std::uint32_t min = 1; min <= 40; ++min)
        CHECK(HumanBgSideTarget(min, 40) - min < min);
    // The AV overshoot the old team-size target allowed (40 - 15 = 25 left
    // a side, enough for a second 15v15) is gone.
    CHECK(HumanBgSideTarget(15, 40) - 15 < 15);

    // Solo Alliance human in WSG: BOTH sides want bots (the old rule filled
    // only Horde). Own side needs 4 more, the other side 5.
    std::uint32_t target = HumanBgSideTarget(4, 10);
    CHECK(HumanBgSideNeed(target, 1) == 4);
    CHECK(HumanBgSideNeed(target, 0) == 5);
    // Waiting bots count toward the side; a full side needs nothing.
    CHECK(HumanBgSideNeed(target, 5) == 0);
    CHECK(HumanBgSideNeed(target, 12) == 0);

    // Per-check share per side keeps both sides growing together.
    CHECK(HumanBgSideWantsBot(9, 0));
    CHECK(HumanBgSideWantsBot(9, kHumanBgDemandPerSidePerCheck - 1));
    CHECK(!HumanBgSideWantsBot(9, kHumanBgDemandPerSidePerCheck));
    CHECK(!HumanBgSideWantsBot(0, 0));
    // A bot group leader queues its whole group: only when it fits.
    CHECK(HumanBgSideWantsBot(5, 0, 5));
    CHECK(!HumanBgSideWantsBot(4, 0, 5));
    CHECK(HumanBgSideWantsBot(1, 0, 0));  // size 0 treated as solo

    // Pace: one check lifts a solo WSG queue past the core minimum on both
    // sides (1 human + 4 own bots >= 4, 5 enemy bots >= 4).
    CHECK(HumanBgSideNeed(target, 1) <= kHumanBgDemandPerSidePerCheck);
    CHECK(HumanBgSideNeed(target, 0) <= kHumanBgDemandPerSidePerCheck);
    CHECK(kHumanBgDemandMaxPerCheck >= 2 * kHumanBgDemandPerSidePerCheck);
    // AV minimum (15 a side) within a few checks, well under two minutes.
    std::uint32_t checksForAv = (15 + kHumanBgDemandPerSidePerCheck - 1) / kHumanBgDemandPerSidePerCheck;
    CHECK(checksForAv * kHumanBgDemandIntervalMs <= 30000);

    std::cout << "All human BG demand policy tests passed.\n";
    return 0;
}
