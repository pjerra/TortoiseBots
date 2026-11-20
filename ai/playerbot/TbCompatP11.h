// The both-core pet helpers of slice 11, package P11, found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_P11.h on its include path, Turtle has ai/cmangos-compat-p11.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_P11.h")
#include "TbCompat_P11.h"
#else
#include "cmangos-compat-p11.h"
#endif
