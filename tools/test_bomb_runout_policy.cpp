#include "../ai/playerbot/BombRunoutPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main()
{
    std::cout << "Starting TortoiseBots bomb runout policy tests...\n";

    float cx, cy, nearest;
    std::size_t nearestIdx;

    // Nobody else: nothing to run from.
    {
        bool hasDetonateMana = false;
        for (std::size_t i = 0; i < kRaidBombSpellCount; ++i)
            hasDetonateMana |= (kRaidBombSpellIds[i] == 27819);
        CHECK(hasDetonateMana && kRaidBombSpellCount == 6);
    }
    CHECK(!BombRunoutCentre(0.0f, 0.0f, nullptr, 0, cx, cy, nearest, nearestIdx));

    // Raid clump of four around (0,0); the carrier stands on its east edge.
    DebuffAnchor raid[4];
    raid[0].x = 5.0f;  raid[0].y = 0.0f;
    raid[1].x = -5.0f; raid[1].y = 0.0f;
    raid[2].x = 0.0f;  raid[2].y = 5.0f;
    raid[3].x = 0.0f;  raid[3].y = -5.0f;
    CHECK(BombRunoutCentre(8.0f, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
    CHECK(Near(cx, 0.0f) && Near(cy, 0.0f));
    CHECK(Near(nearest, 3.0f) && nearestIdx == 0);
    CHECK(!IsBombRunoutClear(nearest, 30.0f));
    std::cout << "  [PASS] centre is the raid centroid, nearest member found\n";

    // Heads straight away from the centre (east), not toward the far side.
    float angle = BombRunoutAwayAngle(8.0f, 0.0f, cx, cy, raid[0].x, raid[0].y);
    CHECK(Near(angle, 0.0f));
    // Master/nearest anchoring regression: a carrier west of one member
    // but east of the centre still runs east (away from the raid).
    angle = BombRunoutAwayAngle(3.0f, 0.0f, cx, cy, raid[0].x, raid[0].y);
    CHECK(Near(angle, 0.0f));
    // On the centre exactly: away from the nearest member.
    angle = BombRunoutAwayAngle(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);
    CHECK(Near(angle, -1.5707963f));
    std::cout << "  [PASS] runs away from the raid centre\n";

    // One step clears every member by radius + margin.
    CHECK(BombRunoutCentre(8.0f, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
    float step = BombRunoutStep(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4);
    float endX = 8.0f + step;
    float clear = DebuffEscapeClearance(endX, 0.0f, raid, 4);
    CHECK(clear >= 30.0f + kBombRunoutMarginYd);
    CHECK(step <= 30.0f);  // no overshoot: donor radius - nearest = 27, +2yd grain
    CHECK(BombRunoutCentre(endX, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
    CHECK(IsBombRunoutClear(nearest, 30.0f));
    std::cout << "  [PASS] one move gets the carrier clear, then it stops\n";

    // Off-axis member: the step grows past radius - nearest until clear.
    DebuffAnchor wide[2];
    wide[0].x = 0.0f; wide[0].y = 0.0f;
    wide[1].x = 10.0f; wide[1].y = 10.0f;
    CHECK(BombRunoutCentre(10.0f, 0.0f, wide, 2, cx, cy, nearest, nearestIdx));
    angle = BombRunoutAwayAngle(10.0f, 0.0f, cx, cy, wide[nearestIdx].x, wide[nearestIdx].y);
    step = BombRunoutStep(10.0f, 0.0f, angle, nearest, 30.0f, wide, 2);
    CHECK(step > 30.0f - nearest);
    CHECK(DebuffEscapeClearance(10.0f + std::cos(angle) * step, std::sin(angle) * step, wide, 2) >= 30.0f + kBombRunoutMarginYd);
    std::cout << "  [PASS] off-axis members are cleared too\n";

    // Already clear: stays put.
    CHECK(BombRunoutCentre(40.0f, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
    CHECK(IsBombRunoutClear(nearest, 30.0f));
    std::cout << "  [PASS] a clear carrier does not move\n";

    std::cout << "All bomb runout policy tests passed.\n";
    return 0;
}
