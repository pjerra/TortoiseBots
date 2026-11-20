# Slice 9, packages O9, F9, C9: doors, chests and loot orders of the run, the object and task facts, the start checks

Branch `s9/doors` (worktree `~/wt-s9-doors`), from `tbf/s9/l` (the loot package, merged with `tbf/main`: the self-led run `feab6c882`). Spec `~/TortoiseBots-audit/FOREVER-SLICE9-SPEC.md` with the lead decisions of 2026-11-20 and both addenda (the ForeverCore team's working examples were read for the API only, nothing copied; the real-client checks: Deadmines doors 17153 / 16400 / 16399 / 16397, cannon 16398, doors open on click, the Iron Clad Door blown by the cannon, Lady Anacondra stacked with a Druid of the Fang). Core pin 6517af0 read only, **zero core changes**. Other workers edit `forever/src/host/run/*` (crowd control on `s8/g`): the run edits are NEW files plus the small hooks listed in section 5.

## 1. O9: the task module (`forever/src/host/run/TbRunTasks.{h,cpp}`, `TbRunTasksPure.h`)

The run leader's orders `open`, `loot`, `lootmethod` (and `use`) were refused `unsupported` (TbRunOrdersPure.h, S6). The Turtle machinery behind them is about 1,700 lines of `commands/Commander.cpp` (4182-5883); this is a REWRITE for the three verbs the two dungeons need, not a port: target (handle or `entry:id@x,y,z`) -> walk to a stand -> act -> result, one task per bot.

| Part | Where | What |
|---|---|---|
| Grammar | `TbRunTasksPure.h` `ParseTaskOrder`; hooked into `ParseOrder` (TbRunOrdersPure.h) | `loot <handle\|entry@pt> <bot> [item]`, `open <handle\|entry@pt> <bot>`, `lootmethod <ffa\|group\|rr> [threshold]`, `use <item> <bot>`: the policy's own strings, parsed with the Turtle parsers (`RC::ParseLootOrder` ...) |
| Verdicts | `TbRunTasksPure.h` `JudgeLock`, `JudgeLootMethod`, `AlreadyUsed`, `PolicyLootMethod`, `StartLootMethodOk` | the lock row through the Turtle judge `RC::CheckLock`; the loot method rule; a door that is not at rest is open already |
| Task clock | `TbRunTasksPure.h` `Advance` / `NoteActStart` | the Turtle machine (`RC::AdvanceTask`: walk, act, stall 8 s, budgets) as ONE function over a plain observation `Obs`; returns `OrderWalk`, `BeginAct`, `Succeed`, `Fail` |
| Orders | `TbRunTasks.cpp:527` `Begin`, `:591` `SetLootMethod` | judged now (bot, target, reach, lock); a refusal changes nothing; the task is queued and the order ACCEPTED at once, the result reaches the policy as the facts' `taskReply`/`taskText` (as the Turtle slot's `taskReply` does) |
| Target search | `TbRunTasks.cpp:197` `FindByEntry` | the spawned game object (for `loot` then the corpse) of the entry nearest the plan's point **within 5 yd** (`kEntrySearchYd`, spec 7); none: `invalid-target`, never another object of the entry. Handles of `ReadFacts` (a boss corpse) resolve through `ResolveHandle` |
| Lock | `TbRunTasks.cpp:124` `GoLockFacts` | `sLockStore` rows of the object's lock id: `LOCK_KEY_ITEM` (the bot carries the key) or `LOCK_KEY_SKILL`; judged at `Begin` and again before the act; the core's `Use` of a door or goober does not look at the lock (GameObject.cpp:2724, :2949), so the host refuses what the core would let through (do less) |
| Door / lever act | `TbRunTasks.cpp:290` `BeginObjectAct` | lock, then **AlreadyUsed** (the core's `Use` TOGGLES a door: a door whose state is not READY is answered `done: already open (state N): not used again`), then the typed `GameObjUse` packet into `HandleGameObjectUseOpcode` (SpellHandler.cpp:201); done = the packed loot/go state changed within 4 s |
| Chest / corpse act | `TbRunTasks.cpp:317` `BeginLootAct` | `ai::LootObject` check, lock, `RC::LootOrders().Watch` BEFORE the open; corpse: `LootOpenCompat` (the typed `CMSG_LOOT_UNIT`); chest: the engine's own `open loot` (`DoSpecificAction`, `loot target` set; the opening spell, a key the bot carries); done = the ledger's window closed (StoreLootAction reports into it), items listed `5397:1,copper:30` |
| Walk | `TbRunTasks.cpp` `OrderWalk` | the executor's own `come` to a stand 1.5 yd short of the object on the bot's side, on the floor (`RC::StandingPoint` + `GetHeightCompat`), re-sent every 3 s while the bot stands still; the tasks' `come` orders are the run's (`fromRun`) and the outside-order baseline moves past them (`TakeQueuedOrders`) |
| Clock | `TbRunTasks.cpp:615` `Update` | every world tick from `run::Update`, a look every 250 ms per run with tasks |
| Texts | `TbRunTasksPure.h` | the answers carry the words the policy matches (`LootRightsText` = `nothing this bot may loot`, `LootEmptyText`, `in-combat:`, `busy:`, `invalid-target: The object is gone.`); `test_run_tasks` checks each against `DungeonRun::LootRefusalOpen` / `TaskResendable` / `GoneIsDone` |

Decisions made here (the spec leaves them open):
* **`use <item>` is parsed and refused `unsupported`**: no plan of the two dungeons has a `use_item` row (Deadmines: 6 wait_object, 3 open, 2 wait_clear, 1 loot), the item-use path is not ported, and `use_on` is out (spec 1).
* **`lootmethod master` and `nbg` are refused** (the greed-only roll code votes in GROUP_LOOT only, L9 round 2: a need-before-greed roll would time out); `ffa`, `rr`, `group` are carried out and read back from the group; a threshold below uncommon is refused `invalid` (the core's own rule, GroupHandler.cpp:410).
* **An open door is not used again** (see above). The policy's open step has no other guard, and the core's `Use` closes an open door again.
* A refusal at `Begin` is `invalid-target` for a target not found (spec 7), not `not-found` (T): `GoneIsDone` matches `not-found:` only, and no plan row of these two dungeons carries the gone-is-done flag.
* The corpse window not opened by the core is answered with the policy's `nothing this bot may loot` words after the retries (rights, range, turn are not told apart by the handler: it returns silently), so the policy moves the order to another bot as it does on Turtle.

## 2. F9: the facts (`TbRunFactsPure.h`, `TbRunFacts.cpp`)

| Fact | Pure (TbRunFactsPure.h) | Core read (TbRunFacts.cpp) |
|---|---|---|
| `objectState`, `objectReadable` (wait_object) | `PlanObjectState` `:802` (the plan's 0 ready = core `GO_STATE_READY` 1, 1 active = `GO_STATE_ACTIVE` 0, 2 = `GO_STATE_DESTROYED`: the Iron Clad Door after the cannon), `ReadWaitObject` | `:998` `ObjectAt`: the nearest spawned object of the entry within 5 yd of the row's point; `readable` = lead alive, not teleporting, on the plan's map, the point within ReactDistance, grid loaded |
| `clearUnits` (wait_clear) | `ReadWaitClear` `:837` | `:1036` `ClearHostiles`: living hostile creatures (`AnyUnfriendlyUnitInObjectRangeCheck`), `engaged` = in a fight with a party bot or pet |
| `carrier`, `carrierHasItem`, `carrierHasEntryItem`, `carrierBusy`, `itemsSplit`, `taskReply`, `taskText` (loot, open, use_item) | `ReadTaskFacts` `:885` with the policy's own `OutstandingCarrierVerdict` / `PickItemCarrier`; a retired order (its bot died or left) is retired and the step read again from a fresh pick | `:1075` `TaskBags`: every party bot's bags, the task module's carrier, busy bots and answer (`tasks::StateOf`) |
| `partyHasItem`, `itemHolder`, `lootTurn`, `corpseChecked`, `corpseHasItem` (loot) | `ReadTaskFacts` | `TaskBags`: the remembered corpse (handle -> creature) read only after a refusal: `Loot::roundRobinPlayer`, `items[].itemid/is_looted` |
| `lootMethod`, `leadLeadsGroup` | -- | `:1220`: every tick; the five 1.12 methods keep their numbers, the core's PERSONAL_LOOT (5) and anything else reads `-1`. The free-for-all detour stays off (`lootFfaOn` forced false, spec D4) |
| `doors` | -- | `:824` `InstanceDoors`: the destination of every area trigger into the plan's map (`sAreaTriggerStore` ids through `ObjectMgr::GetAreaTrigger`), once per map |

`StepWorld` gained `ObjectAt`, `ClearHostiles`, `TaskBags`, `RetireTask` with "not known" defaults, so the S6 scripted worlds still compile and read "nothing there" for them. The S6 field table (`RUN-S6-F-FIELDS.tsv`) is unchanged: the Ragefire Chasm plan has no object step, and `test_run_facts` still holds every skipped field at its default.

## 3. C9: the run commands (`TbRun.{h,cpp}`, `TbPartyState.cpp`, `TB-COMMANDS.md` section 11)

* **Start checks** (`StartRun`, before anything is changed): `bags` (a member with fewer than 12 free bag slots, spec 5d) and `loot-method` (not group loot, spec D4).
* **bags-full pause** (`BagsFullForStep`, before the policy decides): a loot step for an item whose carrier has two or fewer free slots pauses with `bags-full: ...` instead of looting and finding the item missing (the loot code's floor, L9).
* **Run keys** `carrier=`, `item=<id>:held|missing`, `obj=<entry>:<ready|active|active_alt|none>` (`SnapshotStepFacts`, from the facts the policy decided on).
* **Events** `open`, `loot` (a task ended, from the task module), `object` (a wait_object ended on its state), `clear`, `item` (`NoteStepEnd`, from the facts of the tick that ended the step).
* **`.tb state`**: per bot `bagfree=`, `money=`; end line `lootmethod=`, `lootthr=`.
* Pause words: the policy's own notes carry the reason (`wait_object ... did not change`, `open: ... does not carry item`, `loot failed`, `bags-full`); the module's codes stay `outside-order`, `player-in-group`, `lead-off-map`, `command`, plus `bags-full`.

## 4. Data this slice needed and where it came from

* **Plan rows**: `forever/sql/world/2026_11_20_10_tortoisebots_plan_wailing_caverns.sql` (13 legs, 183 steps, 203 unit rows, 6 boss rows) and `2026_11_20_11_tortoisebots_plan_deadmines.sql` (15 legs, 172 steps incl. 6 wait_object, 3 open, 2 wait_clear, 1 loot, 231 unit rows, 6 boss rows), exported by `plan-export --forever --mmaps <maps 36 and 43 of the test server> --rev b7c87854s9` from the agent's `dungeons/*.json` at `b7c87854`. **Finding for A9: `plan-export --forever` on agent main REFUSES every Deadmines object row** (`use_object`, `wait_for`, `loot_object`: "no object rows in this format", `internal/planexport/forever.go` `classifyStep`) and does not apply `alt_entries` (only `plan-check --forever` does), so on main the Deadmines export fails and the 17153 doors would be written with the Turtle entry. Local patch (agent worktree branch `s9/export-objects-scratch`, NOT pushed): carry the three kinds, `useServerEntries(p, "forever")` before `Build`, and a tie-safe read-back tolerance (`near3` 0.0005 -> 0.00051: a coordinate exactly on a half thousandth, -93.3125, failed the read-back check). The rows are the generator's output with that patch, never edited by hand. The agent owner must land the patch (or the equivalent) before the module's plan rows are re-exported.
* Object spawns on the test server's world DB match the plan rows to 0.1 yd (13965, 17153 x2, 17154, 16400, 16399, 17155, 16398, 16397): the 5 yd rule holds for all eight.

## 5. Hooks in existing run files (for the later merge with `s8/g` and the others)

| File | Hook |
|---|---|
| `TbRunOrdersPure.h` | `#include "TbRunTasksPure.h"`; 4 enum values, `Parsed::task`, the four verbs leave the `unsupported` list (one `if`) |
| `TbRunOrders.cpp` | `#include "TbRunTasks.h"`; 4 `case` lines in `Issue` |
| `TbRun.h` | 9 `RunRec` fields (C9 snapshot), `NoteTaskEvent` declaration, two words in the code list |
| `TbRun.cpp` | `tasks::Cancel` in `CancelRunWork`, `tasks::Forget` in `Finish` and `StartRun`, `tasks::Update` + baseline in `Update`; the C9 helpers before `TickRun`; two `refuse` blocks in `StartRun`; 2 lines in `TickRun` (snapshot, bags-full); 3 keys in `RunRecord`; step-end events in `Observe`; `NoteTaskEvent`, `LootMethodWord` |
| `TbRunHooks.h` | `LootMethodWord` |
| `TbRunFactsPure.h` | new section before `ReadStepFacts`; 3 `case` groups replace the "not read" default; `LeadView::name`; 4 `StepWorld` virtuals with defaults |
| `TbRunFacts.cpp` | `InstanceDoors`, 4 overrides in `CoreStepWorld`, 6 lines in `ReadFacts`, `lead.name` |
| `TbPartyState.cpp` | 2 lines per bot record, 4 in the end line |

## 6. Tests

`forever/tools/test_run_tasks.cpp` (grammar against the policy's own strings, lock verdicts, the task clock: walk, act, effect, in-combat, stall, no change, vanished, retries, loot window, refused window, cap; answer texts against `LootRefusalOpen` / `TaskResendable` / `GoneIsDone`), `test_run_facts_s9.cpp` (a Deadmines-shaped plan through the pure facts AND the policy's own `Tick`: open and loot orders, wait_object by state word, wait_clear, carrier by item, split, retired order, loot rights), `test_slice9_doors_wiring.py` (order of calls and the hooks), `test_run_orders.cpp` updated (the four verbs left the unsupported list).

(gates, live results and commits: see the final sections, filled at the end of the work)
