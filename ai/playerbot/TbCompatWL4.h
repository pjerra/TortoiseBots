// The both-core helpers of the world-life slice W4, round A2, found on either core without an edit to the shared compat
// headers: ForeverCore has forever/src/compat/TbCompat_WL4.h on its include path, Turtle has ai/cmangos-compat-wl4.h. A
// later merge may add the two includes to the shims; this wrapper stays correct either way (both headers are `#pragma once`).
//
// Include it AFTER TravelMgr.h/TravelValues.h (the headers name the bot's own types). Every helper is a call the bot
// always made, with the same signature on both cores: the Turtle side is the legacy expression, the ForeverCore side the
// equivalent on this core (an unknown answer makes the bot do LESS, never more).
#pragma once
#if __has_include("TbCompat_WL4.h")
#include "TbCompat_WL4.h"
#else
#include "cmangos-compat-wl4.h"
#endif
