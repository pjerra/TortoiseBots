#pragma once

// Guild Farm orders on an item that only fishing can yield: the order-side
// facts for the terminal farm step. The bot brain's fishing order path
// (ai/playerbot/GuildOrderSources.cpp, FishAction.cpp) is not part of the
// ForeverCore slice, so these orders cannot be worked here; this file lets the
// store answer with the verdict the fishing plan itself would give instead of
// a generic no-source, and keeps the fish source index for a later executor.
//
// World thread only: the index is built once at order-store load, and the
// verdict is read on a bot tick (memory only, no database access there).
#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <optional>

class Player;

namespace tbhost
{
namespace guild_order_fish
{

// One pass over fishing_loot_template (open water) and the fishing-hole game
// object loot, references expanded through reference_loot_template. Called
// from guild_orders::LoadOrders at boot; an unavailable world database or
// table leaves the index empty, which makes every query answer "not fish".
void LoadFishingSources();

// True when fishing loot can yield the item in any zone. False before the
// index is built.
bool IsFishingSource(std::uint32_t itemId);

// Lowest positive skill_fishing_base_level among the zones that yield the
// item. 0 when no zone has a positive requirement (always reachable) or the
// item is not a fishing source.
std::uint32_t MinFishingZoneSkill(std::uint32_t itemId);

// Whether this build can execute a fishing order at all. False today: no
// fishing action is registered on ForeverCore, and nothing casts the fishing
// spell, uses the bobber or takes the catch.
bool HostFishingAvailable();

// The reason a Farm order that can neither craft nor gather the item must end
// with, when the item is a fishing source. nullopt for anything else, so the
// caller keeps its normal no-source path.
std::optional<TortoiseBots::GuildOrders::OrderReason> ResolveFarmRefusal(Player* bot, std::uint32_t itemId);

} // namespace guild_order_fish
} // namespace tbhost
