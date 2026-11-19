// Slice 5, host package H5: the per-member unit rows of the dungeon plans (`ai_dungeon_step_unit`, ForeverCore only) and the check that finds each of them
// in a fresh instance (forever/docs/RUN-S5-H-NOTES.md; spec FOREVER-RUN-S5S6-SPEC.md sections 4.1, 4.2 and 4.4).
//
// A step row of the plan names a pack by its creature entries, the centroid of its members and a radius. The unit rows name every member with its own
// position, so a member can be found by entry + position on ForeverCore (whose spawn ids are not the Turtle ones) and a centroid that loses members
// (5 RFC pull rows) does not matter. The Turtle loader never reads the table.
//
// Compiled with TORTOISEBOTS_SLICE1 only (the loader and the plan table are the engine build's). World thread only, like every host function.
#pragma once

#include "Define.h"
#include "host/TbH3.h"
#include "host/run/TbPlanMatch.h"

#include <memory>
#include <string>
#include <vector>

class Map;

namespace ai { struct DungeonPlan; struct PlanReloadHost; }

namespace ai
{
// Reads `ai_dungeon_step_unit` once at boot, through the schema guard (`WorldQueryCompat`): a missing table or column is logged and means "units not known"
// (`LastStepUnitLoad().read` false, `.tableMissing` true), never an abort. A row whose plan or step is not in the loaded plan table is refused (counted, the
// first ten logged). Does nothing (and says so) while the plan table is not loaded. Builds the spawn index (below). Two boot lines: `>> Loaded <n> dungeon step
// units` and the index line. The reload does NOT call this: `.tb plan reload` goes through FillUnitReloadHost (all or nothing with the plan tables).
void LoadDungeonStepUnits();
}

namespace TortoiseBots
{
namespace run
{
// StepUnit, StepUnitLoad, UnitResult, UnitCheck, PlanCheck: TbPlanMatch.h (the pure part).
StepUnitLoad const& LastStepUnitLoad();

// The unit rows of a plan, in (leg, seq, idx) order; empty when the plan has none or the rows were not read.
std::vector<StepUnit> const& UnitsOfPlan(std::string const& plan);

// ---- `.tb plan <plan> check`: the match of every unit row against the spawns of a map (the rules: MatchPlanUnits in TbPlanMatch.h) ----
// The match of the plan's unit rows against the creature spawns of `map` (its id and difficulty), MatchPlanUnits' rules. Cost: proportional to the plan's rows and to
// the spawns of THEIR entries (an index (map, entry) -> spawns, built once from the core's creature data at boot and again by every successful `.tb plan reload`;
// it holds only the entries some unit row names), not to the creature spawns of the world.
// The result is a shared_ptr to a const check: the cache (newest 16 per (map id, instance id, plan)) and a reload drop only their own reference, so a holder (S6
// reads one across ticks) keeps its copy. `refresh` recomputes it, but at most once per kPlanCheckMinRefreshMs per key: a forced refresh inside that time is
// answered from the cache (the spawn data is static after boot, so the answer is the same; the bound keeps a command loop from re-matching every tick).
// ASSUMPTION: the creature spawn data does not change while the server runs (no `.npc add`/delete of a spawn is followed); a reload refreshes the index.
constexpr long long kPlanCheckMinRefreshMs = 1000;
std::shared_ptr<PlanCheck const> CheckPlanOnMap(std::string const& plan, Map const* map, bool refresh);
void ClearPlanChecks();

// For `.tb plan reload`: fills `host` (ai::ReloadDungeonPlans) so the unit rows are read, checked against the NEW plan table and indexed into temporaries, and
// installed (with the cache cleared) only when the plan tables, the tactics and the units all pass: otherwise nothing changes. host.table stays empty when
// there is nothing to do (the unit table was never read and still does not exist).
void FillUnitReloadHost(ai::PlanReloadHost& host);

// The records of `.tb plan` (one writer each, so the command and `.tb grammar selftest` cannot disagree).
// `plan plan= map= legs= steps= moves= pulls= bosses= [units=] cc_priority= kill_first= [source=]`: `units` (the unit rows the plan has) and `source` are omitted when -1 / null
// (not known: the unit rows were not read / the plan row could not be read again).
h3::Rec PlanRecord(ai::DungeonPlan const& plan, int64 units, std::string const* source);
// The numbers of the legs a plan has (a leg row or a step names it), ascending, and one `leg leg= name= steps=` record for one of them.
std::vector<uint8> LegsOf(ai::DungeonPlan const& plan);
h3::Rec LegRecord(ai::DungeonPlan const& plan, uint8 leg);
// `unit leg= seq= idx= entry= role= result= [yd= spawn= alive=] [optional=yes]`; `alive` -1 = not asked (matched / near only carry yd, spawn and alive).
h3::Rec UnitRecord(UnitCheck const& unit, int alive);

// Whether the creature of a spawn on `map` lives now: its loaded creature is alive, or, when it is not loaded, the map holds no future respawn time for it.
bool SpawnAliveOnMap(Map const* map, uint32 spawnId);
}
}
