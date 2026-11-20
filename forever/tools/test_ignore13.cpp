// Standalone proof of the `.tb ignore` decision half (ai/playerbot/policy/combat/IgnoreMobPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra forever/tools/test_ignore13.cpp -o /tmp/test_ignore13 && /tmp/test_ignore13
#include "../../ai/playerbot/policy/combat/IgnoreMobPolicy.h"

#include <cstdint>
#include <cstdio>
#include <string>

using Guid = uint64_t;
using List = ai::IgnoreMobList<Guid>;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestAddRemoveClear()
{
    List list;
    CHECK(list.Add(11, "Ashenvale Bear", 1000) == List::AddResult::Added);
    CHECK(list.Contains(11, 1000));
    CHECK(!list.Contains(12, 1000));
    CHECK(list.Add(11, "Ashenvale Bear", 1001) == List::AddResult::Duplicate);
    CHECK(list.Live(1001).size() == 1);
    CHECK(list.ExpiresIn(11, 1001) == ai::kIgnoreMobExpirySec - 1);
    CHECK(list.ExpiresIn(12, 1001) == 0);
    CHECK(list.Remove(12) == false);
    CHECK(list.Remove(11) == true);
    CHECK(!list.Contains(11, 1002));
    CHECK(list.Remove(11) == false);
    CHECK(list.Add(21, "a", 1002) == List::AddResult::Added);
    CHECK(list.Add(22, "b", 1002) == List::AddResult::Added);
    CHECK(list.Clear() == 2);
    CHECK(list.Live(1002).empty());
    CHECK(list.Clear() == 0);
}

static void TestCap()
{
    List list;
    // The contract is eight mobs per commander: pin the number, not the constant.
    for (uint64_t i = 1; i <= 8; ++i)
        CHECK(list.Add(i, "mob", 5000) == List::AddResult::Added);
    CHECK(list.Add(9, "one too many", 5000) == List::AddResult::Full);
    CHECK(!list.Contains(9, 5000));
    CHECK(list.Remove(3) == true);
    CHECK(list.Add(9, "fits now", 5000) == List::AddResult::Added);
}

static void TestExpiry()
{
    List list;
    CHECK(list.Add(7, "old", 0) == List::AddResult::Added);
    CHECK(list.Contains(7, ai::kIgnoreMobExpirySec - 1));
    CHECK(!list.Contains(7, ai::kIgnoreMobExpirySec));
    CHECK(list.ExpiresIn(7, ai::kIgnoreMobExpirySec - 1) == 1);
    CHECK(list.ExpiresIn(7, ai::kIgnoreMobExpirySec) == 0);
    CHECK(list.PruneExpired(ai::kIgnoreMobExpirySec) == 1);
    CHECK(list.Live(ai::kIgnoreMobExpirySec).empty());
    // An expired entry frees its slot: the list holds eight live ones again afterwards.
    for (uint64_t i = 1; i <= 8; ++i)
        CHECK(list.Add(i, "mob", ai::kIgnoreMobExpirySec) == List::AddResult::Added);
    CHECK(list.Add(99, "extra", ai::kIgnoreMobExpirySec) == List::AddResult::Full);
    // A re-add after half the window lapses does not extend anything: it is a duplicate.
    CHECK(list.Add(1, "mob", ai::kIgnoreMobExpirySec + 10) == List::AddResult::Duplicate);
    CHECK(list.ExpiresIn(1, ai::kIgnoreMobExpirySec + 10) == ai::kIgnoreMobExpirySec - 10);
}

static void TestAoE()
{
    // At the bot: blocked inside the radius, free outside it.
    CHECK(ai::AoEBlockedByIgnored(10.0f, 29.9f, 1000.0f));
    CHECK(ai::AoEBlockedByIgnored(10.0f, 30.0f, 1000.0f));
    CHECK(!ai::AoEBlockedByIgnored(10.0f, 30.1f, 1000.0f));
    // At the blast: blocked inside blast plus the radius, free outside it.
    CHECK(ai::AoEBlockedByIgnored(10.0f, 1000.0f, 39.9f));
    CHECK(ai::AoEBlockedByIgnored(10.0f, 1000.0f, 40.0f));
    CHECK(!ai::AoEBlockedByIgnored(10.0f, 1000.0f, 40.1f));
    // A point blast still guards the radius around it.
    CHECK(ai::AoEBlockedByIgnored(0.0f, 1000.0f, 30.0f));
    CHECK(!ai::AoEBlockedByIgnored(0.0f, 1000.0f, 30.1f));
}

int main()
{
    TestAddRemoveClear();
    TestCap();
    TestExpiry();
    TestAoE();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
