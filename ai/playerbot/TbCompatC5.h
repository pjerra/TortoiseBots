// The both-core helpers of the combat monitor (package P5 of the commander port), found on either core without an edit to the shared compat
// headers (the shims): ForeverCore has forever/src/compat/TbCompat_C5.h on its include path, Turtle has ai/cmangos-compat-c5.h. When a later
// merge adds the two lines to the shims, this wrapper is still correct (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_C5.h")
#include "TbCompat_C5.h"
#else
#include "cmangos-compat-c5.h"
#endif
