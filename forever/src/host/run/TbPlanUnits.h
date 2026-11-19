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

#include <string>
#include <vector>

class Map;

namespace ai { struct DungeonPlan; }

namespace ai
{
// Reads `ai_dungeon_step_unit` (once at boot, and again after a successful `.tb plan reload`): a row whose plan or step is not in the loaded plan table is
// refused (counted, the first ten logged). Does nothing (and says so) while the plan table is not loaded. One boot line: `>> Loaded <n> dungeon step units`.
void LoadDungeonStepUnits();
}

namespace TortoiseBots
{
namespace run
{
// One row of `ai_dungeon_step_unit`: the member number `idx` of the step (plan, leg, seq); role = pack | boss | first (boss.first) | patrol.
struct StepUnit
{
    uint8 leg = 0;
    uint16 seq = 0;
    uint8 idx = 0;
    uint32 entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string role;
};

// What the last LoadDungeonStepUnits saw.
struct StepUnitLoad
{
    bool read = false;          // false: the plan table was not loaded, nothing was read (every count below is then unknown, not zero)
    uint32 rows = 0;            // rows the query handed over
    uint32 kept = 0;
    uint32 refused = 0;         // unknown plan, step missing from the plan, a bad role, a zero entry or a non-finite position
};
StepUnitLoad const& LastStepUnitLoad();

// The unit rows of a plan, in (leg, seq, idx) order; empty when the plan has none or the rows were not read.
std::vector<StepUnit> const& UnitsOfPlan(std::string const& plan);

// ---- `.tb plan <plan> check`: the match of every unit row against the spawns of a map ----
enum class UnitResult : uint8 { Matched, Near, Missing };

constexpr float kMatchYd = 5.0f;        // a spawn within this of the row is `matched`
constexpr float kNearYd = 15.0f;        // ... within this `near`; farther, or none: `missing`

struct UnitCheck
{
    uint8 leg = 0;
    uint16 seq = 0;
    uint8 idx = 0;
    uint32 entry = 0;
    std::string role;
    UnitResult result = UnitResult::Missing;
    float yd = 0.0f;            // the distance to the spawn (matched / near only)
    uint32 spawnId = 0;         // the ForeverCore spawn id (matched / near only)
    bool optional = false;      // D4: the entry has no spawn at all on the plan's map, so a step does not need this member (a summoned pet, entry 8996)
};

struct PlanCheck
{
    std::string plan;
    uint32 mapId = 0;
    uint32 instanceId = 0;
    std::vector<UnitCheck> units;           // (leg, seq, idx) order
    uint32 matchedCount = 0, nearCount = 0, missingCount = 0, optionalCount = 0;   // `optionalCount` counts the missing units that are optional (they are also in `missingCount`)
    uint32 steps = 0, stepsOk = 0, stepsShort = 0;             // steps that have at least one unit row
};

// The match of the plan's unit rows against the creature spawns of `map` (its id and difficulty): one-to-one, greedy by distance (every pair within kNearYd,
// nearest first, a row and a spawn each used once; ties by (leg, seq, idx) then spawn id). A step is `ok` when every non-optional member is matched or near
// and at least one member was found. The result is CACHED per (map id, instance id, plan) and is what S6's pack lookup reads; `refresh` recomputes it.
// Spawn data is static after boot, so the cache never goes stale except through `ClearPlanChecks` (called after a plan reload).
PlanCheck const& CheckPlanOnMap(std::string const& plan, Map const* map, bool refresh);
void ClearPlanChecks();

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
