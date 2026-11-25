// The both-core helpers of the resistance-gear swap (issue #20), found on either core without editing the shared shim
// headers: ForeverCore has forever/src/compat/TbCompat_Resist.h on its include path, Turtle has
// ai/cmangos-compat-resist.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_Resist.h")
#include "TbCompat_Resist.h"
#else
#include "cmangos-compat-resist.h"
#endif
