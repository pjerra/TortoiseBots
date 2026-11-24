// The both-core helpers of the spell-school switch (issue #48), found on either core without editing the shared shim
// headers: ForeverCore has forever/src/compat/TbCompat_School.h on its include path, Turtle has
// ai/cmangos-compat-school.h. Both are `#pragma once`.
#pragma once
#if __has_include("TbCompat_School.h")
#include "TbCompat_School.h"
#else
#include "cmangos-compat-school.h"
#endif
