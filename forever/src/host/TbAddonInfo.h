// The addon info readers (TBF #130 piece f-info): the TBM read lines the
// addon polls. CC_ASSIGN_BEGIN/CC_ASSIGN/CC_ASSIGN_END ride the roster reply
// (TB commands/Info.cpp:501-505); RUN/RUN_HERE/RAID_LAYOUT come from the run
// state (TB commands/Commander.cpp); FEATURE and CONFIG_BEGIN/CONFIG/CONFIG_END
// list the module's switches and settings (TB commands/Info.cpp:238-311).
// The wire shapes without the core are host/TbAddonInfoPure.h. Threading:
// world thread only (the queue drain calls the verbs; AppendCcAssign is
// called from f-roster's roster reply on the same thread).
#pragma once

#include <string_view>

class Player;

namespace TortoiseBots
{
namespace addoninfo
{
// Registers the "features" and "run" verbs and advertises
// "run:status"/"run:here"/"run:list" ("features" advertises itself). Called
// from TbScripts.cpp right after TortoiseBots::addon::AddScripts().
void RegisterAddonVerbsInfo();

// One CC assignment stream for the requester's party (always BEGIN/rows/END,
// empty when no bot holds a mark). Called from f-roster's roster reply, where
// TB emits it; this piece owns the lines, f-roster owns the roster.
void AppendCcAssign(Player* requester);
} // namespace addoninfo
} // namespace TortoiseBots
