// The both-core helpers of the raid-consumable feature (issue #22), found on either core without editing the shared shim
// headers: ForeverCore has forever/src/compat/TbCompat_RaidCons.h on its include path, Turtle has
// ai/cmangos-compat-raidcons.h (every answer inert: the Turtle core's own kits are untouched). Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_RaidCons.h")
#include "TbCompat_RaidCons.h"
#else
#include "cmangos-compat-raidcons.h"
#endif
