# Slice S5, package M5: merge of P5 and H5, the Ragefire Chasm rows, live checks

Branch `run/s5-integ` (worktree `~/wt-run-s5m`), build slot 1, off `port/forever` (slices 1-4 and the wide pass, 55dff9543). Core read only (pin 6517af05), zero core changes. Test server: the build VM's own `~/forever-run`, driver `~/forever-build/s5m-sess.sh` (m4's, binary `~/forever-build/s5m-on/worldserver`); `forever-test1` never touched. Two boots (logs `~/forever-build/s5m-boot1.log`, `s5m-boot2.log` on the VM); `grep -c 'ASSERT\|ABORT\|Segmentation'` = 0 on both.

## 1. The merge
`git merge --no-ff tbf/run/p5` then `tbf/run/h5`: **no conflicts in any file** (H5 was built on slice 4's host half and touches other lines than slice 4's final fixes). Needs done in one commit (`S5 merge: shims include ...`):
* `forever/src/compat/forever-compat-shim.h` includes `TbCompat_S5.h` (after `TbCompat_D4.h`), `ai/cmangos-compat-shim.h` includes `cmangos-compat-s5.h` (after `-d4.h`); the interim forwarder `forever/src/compat/cmangos-compat-s5.h` and the two includes in `DungeonPlanMgr.cpp` / `BossTacticsMgr.cpp` are gone. The pin in `tools/test_reload_plans_wiring.py` now says the loaders do NOT include the pair themselves, and a new test says both shims do.
* `SliceLinkStubs.cpp`: the four stand-ins (GetDungeonPlans, LoadDungeonPlans, GetBossTactics, LoadBossTactics) removed; `LoadBossStands` stays.
* `forever/SLICE5.txt` = SLICE4 body + `DungeonPlanMgr.cpp`, `BossTacticsMgr.cpp` (176 lines, 176 files); `CMakeLists.txt` `TB_SLICE_LIST` and its error text point at it; `slice_registry_check.py` closes the keys against SLICE5.txt too.
* H5's `TbPlanLoaderStandIn.cpp` (weak reload) deleted; P5's strong `ReloadDungeonPlans` is the one. One load call only: H5's `run::LoadPlans()` (`LoadDungeonPlans` then `LoadDungeonStepUnits`); P5 added none.
* Generated: `slice_gen_context.py --check` up to date; registry check `(a)=0 (b)=215`.

