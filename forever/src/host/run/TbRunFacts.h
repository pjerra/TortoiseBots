// Slice 6, package F6 (facts): the run leader's view of the world on ForeverCore (forever/docs/RUN-S6-F-NOTES.md; spec FOREVER-RUN-S5S6-SPEC.md 5.1, 5.3).
//
// `ReadFacts` fills the Turtle run leader's `DungeonRun::Facts` (runtime/DungeonRunPolicy.h:1561) for ONE run, on the world thread, from the core's
// public API. It is the ForeverCore counterpart of `ReadRunFacts` (commands/Commander.cpp:11690) for the fields a Ragefire Chasm run reads
// (forever/docs/RUN-S5-FACTS-RFC.md): the others keep the `Facts()` default or the explicit "do less" value of RUN-S6-F-FIELDS.tsv.
//
// Light on purpose (no policy header, no core header beyond Define/ObjectGuid): C6's tick includes it. Compiled with TORTOISEBOTS_SLICE1 only.
//
// Threading: world thread only (the run tick in OnBeforeMapsUpdate, World.cpp:2470, while every map is idle). No Player, Unit or Map pointer is kept
// between calls: a run's state here is guid handles, a path-probe cache and counters.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <string>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace DungeonRun
{
struct Facts;
}
namespace run
{
struct RunRec;

// What one read cost and what it could not read. Filled when the caller passes one.
struct FactsReport
{
    bool leadReadable = false;      // the lead is a player in the world: false = nothing was read (Facts is the default plus leadAlive=false)
    uint32 costUs = 0;              // wall clock of the whole read, microseconds
    uint32 members = 0;             // group slots read
    uint32 membersAbsent = 0;       // slots with no player in the world (counted dead and far)
    uint32 pathQueries = 0;         // PathGenerator::CalculatePath calls this read made
    uint32 losQueries = 0;          // line-of-sight tests
    uint32 gridScans = 0;           // creature / unit grid searches
    uint32 heightQueries = 0;       // map height reads
    uint32 creaturesSeen = 0;       // creatures looked at by the step reads
    uint32 listsCut = 0;            // a list that hit its cap (facts::kMaxListed ...)
    uint32 handlesIssued = 0;       // handles in this run's table after the read
    std::string stepKind;           // the kind of the step the read was for ("" when there is none)
    std::vector<std::string> notKnown;   // facts this read had to answer with the "do less" value because the core could not tell (see the field table)
};

// Reads the facts of `rec`'s run into `out` (reset first). `nowMs` is the run tick's clock (GameTime::GetGameTimeMS). Returns false when the leader is
// not a player in the world: `out` is then `Facts()` with leadAlive=false and the caller pauses the run (spec: "Lead off map").
bool ReadFacts(RunRec& rec, uint32 nowMs, DungeonRun::Facts& out, FactsReport* report = nullptr);

// Guid handles (P keys units by uint64, an ObjectGuid has 128 bits). `HandleFor` issues or finds the handle of a guid; `ResolveHandle` turns a handle
// the facts carried back into the guid (false: unknown or dropped). Both are per run (the leader's guid).
uint64 HandleFor(ObjectGuid leader, ObjectGuid guid, uint32 nowMs);
bool ResolveHandle(ObjectGuid leader, uint64 handle, ObjectGuid& out);

// Drops a run's reader state (handle table, probe cache). C6 calls it when the run is removed.
void ForgetRun(ObjectGuid leader);

// How many members of the plan step the S5 unit rows matched on the run's map (-1: not known: no unit rows, or the map is not the plan's). A pull step
// with 0 is "not cleared" (spec 5.3 "Pack lookup"): the caller (C6) skips it, the facts never guess a pack.
int StepMatchedMembers(RunRec const& rec, size_t stepIndex);

// Totals since boot, for `.tb run status` / perf notes.
struct FactsTotals
{
    uint64 reads = 0;
    uint64 totalUs = 0;
    uint32 maxUs = 0;
    uint64 pathQueries = 0;
    uint64 gridScans = 0;
    uint64 unreadable = 0;          // reads where the lead was not in the world
};
FactsTotals const& Totals();

// A member's run-tank read. `PlayerbotAI::IsTank` answers from the forced role bit first, so a bit left on a class whose kit cannot tank
// (a restoration shaman: slice/SliceHybridPolicy.h) must not make it a tank: every run and party read of a member's tank-ness uses this.
bool IsTankMember(Player* bot);
}
}
