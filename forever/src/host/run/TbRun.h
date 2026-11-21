// Slice 5, host package H5: the run header (forever/docs/RUN-S5-H-NOTES.md; spec FOREVER-RUN-S5S6-SPEC.md section 4.5).
//
// One RunRec per run, keyed by the leader's guid: who leads, which plan, how far it is (the Turtle `DungeonRun::Run`, runtime/DungeonRunPolicy.h, is the
// progress), the counters and a ring of the last 1024 events. In S5 NOTHING creates or ticks a run but the selftest: the S6 glue (`.tb run start`, the 1 Hz
// tick) does, and writes the same records this file builds (`run`, `event`). Compiled with TORTOISEBOTS_SLICE1 only.
//
// Threading: world thread only (a command, or the engine tick in OnBeforeMapsUpdate): plain data, no lock.
#pragma once

#include "host/TbH3.h"
#include "host/run/TbRunGatherPure.h"
#include "host/run/TbRunHooks.h"
#include "runtime/DungeonRunPolicy.h"

#include "ObjectGuid.h"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
constexpr size_t kEventRing = 1024;   // a 63-minute run overflowed 200 events; `events since=` names the oldest seq kept

// One entry of a run's event ring (`.tb run events`): kind = start | step | pull | cc | cc-clear | gather-wait | fight-end | rest | death | wipe | revive | pause | resume | done | stop, and since
// slice 9 (package C9) open | object | loot | item | clear (a task or a wait step of the plan ended: `unit` = the object's or boss's entry, `why` = what it saw).
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

// R6 (review item 6): a `.tb run stop` that came in a fight waits for the fight to end, but at most this long: a stuck combat flag (a mob that cannot reach the
// party, an evading mob) must not keep a run the owner stopped alive for good. After it the stop takes effect and the run record says so (`why=`).
constexpr uint32 kStopPendingMaxMs = 90000;

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

    // ---- S6 (package C6): what the tick keeps between its 1 Hz runs. Plain data and guids only: no Player, Unit or Map pointer is kept across ticks. ----
    // A member's host master and whether it was commanded (`.tb master`) before the run bound it to the tank (decision D1); `.tb run stop` and the end of the
    // run put it back. Only members of the leader's party are in it (the leader itself is never rebound).
    struct Binding
    {
        ObjectGuid master;              // BotMgr's host master before the run (empty: none)
        bool commanded = false;         // h3::CommanderGuid was set (then it named `master`)
    };
    std::map<ObjectGuid, Binding> bound;
    std::map<ObjectGuid, uint32> orderSeen;     // member (and the leader) -> the `.tb order` seq the run last saw: a newer seq is an order from outside
    uint32 lastTickMs = 0;                       // GameTime::GetGameTimeMS() of the last tick (the dt the policy is given)
    uint32 ticks = 0;
    bool stopPending = false;                    // `.tb run stop` came in a fight: no new order, the stop takes effect when the fight is over
    uint32 stopPendingSinceMs = 0;               // R6 (review item 6): the clock of that request; kStopPendingMaxMs later the stop takes effect whatever the fight does
    uint32 instanceId = 0;                       // R6 (review item 4): the leader's instance of the plan's map at the start; the lead in another instance ends the run
    std::string stopWhy;
    std::string pauseCode;                       // the machine word of the next pause event's `why=` (outside-order, player-in-group, lead-off-map, command, bags-full, loot-method); empty: the run's own note
    bool pausedForPlayer = false;                // the system run's join flag (SystemRun::JoinFlagAfterScan)
    // What the last tick saw, to name the events (start .. stop) of the next one.
    bool seenInit = false;
    DungeonRun::State seenState = DungeonRun::State::Running;
    DungeonRun::Phase seenPhase = DungeonRun::Phase::Begin;
    size_t seenStep = 0;
    bool seenCombat = false;
    bool seenWipe = false;
    std::set<std::string> seenDead;
    gather::Extension gatherExt;                 // G8: the regroup gather's bounded extensions at the current step (TbRunGatherPure.h)
    // G8 (review): the bots the run's own `cc` orders marked, name -> mark. Run::ccHolds does not survive a step advance, a resume or a regroup
    // (DungeonRun::ResetStep clears it) and the bots' `rti cc` / `run cc family` do: this is what ClearRunCc resets at the run's end, moving an entry it
    // could not settle to the pending retry (DropPendingCc/RetryPendingCc).
    std::map<std::string, std::string> ccOwned;
    size_t pullChecked = size_t(-1);             // the pull step whose unit rows were counted (StepMatchedMembers), and the pass it was counted on
    uint32 pullCheckedPass = 0;
    size_t wadeLoggedStep = size_t(-1);          // the step whose wet wade hop was logged (one line per wet hop, not per order)

    // ---- S9 (package C9): what the run keys and the step-end events read. Plain data from the last tick's facts: the facts the policy decided on. ----
    std::string carrier;                         // the bot a loot / open / use_item step goes to (empty on any other step)
    std::string itemKey;                         // the step's item and whether the carrier holds it ("5397:held" | "5397:missing"; empty: the step has no item)
    std::string objKey;                          // the object the step waits on or opens: "<entry>:<ready|active|active_alt|none>" (empty on any other step)
    size_t preStep = size_t(-1);                 // the step the next fields were read for (the tick that ends a step read them before it decided)
    int preObjState = -1;                        // wait_object: Facts::objectState (-1 not found)
    uint32 preClearNear = 0;                     // wait_clear: hostiles within the radius
    bool preHasItem = false;                     // loot: the carrier, or a party bot, holds the step's item
    std::string preHolder;                       // ... the first bot that does
};

