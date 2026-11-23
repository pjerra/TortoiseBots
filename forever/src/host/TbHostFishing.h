#pragma once

// Host fishing for guild Farm orders on a fish (TBF #155). The legacy
// FishAction/FishValues travel kit is not in the ForeverCore slice (no travel
// strategy, no ChooseTravelTargetAction, no CastCustomSpellAction), so the
// order path fishes here: the bot walks to a spot from the shipped
// FISH_LOCATION data, casts 7731, uses its own bobber when it splashes and
// takes the catch. The errand state is world-thread only and memory only.
#include "ObjectGuid.h"

#include <cstdint>
#include <string>

class Player;

namespace tbhost
{
namespace fishing
{

// The order item is fishing loot that this bot's Fishing skill (with the
// granted pole and lure) can reach. Cheap: no world scan.
bool CanFishOrder(Player* bot, std::uint32_t itemId);

// Learn Fishing and put the pole (and, when the zone needs it, a lure stack)
// in the bags for this order. Idempotent; does nothing for a non-fish item.
bool EnsureOrderFishing(Player* bot, std::uint32_t itemId);

// Pick a fish spot on the bot's map and start one catch. False with a reason
// when no reachable spot exists ("no-water") or the bot cannot cast.
bool StartOrderErrand(Player* bot, std::uint32_t itemId, std::string& outReason);

// One per-bot tick: walk, cast, use the bobber, take the loot.
void TickErrand(Player* bot, std::uint32_t diffMs);

bool HasActiveErrand(Player const* bot);
std::string ActiveErrandPhase(Player const* bot);

// True once after an errand finished with a catch (any fish, not only the
// ordered one); consumed by the order step so a working bot is not counted
// as a failed tick while the ordered fish is still due.
bool TakeCaught(Player* bot);

void CancelErrand(Player const* bot);
void ClearErrand(ObjectGuid const& botGuid);

// True when a fishing order can be executed at all on this build.
bool HostAvailable();

} // namespace fishing
} // namespace tbhost
