# Slice 5, package P5 (plan loader port) and round R0: notes

Worker: S5 R0 + P5, branch `run/p5` (off port/forever, slices 1-3 merged), build slot 3 (`FOREVER_SLOT=3`), 2026-11-19.
Spec: `~/TortoiseBots-audit/FOREVER-RUN-S5S6-SPEC.md` section 4.2 and the rounds table. Facts list (R0): `RUN-S5-FACTS-RFC.md`.

## 1. R0

* `forever/SLICE5.txt`: SLICE3.txt plus `ai/playerbot/DungeonPlanMgr.cpp` and `ai/playerbot/BossTacticsMgr.cpp` (176 lines, 172 files after
  comments and the two replaced ones). The slice-4 lines are not in it yet (slice 4 has not merged): see "Needs at merge".
* Slot 3 did not exist: `FOREVER_SLOT=3 ~/forever-build/build.sh 12 tortoisebots` configured it (cmake, `build-3`, ccache made the first module
  build 45 s); `vm-census.sh` then makes `src-3` and `census-3` itself (the census needs the configured `build-3` for `ninja -t compdb`).
* Census baseline, SLICE5.txt, before any change: **files 172, compile clean 170, with errors 2, error places 38** (DungeonPlanMgr.cpp 31,
  BossTacticsMgr.cpp 7; kinds: `no member named GetCppString in Field` 18, `unique_ptr<QueryResult>` 7, `Fetch` 7, `NextRow` 6). After P5:
  **172 clean, 0 error places**.
* The `Facts` list (U1): `RUN-S5-FACTS-RFC.md` (83 of 246 read in the simulated RFC run, 42 more reachable, 30 stall/tick-body, 86 never,
  5 never read in the policy).

## 2. P5: what changed (Turtle side proved per helper)

| file | change |
|---|---|
| `ai/playerbot/DungeonPlanMgr.cpp` | six reads through `WorldQueryCompat`, `FieldTextCompat` at the 18 text fields, the reload's reconciler through `GetSeekFightReconciler()`; the include of `BossTacticsActions.h` (306 census places) is gone |
| `ai/playerbot/BossTacticsMgr.cpp/.h` | one read through `WorldQueryCompat`, `FieldTextCompat`; new `SeekFightReconciler` pointer (`SetSeekFightReconciler` / `GetSeekFightReconciler`) |
| `ai/playerbot/strategy/actions/BossTacticsActions.cpp` | one line: `static bool const ... = (ai::SetSeekFightReconciler(&ai::ReconcileSeekFightState), true);` |
| `ai/cmangos-compat-s5.h` | Turtle side of the pair |
| `forever/src/compat/TbCompat_S5.h` | ForeverCore side of the pair |
| `forever/src/compat/cmangos-compat-s5.h` | interim forwarder to `TbCompat_S5.h` (see "Needs at merge") |
| `tools/test_reload_plans_wiring.py`, `test_plan_step_label_wiring.py`, `test_aura_tactics_wiring.py`, `test_run_leader_wiring.py` | pins re-pinned to the helper spellings, no assertion removed; new class `LoaderPortWiring` (5 tests) |
| `forever/tools/facts_*.py/.cpp/.sh`, `s5_headers_check.cpp` | R0 tracer and the header-compile TU |

| helper | Turtle side (the original expression) | ForeverCore side (core call) |
|---|---|---|
| `WorldQueryCompat(char const* sql)` | `std::unique_ptr<QueryResult>(WorldDatabase.Query(sql))`: the original `std::unique_ptr<QueryResult> result{WorldDatabase.Query(...)}` | `WorldDatabase.Query(sql)` (DatabaseWorkerPool.h:115) returning the shared `QueryResult` (DatabaseEnvFwd.h:28; `Fetch()` QueryResult.h:60, `NextRow()` :56), after the schema guard below |
| `FieldTextCompat(Field&)` | `field.GetCppString()` | `field.GetString()` (Field.h:119, Field.cpp:113; a NULL gives an empty string) |
| `SeekFightReconciler` pointer | registered by `BossTacticsActions.cpp` at startup to `ai::ReconcileSeekFightState`; `ReloadDungeonPlans` wraps it in the `std::function` that `CommitPlanReload` already took (`&ai::ReconcileSeekFightState`): same function, same call | not registered (boss tactics are not in the slice): an empty `std::function`, which `CommitPlanReload` skips (`if (beforeInstall)`, DungeonPlanReloadPolicy.h:302) |

Why a pointer and not the spec's no-op: an empty function is skipped by the commit exactly as a no-op would be, and the loader needs no
definition for the F side to pass. The pointer is constant-initialised (`= nullptr`), so the registration in `BossTacticsActions.cpp`'s
static initialiser cannot be overwritten by `BossTacticsMgr.cpp`'s.

### The one thing the F side does that the T side does not: the schema guard

