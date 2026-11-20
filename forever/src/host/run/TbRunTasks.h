// Slice 9, package O9 (object, loot and use tasks): the run leader's `open`, `loot` and `lootmethod` orders on ForeverCore
// (forever/docs/SLICE9-DOORS-NOTES.md; spec FOREVER-SLICE9-SPEC.md sections 1, 7, 8).
//
// It is the ForeverCore counterpart of `CommanderOpen` / `CommanderLoot` / `CommanderLootMethod` and the task machinery behind them (commands/Commander.cpp:
// 4182-5883, about 1,700 lines), REWRITTEN for the three verbs the two dungeons need, not ported: target by handle or `entry:id@x,y,z`, walk to a stand, act,
// result, one task per bot. The act of a door or a lever is the typed `GameObjUse` packet into `HandleGameObjectUseOpcode`; the act of a chest is the engine's
// own `open loot` (the opening spell, a key the bot carries); the act of a corpse is the engine's loot window (`LootOpenCompat`). Everything judged
// before the act (the lock row, the loot rights, the reach) is judged by the host first and refuses with a code. The decisions are in TbRunTasksPure.h
// (run by forever/tools/test_run_tasks.cpp); this file asks the world and carries them out.
//
// The task's result is not the order's answer: the order is ACCEPTED at once (the task is queued), and the result reaches the policy as the facts'
// taskReply / taskText (F9, TbRunFacts.cpp reads `StateOf`), exactly as the Turtle slot's `taskReply` does.
//
// Threading: world thread only (the run tick and `run::Update`, in OnBeforeMapsUpdate, while every map is idle). No Player, Unit or Map pointer is kept
// between calls: a task is a pair of guids and plain data. Compiled with TORTOISEBOTS_SLICE1 only.
#pragma once

#include "host/run/TbRunOrders.h"
#include "host/run/TbRunTasksPure.h"

#include "Define.h"
#include "ObjectGuid.h"

#include <string>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace run
{
struct RunRec;

namespace tasks
{
// `open` and `loot`: judged now (the bot, the target, the reach, the lock), accepted when a task could be queued. `bots` = the host bots of the party on the
// lead's map and instance, the lead first (the executor's party). A refusal changes nothing.
OrderAnswer Begin(RunRec& rec, Player* lead, std::vector<Player*> const& bots, TaskOrder const& order, uint32 nowMs);

// Commander P6: shared task entry points for `.tb cmd loot|open|use|gossip|lootmethod|state` on the same task engine.
// `resolvedTarget` is the pre-resolved object/corpse/NPC guid (when `my`, `32-hex`, or `entry:<id>` without `@x,y,z` was resolved by the caller).
OrderAnswer BeginCommander(ObjectGuid commander, Player* lead, std::vector<Player*> const& bots, TaskOrder const& order, ObjectGuid resolvedTarget, uint32 nowMs);

// `lootmethod`: the lead bot, the group's leader, sets it through the handler a client's window uses; the answer reads the method back from the group.
OrderAnswer SetLootMethod(RunRec& rec, Player* lead, std::vector<Player*> const& bots, TaskOrder const& order);
OrderAnswer SetLootMethodForParty(Player* lead, std::vector<Player*> const& bots, TaskOrder const& order);

// Called once per world tick from run::Update: looks at every running task every kTaskTickMs (never while none runs).
void Update(uint32 diffMs);

// The `come` orders the tasks queued since the last call, per run: the run's outside-order check must not take them for a hand-typed order (the caller moves
// the baseline). True once per queued order.
bool TakeQueuedOrders(ObjectGuid leader);

// Ends every task of a run: the answers are dropped (the run is paused, stopped or over), the loot watches are given back.
void Cancel(ObjectGuid leader);

// Drops a run's task state (the end of the run; next to run::ForgetRun).
void Forget(ObjectGuid leader);

// What the facts read of the carrier's task (F9): the bot of the last task order, whether one runs for it, and its answer since the order.
struct TaskState
{
    ObjectGuid carrier;         // the bot of the newest task order (empty: none since the run started or since a cancel)
    bool busy = false;          // a task runs for `carrier`
    int reply = 0;              // 0 none yet, 1 ACK, 2 ERR (the policy's Facts::taskReply)
    std::string text;           // the payload of an ACK ("opened", "5397:1,copper:30") or "<code>: <message>" of an ERR
};
TaskState StateOf(ObjectGuid leader);

// Per-bot task status for `.tb cmd state`: active or last finished task for each bot under `owner` (commander or run leader).
struct BotTaskState
{
    ObjectGuid bot;
    std::string botName;
    bool busy = false;
    int reply = 0;              // 0 none, 1 ok (done), 2 err (failed)
    std::string verb;           // open | loot | use | gossip
    uint32_t entry = 0;
    std::string text;
};
std::vector<BotTaskState> BotStatesOf(ObjectGuid owner, std::vector<Player*> const& bots);

// Whether a task runs for this bot (a bot has one at a time).
bool Busy(ObjectGuid leader, ObjectGuid bot);

// For `.tb run` / the state records: how many tasks run, and the totals since boot.
struct Totals
{
    uint64 began = 0, done = 0, failed = 0, cancelled = 0;
};
Totals const& TotalsSinceBoot();
}
}
}
