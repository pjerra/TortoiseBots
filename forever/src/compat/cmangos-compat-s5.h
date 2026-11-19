// Interim forwarder (package P5 of slice 5). The loader files (ai/playerbot/DungeonPlanMgr.cpp, BossTacticsMgr.cpp) include
// "cmangos-compat-s5.h": on the Turtle core that is ai/cmangos-compat-s5.h, on ForeverCore (this folder comes first on the include
// path) it is this file, which hands over the ForeverCore half of the pair. Once the two shims include the pair (forever-compat-shim.h:
// TbCompat_S5.h, ai/cmangos-compat-shim.h: cmangos-compat-s5.h) this file and the two includes in the loader files can go
// (forever/docs/RUN-S5-P-NOTES.md, "Needs at merge").
#pragma once
#include "TbCompat_S5.h"
