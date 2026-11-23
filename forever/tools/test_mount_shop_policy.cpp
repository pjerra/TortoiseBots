// Standalone pure unit tests for forever/src/host/TbMountShopPure.h.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_mount_shop_policy.cpp -o /tmp/test_mount_shop_policy && /tmp/test_mount_shop_policy
#include "forever/src/host/TbMountShopPure.h"

#include <cstdio>

namespace M = tbhost::mountshop;

static int g_checks = 0;

static void Check(bool ok, char const* what, int line)
{
    ++g_checks;
    if (!ok)
        std::printf("FAIL line %d: %s\n", line, what);
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

int main()
{
    // Riding ladder: nothing below 40, 75 at 40..59, 150 at 60.
    CHECK(M::RidingTargetForLevel(1) == 0);
    CHECK(M::RidingTargetForLevel(39) == 0);
    CHECK(M::RidingTargetForLevel(40) == M::kApprenticeRiding);
    CHECK(M::RidingTargetForLevel(59) == M::kApprenticeRiding);
    CHECK(M::RidingTargetForLevel(60) == M::kJourneymanRiding);
    CHECK(!M::IsEpicTier(59));
    CHECK(M::IsEpicTier(60));

    // Below 40 nothing is wanted, whatever is carried.
    CHECK(M::PickNeed(39, 0, false, false) == M::Need::None);
    CHECK(M::PickNeed(39, 75, false, false) == M::Need::None);

    // Level 40: riding 75 first, then the normal mount.
    CHECK(M::PickNeed(40, 0, false, false) == M::Need::TrainRiding);
    CHECK(M::PickNeed(40, 74, false, false) == M::Need::TrainRiding);
    CHECK(M::PickNeed(40, 75, false, false) == M::Need::BuyMount);
    CHECK(M::PickNeed(40, 75, true, false) == M::Need::None);
    // An epic mount cannot be used at 40; it does not stand in for the normal tier.
    CHECK(M::PickNeed(40, 75, false, true) == M::Need::BuyMount);

    // Level 60: Journeyman Riding, then the epic mount.
    CHECK(M::PickNeed(60, 0, true, false) == M::Need::TrainRiding);
    CHECK(M::PickNeed(60, 75, true, false) == M::Need::TrainRiding);
    CHECK(M::PickNeed(60, 150, true, false) == M::Need::BuyMount);
    CHECK(M::PickNeed(60, 150, false, true) == M::Need::None);
    CHECK(M::PickNeed(60, 150, true, true) == M::Need::None);
    // A normal mount does not satisfy the epic tier.
    CHECK(M::PickNeed(60, 150, false, false) == M::Need::BuyMount);

    // The thresholds match the policy's item lists (normal at 40, epic at 60).
    CHECK(M::kMountLevel == 40);
    CHECK(M::kEpicMountLevel == 60);
    CHECK(M::kApprenticeRiding == 75);
    CHECK(M::kJourneymanRiding == 150);

    std::printf("PASS test_mount_shop_policy: %d checks\n", g_checks);
    return g_checks == 0 ? 1 : 0;
}
