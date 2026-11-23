// The both-core helpers the ride port needs, found on either core without an edit to the shared compat
// headers: ForeverCore has forever/src/compat/TbCompat_RIDE.h on its include path, Turtle has
// ai/cmangos-compat-ride.h. Both headers are `#pragma once`.
#pragma once
#if __has_include("TbCompat_RIDE.h")
#include "TbCompat_RIDE.h"
#else
#include "cmangos-compat-ride.h"
#endif
