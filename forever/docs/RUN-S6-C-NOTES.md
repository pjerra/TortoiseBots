# Slice S6, package C6 (the run tick and `.tb run`): the tank bot leads a dungeon run by itself

Branch `run/c6` (worktree `~/wt-run-c6`), the DEFAULT build slot (no `FOREVER_SLOT`), on `run/f6` (slices 1-4 + S5's halves + F6's facts). Core read only at the pin 6517af05 (zero core changes). No server was started and no SQL was applied: the proof is the builds, the census, the generator checks and two standalone tests.
Spec: `~/TortoiseBots-audit/FOREVER-RUN-S5S6-SPEC.md` 5.1-5.4 (C6's row), D1 and D3; common brief `RUN-S6-COMMON.md`; F6's "Needs from C6" (`RUN-S6-F-NOTES.md` section 8) is implemented item by item (section 3).

## 1. Files

| File | Lines | What |
|---|---|---|
| `forever/src/host/run/TbRun.cpp` | 992 (+752) | S5's header part untouched; appended: the executor-free helpers, the binding, `Observe`, `Finish`, `TickRun`, `Update`, `StartRun`, `StopRun`, `PauseRun`, `ResumeRun`, the four host hooks |
| `forever/src/host/run/TbRun.h` | 142 (+68) | appended: `RunRec` fields of the tick (`bound`, `orderSeen`, `stopPending`, `seen*` ...), `TickTotals`/`TickCost`, `Ended`, `StartRun`/`StopRun`/`PauseRun`/`ResumeRun` and their outcome structs |
| `forever/src/host/run/TbRunHooks.h` | 43 (+13) | `run::Update` (the tick) and four host hooks (`BotInRunningRun`, `BotRunKillFirst`, `BotRunArmedTriggers`, `BotRunPlanMap`) |
| `forever/src/host/run/TbRunCommands.h/.cpp` | 25 / 265 | `.tb run start|stop|pause|resume|status|events` (parse and reply only; the logic is in TbRun.cpp), plus the option-OFF `engine-not-built` stub |
| `forever/src/host/run/TbRunOrders.h` | 74 | a BYTE COPY of O6's header at its commit a752649 (the interface C6 calls) |
| `forever/src/host/run/TbRunOrdersStandIn.cpp` | 38 | WEAK stand-ins for O6's four functions (every order refused `unsupported`, `StartCheck` refuses): delete at the merge |
| `forever/src/TbScripts.cpp` | +5 | the include, the `run` row of the `.tb` table, one `run::Update(diff)` call at the end of `OnBeforeMapsUpdate` (slice build only) |
| `forever/tools/test_run_c6.cpp` | 181 | standalone test of the policy contracts the tick relies on (section 5) |
| `forever/docs/TB-COMMANDS.md` | +45 | section 10, the `.tb run` reference with hand-built samples |

## 2. How a run works (what the tick does, in order, once a second per run)

`run::Update` (TbRun.cpp:693) accumulates the world tick's `diff`; at 1000 ms it takes `GameTime::GetGameTimeMS()` and calls `TickRun` (:485) for every run that has not ended. The policy gets the real elapsed `dt` (at most 5 s).

1. **Own time limit** (`AiPlayerbot.DungeonRunMaxMinutes`): ends like a stop (below). A **pending stop** (a stop that came in a fight): no policy tick, no order; when no living member or pet is in combat the stop takes effect.
2. A leader that no longer leads the party ends the run (`stopped`). A member that joined (already commanded) is bound too, one that left gets its commander back (`SyncBinding`, :333).
3. **A real player in the group** pauses the run (system run, T L6: `SystemRun::JoinFlagAfterScan`/`DecideJoinStep`, `why=player-in-group`); it resumes by itself when the group is bots only again.
4. **Paused**: only the pauses that end by themselves are watched: `LeadBackWhilePaused` (leader off the map) and `CeilingFightOverWhilePaused` (the party out of combat). Events are derived and the tick returns; no `ReadFacts` while paused.
5. **Running**: the leader not in the world, or alive on another map: `PauseLeadOffMap` (the same call F6's notes ask for when `ReadFacts` says false). **Outside order** (:361): any bot of the run whose `h3::LastOrder(bot).seq` moved since the run's last baseline was ordered by hand: `PausePlayer` with `why=outside-order` (no hook in `HandleOrder` is needed: the run's own orders are all queued inside the executor call, after which the baseline is taken again).
6. **A pull step none of whose members the unit rows matched to a spawn** (`StepMatchedMembers == 0`, once per step pass) is skipped as not cleared (`notCleared` + `NextStep`) with a `step` event `why=not-cleared: ...`; -1 (not known) goes on to the facts.
7. `ReadFacts` once (false: `PauseLeadOffMap`), `ApplyPlayerFacts`, `run.liveTactics`; O6's `TakeLateFailures` go to `OrderRefused` before the decision; `DungeonRun::Tick`; then the post-tick calls F6 listed: `StallStandIn(found=false)` and `ApproachStandFound(found=false)` when the policy asked (neither stand search is ported: "found nothing" is the do-less answer and the policy hops, retries or pauses by its own rules), `SpeedRateNow` (test speed off, touches nothing); `TakeLinkedEvents`/`TakeSay`/`deferLog` are logged and dropped, `leadWalkReset`, `stopReachDue`, `ccLandedDrop` cleared (no ported reader), `focusIconClear` takes the run's skull mark off (`DropFocusMark`, only where it still is).
8. **The one order** goes to O6's `Issue(rec, order, nowMs, gather)`; accepted: `NoteMovePathType`, a `pull` / `revive` event; refused: `StallDirectRefused`, else `OrderRefused(run, order, answer.text)` (the policy pauses or retries by its own rules; the unsupported verbs of spec 5.2 pause with the verb in the note). The outside-order baseline moves past what the executor queued.
9. **Events and counters** (`Observe`, :377): `start`, `step`, `pull`, `fight-end`, `rest`, `death` (a name new in `deadNames`), `wipe` (alive 0 of a party), `revive`, `pause`/`resume` (state change; `pauses` counts every pause), `done`, `stop`; one `TortoiseBots run: ...` log line each. `bossDead` is set only from what the facts read (a boss step passed and not in `notCleared`, or `bossOwnDead`/`bossDeadNearSpot`; `bossSearchAlive` makes it alive): a boss never read is not listed.
10. **Done** (the plan's end): `Finish` (:450).

**Cost of a run tick** (said, not measured live: no server): one `ReadFacts` (F6: 2-3 us in the pure half plus the core reads; at most a few path queries and grid scans, `FactsReport` counts them), one `DungeonRun::Tick`, at most ONE executor call, a few map lookups. Bounded per run; runs are at most one per party. The measured cost is `.tb run status all`: `tick_avg_us`, `tick_max_us` (round trip of `TickRun`), `facts_*`. It is NOT in `.tb perf`'s module share (the call is after that sample).

## 3. F6's "Needs from C6", item by item

1. `ReadFacts` once per tick, false pauses the run (`PauseLeadOffMap`, auto-resume by `LeadBackWhilePaused`); no `Facts` kept between ticks. 2. `ForgetRun(leader)` (and O6's `ForgetRunOrders`) in `Finish`. 3. `ApproachStandFound`, `StallStandIn`, `SpeedRateNow` after `Tick` (found=false, see 7 above). 4. `StepMatchedMembers` on pull steps (step 6 above). 5. A player in the group: `player-in-group` pause (3 above). 6. `.tb run status all` carries `Totals()` (`facts_*`).

## 4. The binding (decision D1) and stop

* `start` (TbRun.cpp:719): `StartCheck` (O6), then the party checks, then every OTHER member is bound to the tank with the calls `.tb master` makes (`BotManager::BindBotMaster` + `h3::SetCommander`); the member's host master and commanded flag before the run are kept in `RunRec::bound`. The tank itself is never rebound. **A member that is not commanded yet is refused (`not-commanded`)**: binding it would change its engine kit (the commanded threat back-off, `AiFactory.cpp:633`), `.tb master` rebuilds the kit through its private `rekit` order, and O6's interface offers no queue call for it. The live test setup binds the party first (spec 7/8), so this costs nothing there; if it ever must change, O6 exports a `QueueOrder` for `rekit` and `start` queues it for each newly commanded member.
* `stop`/done: the `stay chat shortcut` action (the one `.tb order <bot> stay` runs) is called at once for the leader and each member (the engines tick in `BotMgr::Update` before the run tick, never inside it; it needs no queue), then `ReleaseMember` gives back the old host master and commander, unless somebody else rebound the bot meanwhile (a `.tb master`, `.tb group`: the newer decision stays). `stayed=` in the reply counts the bots whose action ran. In a fight: `pending=yes` (no order until it ends).

## 5. Tests

`forever/tools/test_run_c6.cpp` (28 checks, VM): a made-up four-step plan and the REAL policy header: a skipped pull step moves on and stays "not cleared"; a refused order (the executor's `unsupported` text) pauses with the answer in the note and `ResumeChecked` goes on; a leader off the map pauses and resumes by itself after the settle time, not at the first tick; `.tb run pause` is never resumed by the run, `resume` is; a stop ends `Done` with `stopped:`; the time limit; the system join step. The tick itself needs the core and is for the merge package's live test (spec 7 d-k).

## 6. What the merge must do

1. Merge O6: delete `TbRunOrdersStandIn.cpp`; `TbRunOrders.h` is a byte copy of O6's (take O6's if it changed). `Issue`'s answer text drives everything the policy decides on a refusal: if O6 changed `OrderAnswer`, adapt the one block in `TickRun` (the `Issue` call).
2. Host hooks (TbHostOwnership.cpp answers four legacy questions with "no run exists"): wire `IsInRunningDungeonRun` -> `run::BotInRunningRun`, `IsRunKillFirst` -> `run::BotRunKillFirst`, `RunArmedAreaTriggers` -> `run::BotRunArmedTriggers`, `BoundRunPlanMap` -> `run::BotRunPlanMap` (`TbRunHooks.h`). Not wired by C6 (not its file): until then the bots keep the "no run" answers (no pull-window hold from the legacy triggers, no map-change refusal): they do less, and the policy's own holds still work.
3. `SLICE5.txt`, `CMakeLists.txt`'s `TB_SLICE_LIST` and the P5 links are M5's (F6 notes, merge 1).
4. Live (M6): spec 7 d-k. Things to read first: `.tb run start Mtank ragefire_chasm` (needs the tank role and a commanded party), `.tb run events Mtank` while it walks, the `unsupported` verbs in the log, the outside-order pause, `.tb run stop` in and out of a fight.

## 7. Limits (said, not hidden)

* The stand-in searches (`NearestReachablePoint`, `ApproachStandSearch`) are not ported: a refused unreachable move PAUSES the run (`MoveRefusedUnreachable`, which would skip it with a note, is NOT called: its note says a stand-in was searched for and none found, which would be untrue); too-far legs (`TooFarLegWanted`) are not taken. The run pauses with the executor's answer; `resume` retries.
* `leadWalkReset` (stand a seated leader up before a walk) is dropped: O6's `come` does `ai->Reset()`; observation g (rest) must confirm the tank walks on after a meal.
* A pause while paused only watches the two auto-resumes above; door, hold and player-hold pauses do not occur in RFC.
* Only host-bot parties: a real player in the group pauses; nothing is ordered for a player.
* The `run` record's `boss=` and `pull=` are as H5 defined them (F6/C6 fill what they read).

## 8. Gates (final tree)

All in the default build slot of the VM, on the final code commit (`C6: server log through the compat log`; later commits are notes).

| Gate | Result |
|---|---|
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 build.sh 12 worldserver`) | `CMAKE_RC=0 MAKE_RC=0`, links; `nm`: `run::Update`, `StartRun`, `StopRun`, `HandleRun` strong, `run::Issue`/`StartCheck` the weak stand-ins. (The first ON build failed on `TC_LOG_INFO`, which `compat/botpch_forever.h` redefines; the three calls are `sLog.outString` now, as `TbOrders.cpp` writes.) |
| option-OFF module build (`-DTORTOISEBOTS_SLICE1=OFF`, target `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` (`HandleRun` answers `engine-not-built`; the other files are empty objects) |
| census SLICE5 / 4 / 3 / 2 / 1 | `files 172 / 174 / 170 / 160 / 133, compile clean = files, error places 0`, includes of a missing header 0 (C6 adds no legacy file) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=215` |
| `test_run_facts` (F6's, unchanged) | `RESULT: PASS` |
| `test_run_c6` (g++ -std=c++17 -Wall -Wextra on the VM, `~/c6-test`) | `RESULT: PASS: 28 checks, 0 failed` |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed |
| server / SQL | none started, none applied |
