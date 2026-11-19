# Slice S5, package H5 (host): plan tables, step units, `.tb plan`, the run header

Branch `run/h5` (worktree `~/wt-run-h5`), build slot 2 (`FOREVER_SLOT=2`), on top of slice 4's host package (`slice4/h`, 3b1e59c84). Core read only at the pin 6517af05 (zero core changes). No server was started, no SQL was applied anywhere; the only database access was read-only SELECTs on forever-test1 (below).
Spec: `~/TortoiseBots-audit/FOREVER-RUN-S5S6-SPEC.md` sections 4.1, 4.4, 4.5 and H5's row of the rounds table; rules `SLICE4-COMMON.md` (C1-C5) and `TB-STATE-GRAMMAR-V2.md` with addendum 1. P5 (loader port) is in `~/wt-run-p5` and was not touched; A5 (client export) and S6 are not part of this package.

## 1. Files

| File | What |
|---|---|
| `forever/sql/world/2026_11_20_00_tortoisebots_plans.sql` | the seven plan tables, final form, `CREATE TABLE IF NOT EXISTS` with every column (no ALTER chain) |
| `forever/sql/world/2026_11_20_01_tortoisebots_dungeon_bosses_rfc.sql` | the four RFC `ai_dungeon_boss` rows copied from the Turtle migration 20261021114931 (`INSERT IGNORE`) |
| `forever/sql/README.md` | how the SQL is delivered and applied (section 2), the list of files |
| `forever/src/host/run/TbPlanUnits.h/.cpp` | `ai::LoadDungeonStepUnits()`, the unit store, `CheckPlanOnMap` (the match and its per-instance cache), the `plan`/`leg`/`unit` record writers |
| `forever/src/host/run/TbPlanCommands.h/.cpp` | `.tb plan list|<plan> info|<plan> check <character>|reload` (and the option-OFF answer) |
| `forever/src/host/run/TbRun.h/.cpp` | `RunRec`, `RunEvent`, the run table, `RunRecord`/`EventRecord`, `LoadPlans`, `AnnotateStateEnd`, the selftest records |
| `forever/src/host/run/TbRunHooks.h` | the few functions other files call (light: no policy header) |
| `forever/src/host/run/TbRunHeaders.cpp` | the run leader's decision headers (`runtime/DungeonRunPolicy.h`, `RemoteCommanderPolicy.h`, `SystemRunPolicy.h`) compiled in, in BOTH builds |
| `forever/src/host/run/TbPlanLoaderStandIn.cpp` | a WEAK stand-in for `ai::ReloadDungeonPlans` until P5 merges (see "Needs from P5") |
| `forever/src/TbScripts.cpp` | one row (`plan`), one call in `OnWorldInitialized` (`run::LoadPlans()`, slice build, module enabled, after the engine config) |
| `forever/src/host/TbH3.cpp`, `TbPartyState.cpp` | one line each: the selftest records; `run=`/`runstate=` on `state-end` |
| `forever/docs/TB-COMMANDS.md` | section 9 |

Nothing outside `forever/` changed: the Turtle gates are not needed. `ai/`, `runtime/`, `forever/src/compat`, `forever/src/slice` untouched.

## 2. The tables and how the SQL reaches the database

Seven tables: `ai_dungeon_plan` (plan, map, cc_priority, cc_always, kill_first, cc_never, source, comment), `ai_dungeon_leg`, `ai_dungeon_step`, `ai_dungeon_leg_need`, `ai_dungeon_boss`, `ai_boss_tactic` (19 loader columns + comment) and the new `ai_dungeon_step_unit` (plan, leg, seq, idx, entry, x, y, z, role, comment; PK plan, leg, seq, idx).
The final columns are the Turtle migrations replayed in order (`data/sql/world/20261014150000`, `20261015200000`, `20261105180000`, `20261021114931`, `20261014120000`, `20261102210000`..`20261113200000`, `20261026130000`); the column lists were checked against the two loaders' SELECTs (`DungeonPlanMgr.cpp:45-220`, `BossTacticsMgr.cpp:27`). Charset utf8mb4 / utf8mb4_unicode_ci like the world database (read on forever-test1: MySQL 8.0.46, schema default utf8mb4_unicode_ci).

