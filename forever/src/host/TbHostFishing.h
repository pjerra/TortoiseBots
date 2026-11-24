#pragma once

// Host fishing on ForeverCore: guild Farm orders on a fish and free-bot idle
// fishing. The legacy FishAction/FishValues travel kit is not in the
// ForeverCore slice (no travel strategy, no ChooseTravelTargetAction, no
// CastCustomSpellAction), so both paths fish here: the bot walks to a spot,
// casts 7731, uses its own bobber when it splashes and takes the catch. The
// errand state is world-thread only and memory only.
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

// Idle fishing for a free (masterless) bot: searches a fishing hole or open
// water nearby, works one catch at a time inside the hourly session budget
// and cancels itself when `enabled` turns false. Called for every bot; cheap
// when no idle session exists.
void TickIdle(Player* bot, std::uint32_t diffMs, bool enabled);

bool HasActiveErrand(Player const* bot);
bool HasActiveIdleErrand(Player const* bot);
std::string ActiveErrandPhase(Player const* bot);

// True once after an errand finished with a catch (any fish, not only the
// ordered one); consumed by the order step so a working bot is not counted
// as a failed tick while the ordered fish is still due.
bool TakeCaught(Player* bot);

void CancelErrand(Player const* bot);
void ClearErrand(ObjectGuid const& botGuid);

// A pole left in the main hand after fishing (an errand that ended, or a
// logout while one held it) makes the bot fight with a tool: put the best
// carried melee weapon back. False when the hand is not a pole or nothing
// equippable is carried. Idempotent.
bool RepairPoleInHand(Player* bot);

// True when a fishing order can be executed at all on this build.
bool HostAvailable();

} // namespace fishing
} // namespace tbhost
