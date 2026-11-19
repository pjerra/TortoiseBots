// Slice 5, host package H5: the run header (forever/docs/RUN-S5-H-NOTES.md; spec FOREVER-RUN-S5S6-SPEC.md section 4.5).
//
// One RunRec per run, keyed by the leader's guid: who leads, which plan, how far it is (the Turtle `DungeonRun::Run`, runtime/DungeonRunPolicy.h, is the
// progress), the counters and a ring of the last 200 events. In S5 NOTHING creates or ticks a run but the selftest: the S6 glue (`.tb run start`, the 1 Hz
// tick) does, and writes the same records this file builds (`run`, `event`). Compiled with TORTOISEBOTS_SLICE1 only.
//
// Threading: world thread only (a command, or the engine tick in OnBeforeMapsUpdate): plain data, no lock.
#pragma once

#include "host/TbH3.h"
#include "host/run/TbRunHooks.h"
#include "runtime/DungeonRunPolicy.h"

#include "ObjectGuid.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
constexpr size_t kEventRing = 200;

// One entry of a run's event ring (`.tb run events`): kind = start | step | pull | fight-end | rest | death | wipe | revive | pause | resume | done | stop.
// `step`, `leg` and `unit` are -1 when the event has none (the record then omits the key: an unknown number is never written as a number).
struct RunEvent
{
    uint32 seq = 0;             // 1, 2, 3 ... per run
    uint32 tMs = 0;             // milliseconds since the run started
    std::string kind;
    int32 step = -1;
    int32 leg = -1;
    int32 unit = -1;            // a creature entry
    std::string why;            // empty when there is none
};

struct RunRec
{
    ObjectGuid leader;
    std::string leaderName;
    std::string planName;
    DungeonRun::Run run;        // the Turtle run: state Running|Paused|Done, phase Begin..Hold, the step index, the note
    uint32 startedMs = 0;       // GameTime::GetGameTimeMS() at the start
    uint32 endedMs = 0;         // the same clock when the run stopped or finished; 0 while it goes on (elapsed= stops there)
    uint32 pauses = 0, deaths = 0, wipes = 0;
    uint32 eventSeq = 0;        // the seq of the newest event (0: none yet)
    bool stopped = false;       // `.tb run stop` took effect (the run's own state stays what it was)
    std::string stopReason;
    std::deque<RunEvent> events;                    // capped at kEventRing, oldest dropped
    // The state of each plan boss the run KNOWS: entry -> dead. Only the S6 glue sets an entry (from the boss creature it read), so an entry that was
    // never read is not in the map and the `boss=` list does not name it: a boss is never reported alive by default.
    std::map<uint32, bool> bossDead;
};

// The run table. `Create` fails (null) when the leader already has a run; the Turtle run is started on the plan at step 0.
RunRec* Find(ObjectGuid leader);
RunRec* Create(ObjectGuid leader, std::string const& leaderName, ai::DungeonPlan const* plan, uint32 nowMs);
bool Remove(ObjectGuid leader);
std::vector<RunRec*> All();                         // by leader name

// Appends an event (the ring drops the oldest past kEventRing) and returns its seq. `tMs` is taken from `nowMs` and the run's start.
uint32 AddEvent(RunRec& rec, uint32 nowMs, std::string kind, int32 step = -1, int32 leg = -1, int32 unit = -1, std::string why = {});

// running | paused | done (the Turtle run's state), or stopped when `stopped` is set.
std::string StateName(RunRec const& rec);

// The `run` record of `.tb run status` (S6) and of the selftest, and the `event` record of `.tb run events`.
h3::Rec RunRecord(RunRec const& rec, uint32 nowMs);
h3::Rec EventRecord(RunEvent const& event);
}
}
