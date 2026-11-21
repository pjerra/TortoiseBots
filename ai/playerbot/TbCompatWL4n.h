// The both-core helpers of the travel slice W4, round A1 (the node graph), found on either core without an edit to the shared
// compat headers: ForeverCore has forever/src/compat/TbCompat_WL4n.h on its include path, Turtle has ai/cmangos-compat-wl4n.h.
// Both headers are `#pragma once`; this wrapper stays correct either way.
#pragma once
#if __has_include("TbCompat_WL4n.h")
#include "TbCompat_WL4n.h"
#else
#include "cmangos-compat-wl4n.h"
#endif
