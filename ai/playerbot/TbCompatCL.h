// The both-core chat link helpers of round S1-R1, found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_CL.h on its include path, Turtle has ai/cmangos-compat-cl.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_CL.h")
#include "TbCompat_CL.h"
#else
#include "cmangos-compat-cl.h"
#endif
