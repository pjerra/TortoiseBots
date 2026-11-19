// The Turtle core's path for the config header ("Config/Config.h"); ForeverCore has no such file. The core's own
// Config.h is already in front of every legacy file (botpch_forever.h); what the legacy code means by `Config` here is
// the class in forever/src/compat/TbCompat_Config.h, which forever-compat-shim.h includes.
// (Not generated: this one carries a class, not a forwarding include.)
#pragma once
#include "TbCompat_Config.h"
