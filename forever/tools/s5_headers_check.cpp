// Slice 5 (package P5): the run leader's decision headers compile on both cores. One translation unit that includes them, so the
// census (FOREVER_CENSUS_LIST=forever/tools/s5_headers.list) and a build see them. They are pure (no core include): this TU adds
// nothing but the includes. The host package H5 owns the module's own copy of this check (forever/src/host/run/TbRunHeaders.cpp);
// this file is the stand-alone form P5 uses until that one merges. Not part of any build list.
#include "../../runtime/DungeonRunPolicy.h"
#include "../../runtime/RemoteCommanderPolicy.h"
#include "../../runtime/SystemRunPolicy.h"
#include "../../ai/playerbot/DungeonPlanPolicy.h"
#include "../../ai/playerbot/DungeonPlanReloadPolicy.h"
#include "../../ai/playerbot/BossTacticsPolicy.h"

int TbS5HeadersCheck()
{
    TortoiseBots::DungeonRun::Facts facts;
    TortoiseBots::DungeonRun::Run run;
    ai::DungeonPlan plan;
    ai::DungeonPlanTable table;
    return int(TortoiseBots::DungeonRun::Tick(run, facts).size() + plan.steps.size() + table.All().size());
}
