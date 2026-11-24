// The both-core helpers of the battleground PvP kit, found on either core without an edit to the shared compat headers:
// ForeverCore has forever/src/compat/TbCompat_BG.h on its include path, Turtle has ai/cmangos-compat-bg.h (both `#pragma once`).
#pragma once
#if __has_include("TbCompat_BG.h")
#include "TbCompat_BG.h"
#else
#include "cmangos-compat-bg.h"
#endif
