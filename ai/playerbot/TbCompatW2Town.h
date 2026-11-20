// The both-core helpers of W2 (vendor, repair, trainer, mail read), found on either core without editing
// the shared shim headers: ForeverCore has forever/src/compat/TbCompat_W2Town.h on its include path,
// Turtle has ai/cmangos-compat-w2town.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_W2Town.h")
#include "TbCompat_W2Town.h"
#else
#include "cmangos-compat-w2town.h"
#endif
