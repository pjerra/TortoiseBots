// The both-core helpers of the open-world PvP seek port, found on either core without an edit to the shared compat
// headers: ForeverCore has forever/src/compat/TbCompat_PvP.h on its include path, Turtle has ai/cmangos-compat-pvp.h.
#pragma once
#if __has_include("TbCompat_PvP.h")
#include "TbCompat_PvP.h"
#else
#include "cmangos-compat-pvp.h"
#endif
