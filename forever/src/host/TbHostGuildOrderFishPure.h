// Core-free rules for a Farm order on an item that fishing_loot_template can
// yield. Standalone-tested by forever/tools/test_guild_order_fish_host.cpp and
// used by TbHostGuildOrderFish.cpp, which supplies the facts from the world DB
// and the bot.
#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>

namespace tbhost
{
namespace guild_order_fish
{

enum class FishFarmVerdict : std::uint8_t
{
    NotFish,           // not a fishing-loot item: leave the order to the normal path
    NoSkill,           // the bot never learned Fishing
    LowSkill,          // even the best granted pole and lure cannot reach any source zone
    BuildCannotFish,   // the bot could fish, but this build has no fishing executor
    Workable,          // a fishing executor exists and the bot reaches a source zone
};

// The order of the checks is the order the player sees: a bot without the
// profession answers noskill even if the build could not fish either; a bot
// whose gear cannot reach a source answers lowskill before the build limit.
inline FishFarmVerdict DecideFishFarmVerdict(bool fishingSource, bool botHasFishingSkill,
    bool zoneReachable, bool hostCanFish)
{
    if (!fishingSource)
        return FishFarmVerdict::NotFish;
    if (!botHasFishingSkill)
        return FishFarmVerdict::NoSkill;
    if (!zoneReachable)
        return FishFarmVerdict::LowSkill;
    if (!hostCanFish)
        return FishFarmVerdict::BuildCannotFish;
    return FishFarmVerdict::Workable;
}

// The order reason a terminal fish farm step reports; None means the caller
// keeps its own path.
inline TortoiseBots::GuildOrders::OrderReason FishFarmReason(FishFarmVerdict verdict)
{
    using TortoiseBots::GuildOrders::OrderReason;
    switch (verdict)
    {
        case FishFarmVerdict::NoSkill:         return OrderReason::NoSkill;
        case FishFarmVerdict::LowSkill:        return OrderReason::LowSkill;
        case FishFarmVerdict::BuildCannotFish: return OrderReason::NoSource;
        case FishFarmVerdict::NotFish:
        case FishFarmVerdict::Workable:
            break;
    }
    return OrderReason::None;
}

} // namespace guild_order_fish
} // namespace tbhost