ForeverCore's connection ABORTS THE SERVER (after a 10 s sleep) on a query naming a missing table or column
(`MySQLConnection.cpp:596-601`, ER_BAD_FIELD_ERROR / ER_NO_SUCH_TABLE). Turtle logs the error and returns null, and the loader relies on it
(`ai_dungeon_leg_need` "an old database without the table"). So `WorldQueryCompat` on F first asks `information_schema.COLUMNS` whether the table
has every backticked column between SELECT and FROM; if not it logs one error line and answers an empty result (the loader's own "no rows" /
"query failed" path), so a world database without the S5 SQL gives "0 plans" and a refused reload, never an abort. Only the single-table
`SELECT ... FROM \`table\`` form is accepted (all seven loader queries); any other form is refused the same way. Cost: one extra query per
call, at boot and `.tb plan reload`. Failure value: the do-less one (nothing loaded).

### Differences to watch (none changes what a bot does)

* A plan or tactic table read on F that returns text with a NULL column gives "" (T: the raw string); the plan columns are NOT NULL.
* `GetUInt8/16/32/64`, `GetFloat` are the same names on both cores; the F `Field` reads text results by parsing (the loader's queries are text
  queries, not prepared ones), `SUM(CRC32())` is cast `AS UNSIGNED` in `TableFingerprintSql`, so `GetUInt64` is right on both.
* With the stand-ins gone (see below) `GetBossTactics()` / `GetDungeonPlans()` return the real, still unsealed tables until the host calls
  `LoadBossTactics()` / `LoadDungeonPlans()`; readers see an empty table before the seal, the same answer the stand-in gave.

## 3. Needs at merge (exact changes in files P5 does not own)

1. **forever/src/compat/forever-compat-shim.h** (section 5, after `#include "TbCompat_S3.h"`, before `TbCompat_Position.h`): add
   `#include "TbCompat_S5.h"          // slice 5: the plan loader (world query, field text)`. **ai/cmangos-compat-shim.h** (after
   `#include "cmangos-compat-s3.h"`): add `#include "cmangos-compat-s5.h"`. Then delete `forever/src/compat/cmangos-compat-s5.h` (the interim
   forwarder) and the two `#include "cmangos-compat-s5.h"` lines in `DungeonPlanMgr.cpp` and `BossTacticsMgr.cpp` (and the pin
   `self.assertIn('#include "cmangos-compat-s5.h"', text, name)` in `tools/test_reload_plans_wiring.py` LoaderPortWiring). Until then the
   two files include the pair themselves (the forwarder comes first on the F include path, `ai/` is on the T path).
2. **forever/src/slice/SliceLinkStubs.cpp** (slice-4 territory: not touched): delete the four stand-ins the real files now define, lines 104-120
   of this tree: `ai::GetBossTactics()`, `ai::GetDungeonPlans()`, `ai::LoadBossTactics()`, `ai::LoadDungeonPlans()` (and the comment above them;
   `LoadBossStands()` between them stays).
   With the option ON they would be duplicate definitions at link time. The census (per file) does not see this; the option-ON link does.
3. **forever/SLICE5.txt**: append the slice-4 lines of `forever/SLICE4.txt` (at this writing `AcceptResurrectAction.cpp`, `ReleaseSpiritAction.cpp`,
   `DeadStrategy.cpp`, `PartyMemberToResurrect.cpp`, each under `ai/playerbot/strategy/...`; take the file as slice 4 merges it), then
   point `TB_SLICE_LIST` in the root `CMakeLists.txt` (line 73) and its error text (line 96) at `forever/SLICE5.txt`.
4. **TbScripts.cpp** (H5): `OnWorldInitialized` needs ONE call, `ai::LoadDungeonPlans()`, after the engine config (it reads the boss tactics
   first, `DungeonPlanMgr.cpp` `LoadDungeonPlans`: `LoadBossTactics()`); only in the slice build (`TORTOISEBOTS_SLICE1`), where the real
   files replace the stand-ins of item 2.
5. **World SQL (H5)**: the loader reads `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`, `ai_dungeon_leg_need`, `ai_dungeon_boss` and
   `ai_boss_tactic` with the exact columns of the SELECTs in `DungeonPlanMgr.cpp:38-130` and `BossTacticsMgr.cpp` (`tools/test_dungeon_plan_sql.py`
   `loader_columns()` is the checker); with the schema guard a missing column means "that table is not read", not an abort.
6. Python wiring pins outside this branch that name the old spellings of these two files must be re-pinned the same way; slice 4's own
   re-pins touch other files.

## 4. Gates (final code commit `63182629f`, later commits are tools and notes only)

See the end of this file (filled in when the runs finished).

## 5. For H5 and the next slice

* `ai::LoadDungeonStepUnits()` (H5) can use `WorldQueryCompat` and `FieldTextCompat` unchanged: the guard also protects its new
  table `ai_dungeon_step_unit` (a missing table gives an empty result, not an abort), but a single-table `SELECT ... FROM \`t\`` only.
* The decision headers need nothing: `forever/tools/s5_headers_check.cpp` includes the three of the spec (plus the plan headers) and is clean on
  ForeverCore (census), and `test_dungeon_run_policy` / `test_system_run_policy` run unchanged on the VM (results below).
* S5-a (boot line) needs the world SQL applied before `LoadDungeonPlans()` runs; with it missing the boot prints `0 plans` and an error line
  per missing table, no abort.
