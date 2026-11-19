// The both-core helpers of wide pass 2, found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_W2.h on its include path, Turtle has ai/cmangos-compat-w2.h. When a later merge adds
// the two lines to the shims, this wrapper is still correct (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_W2.h")
#include "TbCompat_W2.h"
#else
#include "cmangos-compat-w2.h"
#endif
