// Slice 5, host package H5: the few functions of the run header (TbRun.h) that other files call. Light on purpose: this header includes neither the run
// leader's policy (runtime/DungeonRunPolicy.h, 23,000 lines) nor a legacy header, so TbScripts.cpp, TbPartyState.cpp and TbH3.cpp can include it.
// All of it is defined in TbRun.cpp and exists with TORTOISEBOTS_SLICE1 only (the callers are slice-only code).
#pragma once

#include <cstdint>
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

// S6 (package C6): the run tick. Called once per world tick from OnBeforeMapsUpdate, after BotMgr::Update (the world thread, every map idle); it runs each
// live run once per second.
void Update(uint32_t diffMs);

// S6 host hooks for the merge (TbHostOwnership.cpp answers four legacy questions with "no run exists" stand-ins; with a run they should answer from here).
// `bot` is any member of a run's party; the answers are those of the run of the party's leader, and "no run" (false / 0 / null) when there is none or it is
// not running.
bool BotInRunningRun(Player* bot);                              // BotCommands::IsInRunningDungeonRun
bool BotRunKillFirst(Player* bot, uint32_t entry);              // BotCommands::IsRunKillFirst (the plan's kill_first list)
std::vector<uint32_t> const* BotRunArmedTriggers(Player* bot);  // BotCommands::RunArmedAreaTriggers (null: none armed)
uint32_t BotRunPlanMap(Player* bot);                            // BotCommands::BoundRunPlanMap (0: no bound run)

// `.tb grammar selftest`: one `plan`, `leg`, `unit`, `run` and `event` record with fixed example values, built by the real record writers.
void SelfTestRecords(h3::Reply& reply);
}
}