**Delivery (the spec's D2, read in the core at the pin):** the updater takes its directories from the `updates_include` rows of the `world` database (`UpdateFetcher.cpp:98` query, `:107` `$` = SourceDirectory, `:114` a missing directory is skipped with a warning), every `.sql` file recursively (`:73`), in file name order, names unique across directories (`:83` fatal on a duplicate); a file is recorded by name + hash and applied again when its hash changes (`:326-331`); `DBUpdater<T>::Update` is `DBUpdater.cpp:304`, `ApplyFile` (the fork's own-connection runner with a marker statement) `:421`.
Test server (read-only SELECT 2026-11-19): `updates_include` = `$/sql/custom/world`, `$/sql/updates/world` (RELEASED) and the ARCHIVED `$/sql/old/*`; none of the `ai_*` tables exists yet. So the setup copies `forever/sql/world/` to `~/forever-run/sqlsrc/sql/tortoisebots/world/`, inserts ONE row `('$/sql/tortoisebots/world','RELEASED')` into `updates_include`, restarts. A directory outside `/custom/` is outside the fork's re-apply chain (`:232`). A module file must end in a statement and a newline (the fork appends its marker). The slice-4 trainer file is in the same folder and is applied by the same row.
Not shipped here: the Ragefire Chasm plan ROWS (plan/leg/step/step_unit). They are the exporter's output (`plan-export`, `--forever` from A5), "no row is set by hand": the merge package drops `2026_11_20_02_tortoisebots_plan_ragefire_chasm.sql` (or the exporter's own name) into the folder. Without them `.tb plan list` is empty-but-loaded (`plans=0`).

## 3. Commands and records