## 2. The Ragefire Chasm rows
Exporter: external client repo `~/forever-agent` main, commit **63fc3f78**. Command (the mmaps source is the build VM's `~/forever-run/data/mmaps`: only `0389.mmap` and the four `0389_*.mmtile` were copied to a scratch folder, read only at the source):
`go run ./cmd/forever-agent plan-export --forever --mmaps <scratch> --out <scratch> ragefire_chasm` -> `plan_ragefire_chasm.sql`, shipped as `forever/sql/world/2026_11_20_02_tortoisebots_plan_ragefire_chasm.sql` (named by the module's convention, next to H5's `_00_` tables and `_01_` bosses; README updated). Bytes are the exporter's own; nothing edited.
`--report`: `plan=ragefire_chasm map=389 status=exported legs=9 steps=80 pulls=51 moves=25 bosses=4 units=103 boss_rows=4`. Dropped lines, all as PLAN-EXPORT-NOTES.md lists them: entrance/trigger/tele/portal, level_band, tank_min_hp, hire_composition, fleers, notes (plan and 9 legs), `patrol.path/period_s/source` of 6 pull groups (legs 2 x2, 4, 5, 6, 7), `legs[3].skip` (entry 11834), `boss.tactics` of legs 6 and 7. No `avoid`, no refusals. **80 steps, not 84:** the spec's 84/29 moves was an estimate; the exporter notes give 74 without and 80 with `--mmaps` (6 of the ten long hops are cut), and the golden file says the same. 103 unit rows = 93 pack + 4 boss + 6 patrol. The TB-COMMANDS.md section 9 samples now carry the real output (80 steps, 103 units); H5's own notes keep their 84 as written.
Boot logs a loader warning `ai_dungeon_step: plan 'ragefire_chasm' has 6 moves more than 60 yd from the move before them` (the hops `--mmaps` could not cut; information, not an error).

## 3. Server
Applied the way H5's README says: `forever/sql/world/*.sql` copied to `~/forever-run/sqlsrc/sql/tortoisebots/world/`, one row `('$/sql/tortoisebots/world','RELEASED')` in `updates_include`, restart. Boot 1: `Applying update "2026_11_20_00_tortoisebots_plans.sql"`, `_01_...bosses_rfc`, `_02_...plan_ragefire_chasm`, `Applied 4 queries` (the trainer file re-applied, idempotent). Boot lines, no missing-table error:
`>> Loaded 0 boss tactic rows (0 skipped)`, `>> Loaded 1 dungeon plans, 80 steps, 4 dungeon bosses (0 plans, 0 steps and 0 boss rows skipped)`, `>> Loaded 103 dungeon step units for 1 plans (0 rows refused)`, `engine configuration loaded in 1658 ms`.
Party as slice 4 (Mtank + Mpriest + Mdpsa-c, Mcmd GM not in the group), `.tb enter party Mtank trigger 2230`, `.tb tele Mcmd to Mtank`.

## 4. Live checks
| Gate | Result | Decisive lines |
|---|---|---|
| a selftest | **PASS** | `grammar-end count=30 records=27 line_too_long=1`: slice 4's 24/21 plus the six new records (`plan`, `leg`, `unit`, `unit` optional, `run`, `event`) in the reference format of TB-COMMANDS.md section 9. `line_too_long=1` is the deliberate `freetext` case that slice 4 already had (H5's note says it "stays 0": it stays at the baseline 1). |
| b list / info | **PASS** | `plan plan=ragefire_chasm map=389 legs=9 steps=80 units=103`; `info`: `moves=25 pulls=51 bosses=4 units=103 ... source=forever-agent 63fc3f78 dungeons/ragefire_chasm.json`, nine `leg` records (6+4+12+4+17+9+7+10+11 = 80), end `skipped=0`: agree with the boot line. |
| c check, fresh instance | **PASS after one fix (M5-1)** | `tb enter party` -> `instance 3` (fresh: `mobs_alive=132 mobs_total=132`; the core takes a new id here because 1 and 2 were still stored). `plan-end count=103 records=103 verb=check plan=ragefire_chasm bot=Mtank map=389 inst=3 units=103 matched=95 near=1 missing=7 optional=7 steps=55 steps_ok=55 steps_short=0`. Pack rows: 85 matched + 1 near (leg 4 seq 16, entry 11320, 8.61 yd, spawn 20048642, a waypoint walker) + 7 missing (entry 8996, the pet with no spawn, all `optional=yes`) = the spec's 85/1/7; 4 of 4 bosses matched; 6 patrol rows: all matched after the fix (boot 1 before it: 1 matched, 5 missing, see M5-1). Timing: the command answered in 0.075-0.105 s wall (fifo to last line, 50 ms polling); `.tb perf` window around it: world update avg 1.11 ms, **worst 12 ms** (the window before it: 8 ms), module worst 1.4 ms: it does not stall the world thread. Repeated and after `reload` the same numbers. |
| d errors, reload | **PASS** | open world: `error code=wrong-map ... Mtank is not on map 389`; `no-plan` (`nope`), `no-bot` (`Nobody`), `missing-argument` (`.tb plan`, `check` with no name), `missing-verb`, `unknown-verb` (`frob`), `unexpected-argument` (`list extra`); a copy of the plan (database copy under another name, no live row touched) with no unit rows: `error code=no-units`. `reload` with no change: `swapped=yes changes=0 plans=1 steps=80 units=103`; with the copy carrying one pull step `entries=abc`: log `plan 'rfc_broken' leg 0 seq 1 (pull) skipped (BadEntries)`, `swapped=no why=...BadEntries...`, `plans=1 steps=80` (old tables stay). Copies deleted afterwards. |
| e no `run=` | **PASS** | `.tb state party`: no `run=` / `runstate=` on any record or `state-end` (both boots). |

