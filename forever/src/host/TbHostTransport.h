#pragma once

#include "ObjectGuid.h"

#include <cstdint>
#include <string>

class Player;

namespace tbhost
{
namespace ride
{

// Starts a dock-to-dock transport ride for the given transport GameObject entry.
// Refuses non-classic custom routes unless explicitly allowed.
bool StartRide(Player* bot, uint32_t transportEntry, std::string* stateOut = nullptr, uint32_t orderSeq = 0);

// Polls and cancels the multi-tick ride errand outcome for `.tb order`.
bool PollOrderErrand(Player const* bot, uint32_t orderSeq, std::string& outResult);
void CancelOrderErrand(Player const* bot);

// Per-tick ride state machine: waits at dock, boards when stopped in range,
// idles while FindMap() is null (far teleport), and leaves at destination dock.
void TickRide(Player* bot, uint32_t diffMs);

// #215: true while a ride errand waits at the dock, walks onto the deck or rides: the ride alone moves the bot then (the
// engine's tick and the host's catch-up teleport stay off it).
bool HoldsMovement(Player const* bot);

// Returns true if the bot is currently aboard a transport.
bool IsOnBoard(Player const* bot);

// Returns the GUID of the transport the bot is currently aboard (or Empty).
ObjectGuid CurrentTransportGuid(Player const* bot);

} // namespace ride
} // namespace tbhost
