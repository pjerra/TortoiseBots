// Pure unit tests for the fishing-order verdict (TbHostGuildOrderFishPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_guild_order_fish_host.cpp -o /tmp/test_guild_order_fish_host && /tmp/test_guild_order_fish_host

#include "forever/src/host/TbHostGuildOrderFishPure.h"

#include <cstdio>

using namespace tbhost::guild_order_fish;
using namespace TortoiseBots::GuildOrders;

static int g_failures = 0;
#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (!(cond))                                                                \
        {                                                                           \
            ++g_failures;                                                           \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);                   \
        }                                                                           \
    } while (0)

int main()
{
    // Not a fishing source: the store keeps its normal path.
    CHECK(DecideFishFarmVerdict(false, true, true, false) == FishFarmVerdict::NotFish);
    CHECK(DecideFishFarmVerdict(false, false, false, false) == FishFarmVerdict::NotFish);

    // A bot without Fishing answers noskill even when the build could not fish
    // and even when its gear could not reach a zone.
    CHECK(DecideFishFarmVerdict(true, false, true, false) == FishFarmVerdict::NoSkill);
    CHECK(DecideFishFarmVerdict(true, false, false, true) == FishFarmVerdict::NoSkill);

    // Gear that cannot reach any source zone answers lowskill before the
    // build limit.
    CHECK(DecideFishFarmVerdict(true, true, false, false) == FishFarmVerdict::LowSkill);
    CHECK(DecideFishFarmVerdict(true, true, false, true) == FishFarmVerdict::LowSkill);

    // Skill and reach are fine, but no fishing executor is built: nosource.
    CHECK(DecideFishFarmVerdict(true, true, true, false) == FishFarmVerdict::BuildCannotFish);
    // A future executor makes the order workable.
    CHECK(DecideFishFarmVerdict(true, true, true, true) == FishFarmVerdict::Workable);

    // Reason words and the state each maps to (runtime/GuildOrderContract.h).
    CHECK(FishFarmReason(FishFarmVerdict::NoSkill) == OrderReason::NoSkill);
    CHECK(FishFarmReason(FishFarmVerdict::LowSkill) == OrderReason::LowSkill);
    CHECK(FishFarmReason(FishFarmVerdict::BuildCannotFish) == OrderReason::NoSource);
    CHECK(FishFarmReason(FishFarmVerdict::NotFish) == OrderReason::None);
    CHECK(FishFarmReason(FishFarmVerdict::Workable) == OrderReason::None);

    CHECK(StateForReason(FishFarmReason(FishFarmVerdict::NoSkill)) == OrderState::NoSkill);
    CHECK(StateForReason(FishFarmReason(FishFarmVerdict::LowSkill)) == OrderState::NoSkill);
    CHECK(StateForReason(FishFarmReason(FishFarmVerdict::BuildCannotFish)) == OrderState::Cant);

    if (g_failures == 0)
        std::printf("PASS: test_guild_order_fish_host\n");
    else
        std::printf("FAIL: test_guild_order_fish_host (%d failed)\n", g_failures);
    return g_failures ? 1 : 0;
}
