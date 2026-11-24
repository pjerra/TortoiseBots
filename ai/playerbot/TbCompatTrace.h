// The both-core helpers of the run trace and the why trace (runtime/RunTrace.cpp, runtime/WhyTrace.cpp), found
// on either core without an edit to the shared compat headers (the shims): ForeverCore has
// forever/src/compat/TbCompat_TRACE.h on its include path, Turtle has ai/cmangos-compat-trace.h. When a later
// merge adds the two lines to the shims, this wrapper is still correct (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_TRACE.h")
#include "TbCompat_TRACE.h"
#else
#include "cmangos-compat-trace.h"
#endif
