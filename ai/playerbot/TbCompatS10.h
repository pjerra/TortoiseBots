// The both-core helpers of slice 10 (druid, paladin, shaman), found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_S10.h on its include path, Turtle has ai/cmangos-compat-s10.h. When a later merge adds the two
// lines to the shims, this wrapper is still correct (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_S10.h")
#include "TbCompat_S10.h"
#else
#include "cmangos-compat-s10.h"
#endif
