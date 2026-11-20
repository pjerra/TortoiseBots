# Dashboard round 1: Dungeons, Dungeon runs, Commander, Raids, Raid coverage on ForeverCore

Branch `dash/runs` (from `tbf/main` feab6c882). Spec: `~/TortoiseBots-audit/FOREVER-DASH-RUNS-SPEC.md` with its lead decisions (D1 to D5). Zero core changes; no database call in the module code.

## What changed against the spec (read first)

The roster sender (`forever/src/TbDashboard.cpp`, the team member's) has grown since the spec was measured: on `tbf/main` it already sends
**DUNGEON_MOBS** (every dungeon or raid copy with bots, members with targets, the `raid` flag from `Map::IsRaid()`, nearby creatures with hp, victim,
reaction, level, rank and cast, capped at 60 in an 80 yd range) and **RAIDS** (raid groups with leader and subgroups) and COMBAT_FIGHT. So:

* **Dungeons: nothing to add.** We send no second DUNGEON_MOBS: the store keeps one entry per (session, seq) and a second sender would fight his. The Go fixture test `TestForeverRaidCopyMovesToRaids` pins that his format splits the copies between the Dungeons and Raids tabs.
* **Raids**: his RAIDS gives the group roster; the raid logic is not ported. The notice says exactly that (see below), it does not claim "no raids".
* The Commanders menu item is hidden by the page unless SERVER_INFO `bots.remote_commander == "1"` (his SERVER_INFO has no such key). `app.js` now shows the tab always on `-core forever` (`applyCommanderTabFlag`: `isForever || ...`). No change to his file.

## Per tab: what shows

| Tab | Source | Shows |
|---|---|---|
| Dungeons | his BOT_BATCH + DUNGEON_MOBS | copies with their bots, mobs, member targets; raid copies go to Raids |
| Dungeon runs | ours: `<run>.summary.json` per finished run | run id, plan, map and dungeon name, started/ended/duration, status, outcome (cleared / stopped / wiped_out / partial), stop reason, bosses (entry, name, `killed_at_s:null`, extra `killed`), wipe and death counts, party (class, role, level), driver `human`, build `forever-port / core <rev>` |
| Commander | ours: COMMANDERS | one binding per run: commander (the leader's `.tb master` commander, else the leader), group, party with role/hp/power/position, `bound_at`, run plan/state/step/steps/leg/kind/phase/label/note/changed, `plan_steps` (the current leg's window, at most 20), `notes` (last 30 events), `orders:[]`, no mobs/objects |
| Raids | his RAIDS | roster of raid groups; notice: raids are not ported (no fight, boss, layout) |
| Raid coverage | Go reads `ai_boss_tactic` etc. as before | rows are data only; notice says every boss is a gap because raids are not ported |

A finished run stays on the Commander tab 30 s (state `done` / `stopped`), then drops out; the first cycle with no binding sends ONE empty list.

## Message formats

* COMMANDERS: unchanged Turtle v12, built by `runtime/CommanderTelemetry.h` `BuildPayloadParts` (standard library only; compiles in the module flags: U2 resolved). Golden string: `forever/tools/dash_runs_check.cpp` (`kGolden`), same bytes as `tools/observability/internal/state/testdata/forever_commanders.json`. `seq` is our own counter (the dashboard keeps COMMANDERS' session and seq apart from the roster's).
* Summary file: keys read by `internal/dungeons/run.go`: `run, mode, plan, map, dungeon{map}, build{module,core}, driver, started, ended, duration_ms, status, outcome, stop_reason, bosses[{entry,name,killed_at_s:null,killed}], wipes (count), deaths (count), party[{class,role,level}]`. File `<plan>-<startedUnix>-<leaderGuid>.summary.json`, written to a `.tmp` and renamed, once per (leader, startedMs). Fixture: `internal/dungeons/testdata/forever/`. U3 resolved: the index takes it (`TestForeverSummaryIsIndexed`).
* Outcome rule (`Outcome` in `TbDashRunsPure.h`): running while the run goes on; stopped with a stop reason naming "wipe" is `wiped_out`, other stops `stopped`; a Done run is `cleared` only when EVERY plan boss was read dead, else `partial`.
* `GET /api/v1/core`: `{"core":"forever","ported":{"raids":false,"coverage":false,"commander_orders":false,"combat":false}}`; Turtle answers all true. `combat` means the commander card's live combat window (his COMBAT_FIGHT feed is separate).

## Files

New: `forever/src/dash/TbDashRunsPure.h` (plain data, Turtle builders, outcome, summary, gate, ledger, role), `TbDashRuns.{h,cpp}` (collector; SLICE1 only, stubs otherwise), `TbDashLink.{h,cpp}` (local sender shim), `forever/tools/dash_runs_check.cpp`, `forever/tools/test_dash_runs_wiring.py`, `cmd/server/forevertabs{,_test}.go`, `internal/plans/plans_tc_test.go`, `internal/state/forever_runs_test.go`, `internal/dungeons/forever_test.go`, `internal/commandersjs/core_notice_ui_check.js`.
Edited (shared): `forever/src/TbScripts.cpp` (3 one-liners: `DashRuns::LoadConfig()` at both config loads, `DashRuns::Update(diff)` after `run::Update`), `forever/conf/tortoisebots.conf.dist` (`Dashboard.Runs = 1`, `Dashboard.RunSummaryDir = ""`), `internal/plans/plans.go`, `cmd/server/main.go` (one route), `web/index.html`, `web/static/js/app.js` + `commanders.js`. The CMake glob picks the new sources up (no list edit). `TbDashboard.*` and the Go auth files are untouched.

## The header change asked of the team member (D2)

Until it lands, `TbDashLink.cpp` is a local shim (own socket from the same `Dashboard.Enable/Host/Port`, session taken at load, own seq). In `forever/src/TbDashboard.h`, namespace `TortoiseBots::Dashboard`, add:

```cpp
uint64 Session();                          // Sender.session
uint64 NextSeq();                          // ++Sender.seq (the roster cycle's counter)
void Send(std::string const& datagram);    // the existing Send()
bool Active();                             // enabled && socket
```

with four one-line definitions in `TbDashboard.cpp`. Then the four functions of `TbDashLink.cpp` (`Session`, `NextSeq`, `Send`, `Active`) forward to them and `Load()` becomes empty. Not needed for correctness today.

## Plans overlay (U4)

`internal/plans/plans.go`: the creature reads name Turtle columns (`level_max`, `detection_range`, `rank`, `movement_type`). On a MySQL 1054 the source retries with the TC spelling (`Classification`, `creature.MovementType`; level and detection 0 = unknown, so no aggro ring is drawn) and remembers the schema, so later reads are one query. `TestTCSchemaFallback` pins the queries a fake driver sees. Not yet seen against the real VM database (live pending, see below).

## Known limits and later

* Boss kill times and wipe/death positions need `RunRec` fields (D5, run porters). The summary carries a `killed` flag the Go side does not read yet.
* COMMANDER_POS, a capped mob scan for the card, orders, view, layout, fight debugger: wait for the Commander.cpp port.
* A run's `changed` is the time of its newest event (the run keeps no per-field change time).
* Role comes from `BotMgr::DescribeBrain` (cached 10 s per bot), the same read the roster does; empty while the engine is off.
* `driver` is `human` (no config key; the spec added none).

## Live check (pending: both test servers were taken by other sessions)

Done instead, on the laptop: the Go daemon built from this branch (`-core forever -dev-no-auth -trace-dir <dir>`) took the golden COMMANDERS datagram, a roster batch and a DUNGEON_MOBS of the roster sender's format over UDP: `/api/v1/commanders` answered one fresh binding (Mcmd, lead Mtank, 3 bots, ragefire_chasm running 3/6, leg entrance, 3 plan_steps, 4 notes, `orders:[]`), `/api/v1/dungeons` the copy `389:3` with 3 bots, `/api/v1/raids` the copy `409:13` (roster only, `live:false`), `/api/v1/dungeon-runs` the fixture summary (Ragefire Chasm, cleared, human), `/api/v1/core` the not-ported answer, and the served page carries the three notice boxes.

Procedure for the real server (the ON image is saved on the VM as `~/forever-build/worldserver-dashruns-on`, built from commit "sObjectMgr is a reference..." of this branch; rebuild slot 6 for the last commit):

1. Stop the world, back up `bin/worldserver`, copy the ON binary in (RUN2 notes), set `TortoiseBots.Dashboard.RunSummaryDir = "/home/pk/dash-runs-traces"` in `etc/worldserver.conf.d/tortoisebots.conf`, start.
2. `TBDASH_TRACE_DIR=/home/pk/dash-runs-traces HTTP_HOST=127.0.0.1 tools/observability/run-dashboard-forever.sh` (binary built with `go build ./cmd/server`; DB port of the test server, `-data-dir` = the LogsDir of the world), then `ssh -L 8095:127.0.0.1:8095 yulon-ubuntu2`.
3. `.tb run start` on the S6 Ragefire Chasm party; check `/api/v1/commanders` (state running, step advancing), `/api/v1/dungeons` (`389:<inst>`, 5 bots), `/api/v1/raids` (empty, notice shown), `/api/v1/plans/ragefire_chasm` (TC fallback; U4 predicted a 1054 on the old query).
4. `.tb run stop` (or let it finish): one file in the trace dir, `/api/v1/dungeon-runs` lists it with outcome and counts, and the Commander card shows `stopped` for 30 s and then the empty list clears it.
5. `.tb run status all`: `tick_avg_us` unchanged; the log prints every 60 s "dashboard runs feed N cycles ... us each on average" (expect well under 200 us).

## Gates (build VM, slot 6)

The module was synced into `src-6/src/modules/TortoiseBots` from this worktree (the same rsync `vm-census.sh` does) before the builds.

* **Option ON**: `FOREVER_SLOT=6 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON ~/forever-build/build.sh 12 tortoisebots` -> `CMAKE_RC=0`, `MAKE_RC=0` (198 ninja steps; `TbDashRuns.cpp.o` and `libtortoisebots.a` written; no `error:` in `build-6/make.log`).
* **Option OFF**: `FOREVER_SLOT=6 ~/forever-build/build.sh 12 tortoisebots` (the default is OFF) -> `CMAKE_RC=0`, `MAKE_RC=0` (10 steps; the two dash objects are the stubs: 760 and 1312 bytes).
* **Generator**: `python3 forever/tools/gen_aiplayerbot_conf.py --check` -> `aiplayerbot.conf.dist matches (696 keys)`. The file had been stale since R9 moved `PlayerbotAIConfig.cpp` lines (the `--check` failed on the branch base); regenerating changed only the `PlayerbotAIConfig.cpp:<line>` comments in its header lines (475 lines, no key or value), committed separately.
* **Registry**: `python3 forever/tools/slice_registry_check.py --quiet` -> `(a)=0` (17 strategies, 48 actions, 75 triggers, 170 values registered; the 215 unregistered (b) keys are the known later-package list).
* **Wiring**: `python3 -m unittest forever/tools/test_dash_runs_wiring.py` -> 8 tests, OK. No other `tools/*wiring*.py` names a file this branch changed.
* **Go**: `flock /tmp/tb-test.lock nice -n 10 go test ./...` in `tools/observability` -> `ok` for every package, including `cmd/server` (`/api/v1/core`), `internal/state` (COMMANDERS fixture), `internal/dungeons` (summary fixture), `internal/plans` (TC fallback), `internal/commandersjs` (notices).
* **Census**: `FOREVER_SLOT=6 FOREVER_CENSUS_LIST=forever/SLICE5.txt .../census.sh 14` -> `files 176  compile clean 176  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`. `SLICE5.txt` is a superset of `SLICE4..SLICE1.txt` (checked: each smaller list minus the next is empty), so this one run is the "census 0 for SLICE5..1" gate.
* **Pure check**: `clang++ -std=c++17 -Wall -Wextra -I<m> forever/tools/dash_runs_check.cpp` in the `forever-builder` image (module tree mounted) -> `RESULT: PASS (dash_runs_check)`, exit 0: the binding mapping, the 30-note and 20-step windows, the outcome rule (a `done` run with an unread boss is `partial`), the golden COMMANDERS bytes, the single empty list, and the one-summary-per-(leader, startedMs) ledger.
