// The both-core helpers of the world slice W0 (the pool), found on either core without an edit to the shared compat headers:
// ForeverCore has forever/src/compat/TbCompat_WL0.h on its include path, Turtle has ai/cmangos-compat-wl0.h. A later merge may
// add the two includes to the shims; this wrapper stays correct either way (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_WL0.h")
#include "TbCompat_WL0.h"
#else
#include "cmangos-compat-wl0.h"
#endif
