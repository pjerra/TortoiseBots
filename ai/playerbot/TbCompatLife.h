// The both-core helpers of the free-bot rpg layer (TBF #147), found on either core without an edit to the shared compat
// headers (the shims): ForeverCore has forever/src/compat/TbCompat_Life.h on its include path, Turtle has
// ai/cmangos-compat-life.h. When a later merge adds the two lines to the shims, this wrapper is still correct (both
// headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_Life.h")
#include "TbCompat_Life.h"
#else
#include "cmangos-compat-life.h"
#endif
