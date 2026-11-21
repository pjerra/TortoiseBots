#include "../../ai/playerbot/policy/death/DeathReleasePausePolicy.h"
#include "../../ai/playerbot/policy/travel/WalkTargetOffsetPolicy.h"
#include "../../ai/playerbot/policy/world/RareHuntPolicy.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static void TestDeathReleasePause()
{
    using namespace TortoiseBots;

    // Zero max disables the pause.
    DeathReleasePauseState s;
    assert(!ShouldHoldDeathRelease(s, 1000, 0, 0, 500));
    assert(!s.active);

    // Random delay latched within [min, max] seconds and held until elapsed.
    ResetDeathReleasePause(s);
    assert(ShouldHoldDeathRelease(s, 10000, 3, 10, 4000));
    assert(s.active);
    assert(s.delayMs >= 3000u && s.delayMs <= 10000u);
    assert(s.delayMs == 7000u);
    assert(ShouldHoldDeathRelease(s, 16999, 3, 10, 9999)); // later roll ignored while active
    assert(!ShouldHoldDeathRelease(s, 17000, 3, 10, 9999));

    // Reset clears latched timer for the next death.
    ResetDeathReleasePause(s);
    assert(!s.active);
    assert(ShouldHoldDeathRelease(s, 20000, 5, 2, 123)); // max < min clamps to min (5s)
    assert(s.delayMs == 5000u);
    assert(!ShouldHoldDeathRelease(s, 25000, 5, 2, 123));

    // Unsigned wrap-around safety.
    ResetDeathReleasePause(s);
    uint32_t const nearWrap = 0xFFFFFF00u;
    assert(ShouldHoldDeathRelease(s, nearWrap, 2, 2, 0));
    assert(ShouldHoldDeathRelease(s, nearWrap + 1999u, 2, 2, 0));
    assert(!ShouldHoldDeathRelease(s, nearWrap + 2000u, 2, 2, 0));
}

static void TestWalkTargetOffset()
{
    using namespace ai;

    // Never offset in instances, on unit targets, exact sites, or short distances.
    assert(!ShouldOffsetWalkTarget(true, false, false, 50.0f, 2.5f));
    assert(!ShouldOffsetWalkTarget(false, true, false, 50.0f, 2.5f));
    assert(!ShouldOffsetWalkTarget(false, false, true, 50.0f, 2.5f));
    assert(!ShouldOffsetWalkTarget(false, false, false, 14.9f, 2.5f));
    assert(!ShouldOffsetWalkTarget(false, false, false, 50.0f, 0.0f));
    assert(ShouldOffsetWalkTarget(false, false, false, 15.0f, 2.5f));

    // Bounded radius, deterministic per bot, distinct across bots.
    float dx1 = 0.0f, dy1 = 0.0f;
    float dx1Repeat = 0.0f, dy1Repeat = 0.0f;
    float dx2 = 0.0f, dy2 = 0.0f;
    ComputeWalkTargetOffset(101u, -8940.0f, -132.0f, 2.5f, dx1, dy1);
    ComputeWalkTargetOffset(101u, -8940.0f, -132.0f, 2.5f, dx1Repeat, dy1Repeat);
    ComputeWalkTargetOffset(102u, -8940.0f, -132.0f, 2.5f, dx2, dy2);

    float const r1 = std::hypot(dx1, dy1);
    float const r2 = std::hypot(dx2, dy2);
    assert(r1 >= 0.25f * 2.5f - 1e-4f && r1 <= 2.5f + 1e-4f);
    assert(r2 >= 0.25f * 2.5f - 1e-4f && r2 <= 2.5f + 1e-4f);
    assert(dx1 == dx1Repeat && dy1 == dy1Repeat);
    assert(std::fabs(dx1 - dx2) > 1e-3f || std::fabs(dy1 - dy2) > 1e-3f);
}

static void TestRareHuntPolicy()
{
    using namespace TortoiseBots::world;

    // Disabled by flag or non-free / mastered / BG bots.
    assert(!ShouldHuntRare(false, true, false, false, kCreatureRankRare, false, 10, 10, 1));
    assert(!ShouldHuntRare(true, false, false, false, kCreatureRankRare, false, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, true, false, kCreatureRankRare, false, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, false, true, kCreatureRankRare, false, 10, 10, 1));

    // Normal rare (rank 4) is solo-huntable; rare elite (rank 2) requires canFightElite;
    // plain elite / worldboss / normal are not rares.
    assert(ShouldHuntRare(true, true, false, false, kCreatureRankRare, false, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankRareElite, false, 10, 10, 1));
    assert(ShouldHuntRare(true, true, false, false, kCreatureRankRareElite, true, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankElite, true, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankWorldBoss, true, 10, 10, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankNormal, true, 10, 10, 1));

    // Level window: [botLevel - 3, botLevel + maxLevelOver].
    assert(ShouldHuntRare(true, true, false, false, kCreatureRankRare, false, 10, 7, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankRare, false, 10, 6, 1));
    assert(ShouldHuntRare(true, true, false, false, kCreatureRankRare, false, 10, 11, 1));
    assert(!ShouldHuntRare(true, true, false, false, kCreatureRankRare, false, 10, 12, 1));
}

int main()
{
    TestDeathReleasePause();
    TestWalkTargetOffset();
    TestRareHuntPolicy();
    std::printf("PASS: test_polish_policy (3 groups)\n");
    return 0;
}
