// The both-core helpers of the world-life slice W1, found on either core without an edit to the shared compat headers:
// ForeverCore has forever/src/compat/TbCompat_W1.h on its include path, Turtle has ai/cmangos-compat-w1.h. A later merge may add
// the two includes to the shims; this wrapper stays correct either way (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_W1.h")
#include "TbCompat_W1.h"
#else
#include "cmangos-compat-w1.h"
#endif