Recorded, per leg, the units that did not match (before the fix): leg 6 seq 4 and leg 7 seq 3-6 and leg 8 seq 5-6: entry 8996 (7 rows, summoned pet, no spawn on map 389, `optional=yes`); leg 4 seq 16 idx 0: near at 8.61 yd (the patrol spawn 20048642 stands at -176.8,11.7; the row is the plan's pack position); five patrol rows (leg 2 seq 2 and 9, leg 4 seq 12, leg 6 seq 1, leg 7 seq 1): see M5-1. A reload that DROPS a plan from the table (the temporary copy removed again) is refused by the loader's reload policy (`plan 'rfc_nounits' is gone from ai_dungeon_plan`, `swapped=no`): the legacy rule, a restart clears it.

## 5. Bug found and fixed
**M5-1** (`forever/src/host/run/TbPlanUnits.cpp`, `CheckPlanOnMap`): boot 1 gave `matched=90 near=1 missing=12 optional=7 steps_ok=50 steps_short=5`. The five extra missing rows were `role=patrol`: the exporter writes a patrolling pack member twice (the pack row at its position and the patrol row at the path's first point, here 0.01-0.1 yd apart: the SAME creature, e.g. spawn 20048648 for leg 2 seq 2), and one-to-one matching left the second row with no spawn, failing five steps. Fix: a patrol row that found no free spawn takes the spawn that a row of its own step (same leg, seq and entry) already holds, when it stands within 15 yd of the row. It can only turn a missing row into a match when a spawn of that entry stands there; it never reports a creature that is not on the map. Documented in TB-COMMANDS.md section 9 (`check`). Decision for the lead, not for me: the exporter could instead skip the duplicate row; the module fix is safe either way.

## 6. Gates (final commit)
| gate | result |
|---|---|
| census SLICE5 / 4 / 3 / 2 / 1 (slot 1) | files 176 / 174 / 170 / 160 / 133, all compile clean, 0 error places, 0 includes of a missing header |
| option ON `worldserver` | CMAKE_RC=0 MAKE_RC=0, 0 `undefined`; `nm`: `ai::LoadDungeonPlans`, `ai::ReloadDungeonPlans`, `ai::LoadDungeonStepUnits`, `run::HandlePlan` present (one definition each: no duplicate link error) |
| option OFF `tortoisebots` | CMAKE_RC=0 MAKE_RC=0, `libtortoisebots.a` = the 8 slice-4 objects + `TbPlanCommands TbPlanUnits TbRun TbRunHeaders` (the stand-in object is gone) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | up to date; `(a)=0 (b)=215` |
| `test_dungeon_run_policy`, `test_system_run_policy` (g++ -std=c++17 -Wall -Wextra on the VM) | `ok`, `109 checks passed`, no warning |
| python wiring pins (`test_reload_plans_wiring.py` 16, `test_slice4_d4_wiring.py` 10) | OK |
| Turtle `vm-build` (code-final 5c9016038, after the live boots) | `COMPILE_SECONDS=3090 CMAKE_RC=0 MAKE_RC=0` (the VM was at load 30-38 from other workers) |
| Turtle `vm-test tb` (5ab8790ca) | `RESULT: PASS`, rc=0, vm_duration 1140 s |

## 7. What the S6 merge must know
* `LoadDungeonPlans` is called once, from `run::LoadPlans()` in `OnWorldInitialized`; S6 must not add a second load. `GetDungeonPlans()` is sealed after that.
* The plan has **80 steps** (25 moves) with `--mmaps`; any S6 test that says 84 is wrong. Patrol rows share a spawn with the pack row of their own step: S6's pack lookup (`CheckPlanOnMap` cache) can see the same `spawnId` on two rows of one step; they are one creature.
* Entry 8996 (7 rows) is optional by rule D4; it has no spawn. Boss entries 11517, 11518, 11519, 11520 and `GetBossState` ids 2732-2735 (slice 4's `boss=` list) are the same four bosses.
* The core reuses instance ids only after unload: this boot's fresh instance was 3 (1 and 2 stored from earlier tests), so `inst=` still cannot prove "fresh"; `mobs_alive == mobs_total` does.
* Reload refuses to drop a plan from the table (restart needed); a bad row refuses the whole swap and the old tables stay.
* Rows are generated: to change the plan edit `dungeons/ragefire_chasm.json` in forever-agent and export again; never edit the SQL.