`.tb plan list | <plan> info | <plan> check <character> | reload [#token]`, all through `Enc/Rec/Reply`, a bad token refused first (`RequireBuilt`), markers `plan-begin`/`plan-end` with a `verb=` on the end line (addendum A2; the spec's `check-end` yields to it). Full reference with samples: `TB-COMMANDS.md` section 9.

| Reply | Records (keys) | End line |
|---|---|---|
| list | `plan` (plan map legs steps [units]) | `verb=list plans=` |
| info | `plan` (plan map legs steps moves pulls bosses [units] cc_priority kill_first [source]), `leg` (leg name steps) | `verb=info plan= [skipped=]` |
| check | `unit` (leg seq idx entry role result=matched\|near\|missing, [yd spawn alive] or [optional=yes]) | `verb=check plan= bot= map= inst= units= matched= near= missing= optional= steps= steps_ok= steps_short=` |
| reload | none | `verb=reload swapped= [why=] [changes=] plans= steps= [units= units_refused=]` |
| errors | `error code= cmd=plan` | `missing-argument missing-verb unknown-verb unexpected-argument not-loaded no-plan no-bot not-allowed wrong-map no-units` |

Decisions made where the spec was silent (all in section 9 of TB-COMMANDS.md): `skipped=` is computed as the plan's rows in the database minus the rows the table holds (the loader keeps no counts; absent when a count could not be read); the distance of `check` is 3D between the row and `CreatureData::spawnPoint` (so it does not depend on loaded creatures); `optional=yes` marks a missing unit whose entry has no spawn on the map (D4); a step is ok when every non-optional member is matched or near AND at least one member was found; the match is one-to-one greedy over all pairs within 15 yd (nearest first, ties by leg/seq/idx then spawn id); `no-units` refuses a check of a plan with no unit rows instead of answering a green zero; a `check` needs a host bot or the sender's own character (`MayLeadBots`, C3).
`.tb state`: `state-end` gains `run=<lead name> runstate=<state>` for a party whose leader has a run (absent otherwise).

## 4. The run header

`TbRun.h`: `RunRec { leader, leaderName, planName, DungeonRun::Run run, startedMs, endedMs, pauses, deaths, wipes, eventSeq, stopped, stopReason, deque<RunEvent> events (cap 200), map<entry,bool> bossDead }` keyed by the leader's guid (`Find`, `Create`, `Remove`, `All`, `PlansInUse`, `AddEvent`). `RunRecord` writes `run lead plan state step steps [leg legname kind] phase note pull pulls boss deaths wipes pauses elapsed [why] [ev]`; `EventRecord` writes `event seq t kind [step] [leg] [unit] why`.
Values follow the grammar: an unknown number is an omitted key (`leg`/`legname`/`kind` past the last step, `ev` with no event, `unit`/`step`/`leg` of an event that has none), `why=` exactly when paused (the run's own note) or stopped. `pull` counts the pull steps the run passed that it did not record as skipped, deferred or given up (`Run::notCleared`, `unreachableSkips`, `noPathSkips`, `deferredPulls`, `deferGivenUp`). `boss=` lists only the bosses the glue has read (`bossDead`): a boss is never reported alive by default.
Nothing creates or ticks a run in S5: the selftest builds a made-up plan and run (`.tb grammar selftest` prints one `plan`, `leg`, `unit`, `unit` (optional) and `run` + `event` record from the real writers).

## 5. Needs from P5 (loader port)

1. The three declarations of `ai/playerbot/DungeonPlanMgr.h` (`GetDungeonPlans`, `LoadDungeonPlans`, `ReloadDungeonPlans` and `PlanReloadReport`) behind the real `DungeonPlanMgr.cpp`; `DungeonPlanTable` as in `ai/playerbot/DungeonPlanPolicy.h` (`Sealed`, `Find`, `All`, `PlanCount`, `StepCount`) and `PlanReloadOutcome` (`swapped`, `reason`, `changes`) as in `DungeonPlanReloadPolicy.h`. This package uses nothing else of the loader.
2. `forever/src/slice/SliceLinkStubs.cpp` defines stand-ins for `ai::GetDungeonPlans` (empty unsealed table) and `ai::LoadDungeonPlans` (`NoteBoss()` only): they exist today, so the ON build links and `.tb plan` answers `not-loaded`. P5 must remove those two (and the `BossTacticsMgr` ones it replaces) when `DungeonPlanMgr.cpp` joins `forever/SLICE5.txt`, else a duplicate symbol.
3. `forever/src/host/run/TbPlanLoaderStandIn.cpp` (weak `ai::ReloadDungeonPlans`, refuses with a reason) is deleted by the merge package; as it is weak, P5's strong definition replaces it even before that.
4. `TbScripts.cpp` calls `run::LoadPlans()` (= `ai::LoadDungeonPlans(); ai::LoadDungeonStepUnits();`) in `OnWorldInitialized`: P5 must NOT add its own call (a second `LoadDungeonPlans` does nothing by design, but `LoadDungeonStepUnits` would read twice).
5. If P5's loader keeps per-load counts, an accessor for them would replace the database counts behind `skipped=` (optional).

## 6. What the merge package must check live first

1. Apply the SQL (section 2), restart: boot lines `>> Loaded 1 dungeon plans, 84 steps, 4 dungeon bosses (0 plans, 0 steps and 0 boss rows skipped)` (RFC export of 2026-11-19: 9 legs, 84 steps) and `>> Loaded <n> dungeon step units for 1 plans (0 rows refused)`; no `Table ... doesn't exist` error from `ai_dungeon_step_unit` or `ai_boss_tactic`.
2. `.tb grammar selftest #g`: the new `plan`/`leg`/`unit`/`run`/`event` lines match TB-COMMANDS.md section 9; `line_too_long` stays 0.
3. `.tb plan list`, `.tb plan ragefire_chasm info`: agree with the boot line; `skipped=0`.
4. After `.tb enter party` (slice 4): `.tb plan ragefire_chasm check Mtank`: expected from the spec (live S5-c) 85 matched within 5 yd, 1 near (spawn 20048642, 8.6 yd), 7 missing = entry 8996 (all `optional=yes`), 4 of 4 bosses matched, `steps_short=0`. Time the command (one pass over the creature data store).
5. `.tb plan ragefire_chasm check Mtank` from the open world: `error code=wrong-map`; a made-up plan name: `no-plan`; with the unit rows deleted: `no-units`.
6. `.tb plan reload`: `swapped=yes` with no row changed; with one broken step row (edit a copy, never the live data): `swapped=no why=...` and the old plans stay.
7. `.tb state party` of a party with no run: no `run=` key (nothing creates a run before S6).

## 7. Not done / limits (said, not hidden)
* The RFC plan rows and the unit rows are not in this folder (exporter output, section 2).
* The check reads the world database's creature data (the same store `.tb instance info` counts), not the instance's loaded creatures: it proves the plan can be matched to spawns, not that a creature is loaded or alive beyond the respawn-time rule.
* The `run` record's `pull`/`boss` need the S6 glue to be meaningful: today only the selftest sets them.
* `TbRunHeaders.cpp` parses the three headers in both builds and uses one fact of the run header; it does not run the standalone policy tests (those are P5's / R0's gate).

## 8. Gates (final tree)
The commits after the last code commit (`H5: docs, sql readme, loader stand-in reduced to the reload`) are documentation only.
| Gate | Result |
|---|---|
| option-ON `worldserver` (slot 2, `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0`, links; `nm`: `TortoiseBots::run::HandlePlan`, `run::SelfTestRecords`, `ai::LoadDungeonStepUnits` present; no warning in the files of this package |
| option-OFF module build (`FOREVER_SLOT=2 build.sh 12 tortoisebots`, `-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0 MAKE_RC=0`; `libtortoisebots.a` holds the 8 slice-4 objects plus `TbPlanCommands TbPlanLoaderStandIn TbPlanUnits TbRun TbRunHeaders` (the last four compile to empty objects but TbRunHeaders, which parses the three policy headers) |
| census SLICE3 / SLICE2 / SLICE1 (`FOREVER_SLOT=2`) | `files 170 / 160 / 133  compile clean = files  error places 0`, includes of a missing header 0 (this package adds no legacy file) |
| generator `--check`, registry check | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=207` |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed |
| server / SQL | none started; none applied; read-only SELECTs on forever-test1 only (`updates_include`, `ai_%` tables, creature spawns of map 389: 132, entry 8996: 0, all `spawnDifficulties` = 1) |
