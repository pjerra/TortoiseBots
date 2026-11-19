// The both-core helpers of slice 8, package K8, found on either core without an edit to the shared compat headers (the shims):
// ForeverCore has forever/src/compat/TbCompat_K8.h on its include path, Turtle has ai/cmangos-compat-k8.h. When the merge adds the two
// lines to the shims, this wrapper is still correct (both headers are `#pragma once`).
#pragma once
#if __has_include("TbCompat_K8.h")
#include "TbCompat_K8.h"
#else
#include "cmangos-compat-k8.h"
#endif
