// Slice 5, host package H5: the few functions of the run header (TbRun.h) that other files call. Light on purpose: this header includes neither the run
// leader's policy (runtime/DungeonRunPolicy.h, 23,000 lines) nor a legacy header, so TbScripts.cpp, TbPartyState.cpp and TbH3.cpp can include it.
// All of it is defined in TbRun.cpp and exists with TORTOISEBOTS_SLICE1 only (the callers are slice-only code).
#pragma once

#include <vector>

class Player;

namespace ai { struct DungeonPlan; }

namespace TortoiseBots
{
namespace h3 { class Rec; class Reply; }
namespace run
{
// OnWorldInitialized: loads the dungeon plan tables and the step unit rows (`ai::LoadDungeonPlans`, `ai::LoadDungeonStepUnits`), once the module is
// enabled and the engine built. The plan loader is the P5 package's; until it merges a stand-in does nothing and says so (TbPlanLoaderStandIn.cpp).
void LoadPlans();

// Every run's plan pointer (`.tb plan reload` passes them as `inUse`, so a replaced table a run still reads is kept alive).
std::vector<ai::DungeonPlan const*> PlansInUse();

// `.tb state`: the end line of a party in a run gains `run=<lead name> runstate=<state>` (addendum A8 allows new keys). Absent when the party has no run.
void AnnotateStateEnd(h3::Rec& end, Player* member);

// `.tb grammar selftest`: one `plan`, `leg`, `unit`, `run` and `event` record with fixed example values, built by the real record writers.
void SelfTestRecords(h3::Reply& reply);
}
}