// The run table. `Create` fails (null) when the leader already has a run; the Turtle run is started on the plan at step 0.
RunRec* Find(ObjectGuid leader);
RunRec* Create(ObjectGuid leader, std::string const& leaderName, ai::DungeonPlan const* plan, uint32 nowMs);
bool Remove(ObjectGuid leader);
std::vector<RunRec*> All();                         // by leader name

// One listener for a run that ends (Finish is its only writer of endedMs): the dashboard feed copies what it needs while the record is
// whole, because a later `.tb run start` removes it before the next world tick. Null when nothing is installed; a refused start calls it
// never. The listener must not keep the record or a Player pointer.
using EndListener = void (*)(RunRec const&);
void SetEndListener(EndListener listener);

// Appends an event (the ring drops the oldest past kEventRing) and returns its seq. `tMs` is taken from `nowMs` and the run's start.
uint32 AddEvent(RunRec& rec, uint32 nowMs, std::string kind, int32 step = -1, int32 leg = -1, int32 unit = -1, std::string why = {});

// S9 (C9): a task or wait step of the run ended; an event of `kind` (open | object | loot | item | clear) with the entry it was about and what was seen. It
// is written into the ring and the server log like every run event, on the run's current step.
void NoteTaskEvent(RunRec& rec, uint32 nowMs, char const* kind, int32 unit, std::string const& why);

// running | paused | done (the Turtle run's state), or stopped when `stopped` is set.
std::string StateName(RunRec const& rec);

// ---- S6 (package C6): the tick, the binding and the commands' logic (the `.tb run` handlers in TbRunCommands.cpp only parse and write replies) ----
// `Update` is called once per world tick from OnBeforeMapsUpdate (TbRunHooks.h); it retries the cc teardown an ended run left pending and runs every live run
// once per second. Cost of one run tick: one `ReadFacts` (F6: at most a few path queries and grid scans, 2-3 us in the pure half), one `DungeonRun::Tick`,
// at most ONE order through the executor, and a handful of map lookups; `TickTotals` says what it measured.
struct TickTotals
{
    uint64 ticks = 0;           // run ticks since boot
    uint64 totalUs = 0;         // wall clock of all of them
    uint32 maxUs = 0;           // the worst one
};
TickTotals const& TickCost();

bool Ended(RunRec const& rec);                          // the run is done or stopped (it stays in the table for `status` and `events` until a new start)

// G8 (review): a run's cc mark that its teardown could not take back (the bot out of the world, no engine, a read-back that did not take) is retried every
// world tick until it settles. A newer accepted `cc` / `cc clear` for the bot drops its pending entry first (that order takes the bot over).
void DropPendingCc(std::string const& botName);

struct StartOutcome
{
    std::string code;           // empty = started; else not-bot not-leader group-raid player-in-group not-tank no-engine wrong-map already-running not-commanded bind-failed no-plan bags loot-method
    std::string detail;
    RunRec* rec = nullptr;
};
// Checks the party and starts the run (binds every other member to the leader). Nothing changes when a code is returned.
StartOutcome StartRun(Player* leader, ai::DungeonPlan const* plan, uint32 nowMs);

struct StopOutcome
{
    std::string code;           // empty = accepted; else not-running
    std::string detail;
    bool pending = false;       // a fight is going on: the stop takes effect when it ends
    uint32 stayed = 0;          // members the executor accepted a `stay` for (0 while the run is pending)
};
StopOutcome StopRun(RunRec& rec, uint32 nowMs, std::string const& reason);

struct StateOutcome
{
    std::string code;           // empty = done; else not-running | not-paused | stopping | player-in-group | refused
    std::string detail;
};
StateOutcome PauseRun(RunRec& rec, uint32 nowMs);
StateOutcome ResumeRun(RunRec& rec, uint32 nowMs);

// The `run` record of `.tb run status` (S6) and of the selftest, and the `event` record of `.tb run events`.
h3::Rec RunRecord(RunRec const& rec, uint32 nowMs);
h3::Rec EventRecord(RunEvent const& event);
}
}
