// The both-core helpers of round S1-R2, found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_CH.h on its include path, Turtle has ai/cmangos-compat-ch.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_CH.h")
#include "TbCompat_CH.h"
#else
#include "cmangos-compat-ch.h"
#endif
