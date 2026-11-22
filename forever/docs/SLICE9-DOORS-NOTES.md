# Slice 9, packages O9, F9, C9: doors, chests and loot orders of the run, the object and task facts, the start checks

Branch `s9/doors` (worktree `~/wt-s9-doors`), from `tbf/s9/l` (the loot package, merged with `tbf/main`: the self-led run `feab6c882`). Spec `~/TortoiseBots-audit/FOREVER-SLICE9-SPEC.md` with the lead decisions of 2026-11-20 and both addenda (the ForeverCore team's working examples were read for the API only, nothing copied; the real-client checks: Deadmines doors 17153 / 16400 / 16399 / 16397, cannon 16398, doors open on click, the Iron Clad Door blown by the cannon, Lady Anacondra stacked with a Druid of the Fang). Core pin 6517af0 read only, **zero core changes**. Other workers edit `forever/src/host/run/*` (crowd control on `s8/g`): the run edits are NEW files plus the small hooks listed in section 5.

## 1. O9: the task module (`forever/src/host/run/TbRunTasks.{h,cpp}`, `TbRunTasksPure.h`)

The run leader's orders `open`, `loot`, `lootmethod` (and `use`) were refused `unsupported` (TbRunOrdersPure.h, S6). The Turtle machinery behind them is about 1,700 lines of `commands/Commander.cpp` (4182-5883); this is a REWRITE for the three verbs the two dungeons need, not a port: target (handle or `entry:id@x,y,z`) -> walk to a stand -> act -> result, one task per bot.

| Part | Where | What |
|---|---|---|
| Grammar | `TbRunTasksPure.h` `ParseTaskOrder`; hooked into `ParseOrder` (TbRunOrdersPure.h) | `loot <handle\|entry@pt> <bot> [item]`, `open <handle\|entry@pt> <bot>`, `lootmethod <ffa\|group\|rr> [threshold]`, `use <item> <bot>`: the policy's own strings, parsed with the Turtle parsers (`RC::ParseLootOrder` ...) |
| Verdicts | `TbRunTasksPure.h` `JudgeLock`, `JudgeLootMethod`, `AlreadyUsed`, `PolicyLootMethod`, `RunLootMethodOk` | the lock row through the Turtle judge `RC::CheckLock`; the loot method rule (at the start, every running tick and before every act); a door that is not at rest is open already |
| Task clock | `TbRunTasksPure.h` `Advance` / `NoteActStart` / `NoteActCast` | the Turtle machine (`RC::AdvanceTask`: walk, act, stall 8 s, budgets) as ONE function over a plain observation `Obs`; returns `OrderWalk`, `BeginAct`, `Succeed`, `Fail`; a cast the act started (a locked chest's Opening) extends the act budget |
| Orders | `TbRunTasks.cpp:527` `Begin`, `:591` `SetLootMethod` | judged now (bot, target, reach, lock); a refusal changes nothing; the task is queued and the order ACCEPTED at once, the result reaches the policy as the facts' `taskReply`/`taskText` (as the Turtle slot's `taskReply` does) |
| Target search | `TbRunTasks.cpp:197` `FindByEntry` | the spawned game object (for `loot` then the corpse) of the entry nearest the plan's point **within 5 yd** (`kEntrySearchYd`, spec 7); none: `invalid-target`, never another object of the entry. Handles of `ReadFacts` (a boss corpse) resolve through `ResolveHandle` |
| Lock | `TbRunTasks.cpp:124` `GoLockFacts` | `sLockStore` rows of the object's lock id: `LOCK_KEY_ITEM` (the bot carries the key) or `LOCK_KEY_SKILL`; judged at `Begin` and again before the act; the core's `Use` of a door or goober does not look at the lock (GameObject.cpp:2724, :2949), so the host refuses what the core would let through (do less) |
| Door / lever act | `TbRunTasks.cpp:290` `BeginObjectAct` | lock, then **AlreadyUsed** (the core's `Use` TOGGLES a door: a door whose state is not READY is answered `done: already open (state N): not used again`), then the typed `GameObjUse` packet into `HandleGameObjectUseOpcode` (SpellHandler.cpp:201); done = the packed loot/go state changed within 4 s |
| Chest / corpse act | `TbRunTasks.cpp:317` `BeginLootAct` | `ai::LootObject` check, lock, `RC::LootOrders().Watch` BEFORE the open; corpse: `LootOpenCompat` (the typed `CMSG_LOOT_UNIT`); chest: the engine's own `open loot` action EXECUTED directly (`GetAction("open loot")->Execute`, the Turtle `ExecuteOrderedAction` path, `loot target` set; the opening spell, a key the bot carries). Through `DoSpecificAction` the action's own isUseful gate refused the chest the bot stood at (found live, section 8); done = the ledger's window closed (StoreLootAction reports into it), items listed `5397:1,copper:30` |
| Walk | `TbRunTasks.cpp` `OrderWalk` | the executor's own `come` to a stand 1.5 yd short of the object on the bot's side, on the floor (`RC::StandingPoint` + `GetHeightCompat`), re-sent every 3 s while the bot stands still; the tasks' `come` orders are the run's (`fromRun`) and the outside-order baseline moves past them (`TakeQueuedOrders`) |
| Clock | `TbRunTasks.cpp:615` `Update` | every world tick from `run::Update`, a look every 250 ms per run with tasks |
| Texts | `TbRunTasksPure.h` | the answers carry the words the policy matches (`LootRightsText` = `nothing this bot may loot`, `LootEmptyText`, `in-combat:`, `busy:`, `invalid-target: The object is gone.`); `test_run_tasks` checks each against `DungeonRun::LootRefusalOpen` / `TaskResendable` / `GoneIsDone` |

Decisions made here (the spec leaves them open):
* **`use <item>` is parsed and refused `unsupported`**: no plan of the two dungeons has a `use_item` row (Deadmines: 6 wait_object, 3 open, 2 wait_clear, 1 loot), the item-use path is not ported, and `use_on` is out (spec 1).
* **Every `lootmethod` (`ffa`, `rr`, `group`, `master`, `nbg`) is carried out** and read back from the group; the bots vote group loot and need-before-greed and stay silent otherwise (no roll window in free-for-all and round robin; the human master distributes master loot). A threshold below uncommon is refused `invalid` (the core's own rule, GroupHandler.cpp:410).
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

## 3. C9: the run commands (`TbRun.{h,cpp}`, `TbPartyState.cpp`, `TB-COMMANDS.md` section 12; the commander card keeps section 11 after the integration)

* **Start checks** (`StartRun`, before anything is changed): `bags` (a member with fewer than 12 free bag slots, spec 5d) and `loot-method` (not group loot or need-before-greed).
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

## 7. Gates (final commit, build slot 2 of `yulon-ubuntu2`)

| Gate | Result |
|---|---|
| census `FOREVER_CENSUS_LIST=forever/SLICE9.txt` | `files 178  compile clean 178  with errors 0  error places 0  includes of a header ForeverCore does not have 0` (SLICE5..1 are subsets of that list; none of this package's files is a legacy file) |
| option ON `worldserver` | `CMAKE_RC=0 MAKE_RC=0` (first build, and again after the two live fixes); `nm`: `tasks::Begin`, `tasks::Update`, `tasks::SetLootMethod` strong in `worldserver` |
| option OFF `tortoisebots` | `CMAKE_RC=0 MAKE_RC=0` (first build, and again on the final commit) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `up to date`, `(a)=0` |
| policy tests (`policy-tests.sh` copy, 476+ tests) | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL: the same six as on the unchanged `port/forever` tree (`test_combat_monitor_service*`, `test_decision_capture_*`, `test_log_file_rotation`, `test_run_trace_writer`) |
| `test_run_tasks` | 141 checks, 0 failed |
| `test_run_facts_s9` | 42 checks, 0 failed (mutation: `PlanObjectState(1)` returning 1 fails 3) |
| `test_run_orders` / `test_run_c6` / `test_run_facts` (RFC rows + field table) / `test_run_liquid` / `test_run_cancel` / `test_forever_plan_match` | 144 / 28 / PASS / PASS / PASS / 38 checks |
| Turtle `vm-build` / `vm-test tb` (no `forever-world` container running) | `CMAKE_RC=0 MAKE_RC=0 QUEUE_RC=0` / `RESULT: PASS kind=tb wt=wt-s9-doors sha=f477cee8b` |
| python wiring | `test_slice9_doors_wiring.py` 21 tests, `test_slice9_l9_wiring.py`, `test_run_review_wiring.py`, `test_slice4_d4_wiring.py`, `tools/test_forever_no_direct_queries.py`: all OK |

## 8. Live results (build VM, `~/forever-run` / `forever-world`, my binary copied in and the original restored; level-22 party Mtank (prot warrior), Mpriest, Mdpsa (arms), Mmage, Mrogue; commander Mcmd, GM)

Setup notes: `.tb level 22` + `.tb train` per bot; the talent spell ids of `talents-level22.csv` mostly answered `result=unknown` on this server (only the first rank of each talent and a few more learned: the data file's rank spell ids do not all exist as spells), so the party's talents are partial. A bot only follows its commander in the open world: the commander must be teleported with the party (`.tb tele Mcmd ...`), and the engines of a party that entered an instance start only after the commander joined the tank's instance (`.tb tele Mcmd to Mtank`).

**Wailing Caverns** (`.tb run start Mtank wailing_caverns`, run 1, 3,624 s, steps 0-35 of 183, 29 deaths, wipes recovered by the entrance revive):
* a. `.tb plan wailing_caverns check Mtank`: units matched, the Druid of the Fang stacked on Anacondra's spot read as `missing` for 3 of 4 spawns as expected; b. the run started and ran by itself, no order from outside; c. bots looted corpses (`StoreLootAction` rows: Small Raptor Tooth, Flecked Raptor Scale, Gelatinous Goo ...); **d. one green drop (Worn Turtle Shell Shield): five `LootRoll` rows, all `greed`, one winner, nothing timed out: U4 answered, a roll vote from a socketless session works**; e. Kresh killed (`boss=3653:dead` in the run record; `.tb instance info` `boss=585:dead;586:alive;587:dead;...`);
* wipes: `death` x3-5 then `revive party` (`events` kinds `wipe`, `revive`): the bots rise at the entrance inside (D2 held) and the run walked back and went on (steps 7, 8, 10, 14, 17, 21, 33 each lived through one or more);
* `.tb run start` refused `bags` live (Mdpsa had 7 free slots after the run's loot): the check works; `.tb state` shows `lootmethod=group lootthr=2`, `bagfree=`, `money=`; the run record carries `carrier=` / `item=<id>:missing` (seen on the loot plan);
* it STOPPED at step 35 (leg 4, cobrahn_b) with `pause why=cmd move -61.4 323.7 -106.6 refused: unreachable: ... crosses or ends in liquid (lava or water) at -41.3 298.7 -105.3`: the S6 liquid rule (water in the Cobrahn cavern) refuses the point; not a slice 9 matter (no object or loot step is involved). The run was stopped there by hand. Not reached: the Serpentis hop (g), `.tb kill party` after Cobrahn (f: the natural wipes covered the revive-and-walk-back), Verdan.
* 0 `ASSERT` / `Segmentation` in the log for the whole session.

**Deadmines** (no full-clear: every plan row of the real `deadmines` plan sits next to a Defias pack, so the party died on arrival at the door; the object steps were therefore run as short test plans in this server's DB only, `s9_dm_door`, `s9_dm_loot`, `s9_dm_cannon2`: step rows COPIED from the exported `deadmines` plan (legs 2 and 8), never shipped, and the four bots GM-flagged so the packs ignore them):
* i. **open 17153 (leg 2)**: `event kind=open why=done: opened` at t=45 s (after the rest phase), `event kind=object why=17153:active` 2 s later, plan done: U1 answered, a bot's typed use packet opens a Heavy Door (the door's own state changed READY -> ACTIVE);
* k. **chest 17155**: first run (the engine's `DoSpecificAction`) failed `The bot could not start opening it` (the action's isUseful gate): fixed by executing the action directly; second run: `event kind=loot why=done: 5397:1` after 20 s (the opening cast + the window), `event kind=item ... item 5397 is in Mtank's bags`: U3 answered; **cannon 16398** with the item: first run failed `The object showed no change after the bot used it.` (the goober's script answers the use and its own state never changes): fixed (section 1); second run: `event kind=open why=done: used: the object's script answers ...`, then `wait_object 16397` ended `event kind=object why=16397:active_alt` 1 s later = `GO_STATE_DESTROYED`: the Iron Clad Door blown open: U2's effect read; `.tb additem Mtank 5397` works (U8).
* NOT run live: the Sneed and Gilnid door waits (16400 / 16399: j), the wait_clear after the chest (the Overseer fight; with GM bots the 45 yd clear would wait on the unrelated packs), a wipe on the docks (l), VanCleef (m), the 3 h soak (n), `bags-full` pause, the `lootmethod` order. The F9 wait reads are covered by the standalone test and by the `object` events above.
* Deadmines entry through trigger 78 worked (`BotMgr: map 36 (instance 1) got entrance world_safe_locs 1100078`), the 5 bots landed in one instance.

Restore: the test server was stopped with `s9d1-sess.sh stop` and its original `worldserver` copied back (`cmp` equal to `worldserver.pre-m3`); the three `s9_dm_*` test plans stay in forever-db's `ai_dungeon_*` tables (delete with `DELETE ... WHERE plan LIKE 's9\_dm\_%'`); the plan rows of `wailing_caverns` and `deadmines` were put into `~/forever-run/sqlsrc/sql/tortoisebots/world/` (applied at boot by the updater) and into run2's `sqlsrc` folder (not applied there).

## 9. Review round 1 (2026-11-20): one Codex finding, two Gemini findings

| Finding | Verdict | Fix, and where the test fails without it |
|---|---|---|
| Codex [high]: a loot method changed during a run is observed (`Facts::lootMethod`) but never enforced, so later corpse/chest tasks still loot | fix | `TbRun.cpp:791` reads the method on every running tick: a drift cancels the run's tasks and pauses with the code `loot-method` (the reason names the method and asks for group loot, then `.tb run resume`); `ResumeRun` (`:1106`) re-reads the group and refuses with `loot-method` until it is back. `TbRunTasks.cpp:107` reads the lead's group again right before every loot/object act (`:521`), so no act starts in the up-to-1 s between run ticks or the 250 ms between task looks. Tests: `test_run_tasks` refuses round robin, master and need-before-greed (the reason names each); `test_slice9_doors_wiring` pins the tick gate before `DungeonRun::Tick` (with `tasks::Cancel` and the pause before the policy can order), the act gate before both act calls, and the resume re-check. |
| Gemini 1 [medium]: the chest Opening cast time is not added to the act budget (`ActBudgetAfterCast` unused), so an 8 s act can time out while its own cast still runs | fix | `TbRunTasks.cpp:377-381`: when `open loot` starts a cast, its remaining time is added with `NoteActCast` (`TbRunTasksPure.h:425`), the Turtle rule (`ActBudgetAfterCast`). Test `TestChestCastBudget` in `test_run_tasks`: 9 s after the act began with a 5 s cast the task still waits and then succeeds; without the note the old 8 s budget answers `timeout` (mutation checked). Wiring pin: the note is called after `Execute` and before `Started`. |
| Gemini 2 [low]: `CoreStepWorld::ObjectAt` runs the grid scan even when a dead lead is on another map (a graveyard), so a spawn at the same coordinates there can read as found | fix | `TbRunFacts.cpp:1009`: the early return also covers a null plan and a lead off the plan's map, before `GetGameObjectListWithEntryInGrid`; "not found" stays unreadable. Wiring pin: the map check precedes the scan. |

Decisions: a drift **pauses** (do less; the supervisor sets the method) instead of stopping the run, and the pause needs a validated resume. The `lootmethod` order carries out every method, but while a run is running any method but group loot or need-before-greed pauses it and blocks every act; the free-for-all detour stays off (`lootFfaOn` false, spec D4).

Gates (final tree, build slot 8 of `yulon-ubuntu2`): census `FOREVER_CENSUS_LIST=forever/SLICE9.txt` `files 178 compile clean 178 with errors 0 error places 0 includes of a header ForeverCore does not have 0`; option ON `tortoisebots` and `worldserver` `CMAKE_RC=0 MAKE_RC=0` (the `worldserver` binary carries the new pause text); option OFF both targets `CMAKE_RC=0 MAKE_RC=0` (slice code not linked); `slice_gen_context.py --check` up to date and `slice_registry_check.py` `(a)=0`; `test_run_tasks` 148 checks, `test_run_facts_s9` 42, `test_run_orders` 144, `test_run_c6` 28, `test_run_facts` PASS with the Ragefire Chasm rows, `test_run_liquid` and `test_run_cancel` PASS; `test_slice9_doors_wiring.py` 24 tests and the other wiring tests OK. `tools/verify_all.sh` steps 1-6 and 8/8b pass; its step 7 points at an incomplete `../tortoise-wow`, and the host contract passes against a complete core (`tortoise-wow-f3`). Not run: Turtle `vm-build` / `vm-test tb` (nothing outside `forever/` changed) and the Go agent gates (no Go change).
