// The both-core helpers of slice 11, package K11h (hunter), found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_K11h.h on its include path, Turtle has ai/cmangos-compat-k11h.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_K11h.h")
#include "TbCompat_K11h.h"
#else
#include "cmangos-compat-k11h.h"
#endif
