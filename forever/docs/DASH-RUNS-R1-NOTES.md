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
