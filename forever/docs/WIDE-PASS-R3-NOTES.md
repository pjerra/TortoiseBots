# Wide mechanical pass R3: five more families, with a new both-core helper pair, and the census script fixed

Branch `port/wide-pass-3` (off `tbf/main` e57bd1849 = `port/forever` + the second pass + the dashboard roster), worktree `~/wt-wide3`, default build slot
(no `FOREVER_SLOT`). Method, rules and scripts as the first two passes (`WIDE-PASS-R1-NOTES.md`, `WIDE-PASS-R2-NOTES.md`); the helpers live ONLY in a new pair,
found through a wrapper, so no shared shim and no file under `forever/src/slice` is edited.

| piece | path |
|---|---|
| ForeverCore side | `forever/src/compat/TbCompat_W3.h` |
| Turtle side (same names and signatures) | `ai/cmangos-compat-w3.h` |
| the wrapper every rewritten file includes (`__has_include("TbCompat_W3.h")`, else the Turtle file) | `ai/playerbot/TbCompatW3.h` |
| the rewrite script (one `--family` per commit, `--check` prints the diff) | `forever/tools/rewrite_wide_pass3.py` |
| files this pass must not touch | `forever/tools/wide3-exclude.txt` (405 paths) |
| census runner (VM side) and its order test | `forever/tools/census.sh`, `forever/tools/test_vm_census_order.py` |

Excluded: every file in `forever/SLICE*.txt` of `tbf/main`, `tbf/run/s6-integ`, `tbf/s8/h` and `tbf/s9/l` (202 files; slices 1-5, 8 and 9 and their sub-lists), the `.h`/`.cpp` of the same
base name of each, and `commands/Commander.cpp` with every file named `*Commander*` (`runtime/RemoteCommander*.h`, `runtime/CommanderTelemetry.h`). "Its helpers" is read as that name rule;
a file the Commander port also needs but that is not named so is not excluded (the lead may extend the list). A rewritten file gets `#include "playerbot/TbCompatW3.h"` after the last
include of its leading block (`"../ai/playerbot/TbCompatW3.h"` outside `ai/`, as its neighbours reach `ai/`); a family that only changes a spelling includes nothing.

## Part A: the census script (`forever/tools/vm-census.sh`)

What was wrong: the script uploaded the core and the worktree into `src[-N]` BEFORE it took `census[-N].lock`, and the lock covered only the census itself (the summary was read after
it was released). Two users of one slot therefore overwrote each other's source in the middle of a run, and a run that started while another one was deleting `census[-N]/` (the runner
does `rm -rf census[-N]`) could print a "files 0" summary, or somebody else's.

Now (pinned by `forever/tools/test_vm_census_order.py`, 8 tests, run with fake `ssh`/`rsync`/`docker` and the real `flock`):
1. the script opens ONE ssh session on the VM that takes `census[-N].lock` and `build[-N].lock` (so no ForeverCore build starts on a source being replaced); it waits there while another run holds the slot;
2. only then the core and the worktree are uploaded into `src[-N]`, and the script sends `GO` into the session;
3. the SAME session runs the census (the `census.sh` that came with the uploaded tree; the old `~/forever-build/census.sh` is no longer used by this script and is left alone for other callers) and reads the summary;
   the census is a child of the session and inherits the lock descriptors, so if the script or its ssh connection dies, the census on the VM goes on and keeps the slot locked until it ends (found during this pass:
   an ssh drop under a VM load of 100-300 left an orphan census running while the next run took the freed lock and uploaded over its source; a first version of the fix that only held the lock in a side session had that flaw);
4. a summary that says `files 0` makes the script fail (exit 3); a census that exits nonzero (docker or `port_census.py` failed) fails the run with that status, and one that writes no
   `summary.txt` or no table fails it (exit 1) instead of printing anything; a session that ends before the census reports its end makes it fail (exit 1);
5. the lock is released when the session ends.
Output folder: one per run, `yulon-ubuntu2:~/forever-build/census-runs/<name>/`, `<name>` = `FOREVER_CENSUS_NAME` (checked: letters, digits, `.`, `_`, `-`) or by default
`<worktree>-s<slot>-<list or all>-<utc stamp>-<pid>`; it never collides with a build slot's folders (`census-N` is not written any more). The 120 MB precompiled header of a run is deleted when it ends.
The tests prove: lock held at the moment of both uploads; a run killed in the middle of its census leaves the slot locked until that census ends (the next run uploads only after it); order lock, upload, upload, census start, census end; two runs on one slot do not overlap and each summary names its OWN tree;
two different slots do run at the same time; a passed name is used and a bad name is refused; `files 0` fails; a census that exits nonzero, or writes no summary, is a failed run
(no summary printed, nonzero exit, lock released); the lock is free after the run.
Every census in the tables below ran through this script after the change, so the order and the per-run folders are exercised for real, not only against the fakes.

## Numbers (whole tree, default slot, `forever/tools/vm-census.sh "" 12`)

| step | summary line | change |
|---|---|---|
| baseline, run 1 (`tbf/main` e57bd1849) | `files 649  compile clean 356  with errors 293  error places 5995 (in 312 files)  includes of a header ForeverCore does not have 40` | |
| baseline, run 2 | `files 649  compile clean 356  with errors 293  error places 5995 (in 312 files)  includes of a header ForeverCore does not have 40` | equal |
| after family 1 (spellings) | `files 649  compile clean 357  with errors 292  error places 5951 (in 310 files)  ... 40` | -44 |
| after family 2 (guid text) | `files 649  compile clean 358  with errors 291  error places 5892 (in 309 files)  ... 40` | -59 |
| after family 3 (database reads) | `files 649  compile clean 364  with errors 285  error places 5649 (in 302 files)  ... 40` | -243 |
| after family 4 (manager singletons) | `files 649  compile clean 364  with errors 285  error places 5600 (in 302 files)  ... 40` | -49 |
| after family 5 (quest members) | `files 649  compile clean 364  with errors 285  error places 5533 (in 302 files)  ... 40` | -67 |
| final, twice on the last code commit (`wide3-final1`, `wide3-final2`) | `files 649  compile clean 364  with errors 285  error places 5533 (in 302 files)  includes of a header ForeverCore does not have 40` | equal |

462 places closed in all, 8 files clean. The baseline is 5995, not the 6004 of the last second-pass note: `tbf/main` carries the second pass as it was BEFORE its review (family 3 of that pass,
`GroupIdW2`, is still in `runtime/BotLfgService.cpp` (8 sites) and `runtime/HireProvisionService.cpp:1485` (1 site)); the review's revert of those nine sites is only on `tbf/port/wide-pass-2`.
This pass did not touch them (the lead decides, below).

## Family 1: spellings, no helper (47 sites, 20 files)

* `event.GetSource()` -> `event.getSource()` (35 sites; also `moveEvent.` and `GetLastEvent().`): `ai::Event`'s member is `getSource()` (`ai/playerbot/strategy/Event.h:40`). The Turtle shim
  (`ai/cmangos-compat-shim.h:1042`) has `#define GetSource getSource` so the old spelling compiled there; ForeverCore does not have the macro (and its `GroupReference::GetSource` must keep its name).
  After the rewrite the Turtle preprocessor output is the same token as before.
* `->getZoneId()` -> `->GetZoneId()` (12 sites on a `Player`): Turtle shim line 1045 maps `getZoneId` to `GetZoneId`, so the Turtle call is the same token; ForeverCore's is `GetZoneId`
  (`Object.h:332`). `runtime/EnchantCapital.cpp:56` (`sTerrainMgr.getZoneId(...)`) is another receiver and was left (still one census place).
Closed: 44 places (47 sites; three of them were on lines with a second error).

## Family 2: a guid as text, `GuidStringW3` (66 sites, 12 files)

Helper `GuidStringW3(ObjectGuid const&)`. Turtle body: `guid.GetString()`. ForeverCore body: `guid.ToString()`. (The existing `GuidTextCompat` was NOT reused: its Turtle body is `operator<<`, not the original call.)
65 sites are log lines or a display fallback (`HireLifecycle.cpp:317`, the name of a dismissed bot when the name lookup fails); the text of one guid differs between the cores (Turtle: type, name, entry/low;
ForeverCore: `Full: 0x... Type: Player Low: N`), which no log reader depends on. The 66th, `PlayerbotRuntimeFacade.cpp` `TradeKey(bot, master)`, builds an in-memory map key
(`s_tradeDiscounts`, never stored or sent) from the two texts; ForeverCore's text carries the whole guid, so it is one key per pair. The rewrite touches only the lines the census names
for "no member named 'GetString' in 'ObjectGuid'" (`Field::GetString` exists too). Closed: 59 places.

## Family 3: database reads (126 sites, 30 files)

Helpers (existing ones reused, three new):
* `QueryGuardCompat(raw)` (existing, both cores): `std::unique_ptr<QueryResult> r(DB.Query(...))` becomes `auto r = QueryGuardCompat(DB.Query(...))`. Turtle: the same unique_ptr; ForeverCore: the shared
  `QueryResult` itself, so `r->Fetch()`, `r->NextRow()` and `if (r)` work as they were written (this was the 119 "no member named 'Fetch'/'NextRow' in shared_ptr<ResultSet>" and 75 "no matching constructor" places).
  Shapes rewritten: `CharacterDatabase.PQuery` 50, `CharacterDatabase.Query` 9, `WorldDatabase.Query` 10, `WorldDatabase.PQuery` 2, `LoginDatabase.PQuery` 4 (75 declarations).
* printf-format queries (findings item 18, the PQuery trap): the character database goes through the existing `CharacterPQueryCompat`; `DbPQueryW3(db, format, args...)` (new) is the same for the world and login databases.
  Turtle: `db.PQuery(format, args...)`. ForeverCore: formats with `vsnprintf` semantics (`LegacyFormatW3`, string arguments must be `char const*`, a `static_assert` says so) and sends the text with `Query()`.
  A zero-argument call skips the formatter on both sides of `LegacyFormatW3` (a query with a literal `%` has no conversions).
  The first round matched only `std::unique_ptr<QueryResult> x(DB.PQuery(...))`; the second (fix round) also rewrites the shapes that compile on ForeverCore with the arguments dropped:
  `auto x = DB.PQuery(fmt, args)`, `if (auto x = ...)` and `x = ...` (20 sites, 10 files), and `PQuery("%s", text)` becomes `DB.Query(text)` (4 sites: three TravelNode loads and
  one vendor query in `RandomItemMgr.cpp`). No site outside the exclusion list now hands a printf format to ForeverCore's `{}` PQuery (0 found by the scan; `ItemUsageValue.cpp` and
  `Commander.cpp` keep theirs, both excluded).
* `FieldTextW3(field)` (32 sites): Turtle `GetCppString()`, ForeverCore `GetString()`. `FieldIsNullW3(field)` (4): Turtle `IsNULL()`, ForeverCore `IsNull()`.
* `DbEscapeW3(db, text)` (15 sites; character 11, login 2, world 2): Turtle `db.escape_string(text)`, ForeverCore `db.EscapeString(text)`.
Closed: 243 places, 6 files clean. The census has no error in `TbCompat_W3.h` or from a helper's `static_assert`.

NOT done, needs a port: **`PExecute` (35 sites outside the exclude list: character 23, world 11, login 1)**. On ForeverCore it compiles and silently drops its arguments, so a rewrite through a helper would START
real writes against ForeverCore's schema (tables and columns that may not exist). Decision for the lead: route them to the unported list (log once, write nothing) or add a formatting helper like `DbPQueryW3`
once each statement's table is checked. Until then a file that becomes clean in this pass still holds its `PExecute` calls; the scripts say where: `grep -rn PExecute` outside the exclude list. The strategy-store
tables (`ai_playerbot_*`) and the character/world tables the SELECTs read are written for Turtle's schema. A query against a table or column ForeverCore lacks is NOT harmless: `MySQLConnection::_HandleMySQLErrno`
(`ForeverCore/src/server/database/Database/MySQLConnection.cpp:594-606`) sleeps 10 s and calls `ABORT()` on `ER_NO_SUCH_TABLE`, `ER_BAD_FIELD_ERROR` and `ER_PARSE_ERROR`. A file this pass made
compile-clean is compiled, not schema-safe: before a linked path runs on ForeverCore its tables and columns must exist (module SQL) or the call must go to the unported list.

## Family 4: the manager singletons (49 sites, 13 files)

Helpers `AuctionMgrW3()`, `AccountMgrW3()`, `ScriptMgrW3()` (macros, so the call site's own include of the manager is the only one needed). Turtle: `(sAuctionMgr)` etc., the global object
(`extern AuctionHouseMgr sAuctionMgr;`). ForeverCore: `(*sAuctionMgr)`, because `sAuctionMgr` is `AuctionHouseMgr::instance()`, a pointer (`AuctionHouseMgr.h:439`, `AccountMgr.h:105`, `ScriptMgr.h:1563`).
`sAuctionMgr.` 33 (the 34th, `ItemUsageValue.cpp:27`, is in a slice file and stays), `sAccountMgr.` 12, `sScriptMgr.` 4 (the other mentions are comments and `Commander.cpp`). Closed: 49 places, exactly the sites. Many of the
methods called through them do not exist on ForeverCore (`GetAuctionsMap`, `GetAItem`, ...): those are the next errors of the same lines ("Left for later").

## Family 5: quest title and reward arrays (68 sites, 17 files)

Helpers (templates, so no include of `QuestDef.h` is needed): `QuestTitleW3(q)`: Turtle `q->GetTitle()`, ForeverCore `q->GetLogTitle()` (`QuestDef.h:673`, the quest log title);
`QuestRewItemIdsW3(q)`, `QuestRewItemCountsW3(q)`, `QuestRewChoiceItemIdsW3(q)` return the array: Turtle `q->RewItemId` (`uint32[4]`), `RewItemCount`, `RewChoiceItemId` (`uint32[6]`, `QuestDef.h:311-314` of the reference core);
ForeverCore `q->RewardItemId`, `RewardItemCount`, `RewardChoiceItemId` (`std::array<uint32, 4/4/6>`, `QuestDef.h:753-757`, `QUEST_REWARD_ITEM_COUNT 4`, `QUEST_REWARD_CHOICES_COUNT 6`). `quest->RewItemId[i]` becomes
`QuestRewItemIdsW3(quest)[i]`: same index, same element. Closed: 67 places. The rewrite touches only the lines the census names for these four members of `Quest`.

## Needs a port, not faked (the lead decides)

| item | places (non-excluded files) | why it is not mechanical |
|---|---|---|
| `PExecute` | 35 sites | see family 3: writes |
| quest objectives: `ReqItemId`, `ReqItemCount`, `ReqCreatureOrGOId`, `ReqCreatureOrGOCount`, `ReqSpell`, `ReqSourceId`, `IsActive`, `GetType`, `GetMinLevel`, `GetRewOrReqMoney` | about 120 | ForeverCore keeps objectives as a list with a type per entry (`QuestObjective`), not fixed arrays: an index-for-index read needs a defined order and a decision for kills of game objects (negative ids on Turtle) |
| `CreatureTemplate` members (`rank`, `trainer_type`, `level_max`, `entry`, `trainer_id`, `vendor_id`, ...) | 112 | ForeverCore splits the template and `CreatureDifficulty`; trainer and vendor ids are not fields of it |
| DBC entries: `AreaTableEntry` (`Id`, `Name`, `AreaLevel`, `ExploreFlag`), `AreaTriggerEntry`, `SkillLineAbilityEntry`, `SkillLineEntry`, `WorldSafeLocsEntry`, `TaxiNodesEntry`, `TaxiPathEntry`, `TalentEntry` | about 350 | field meanings differ (the findings list: Turtle's area key is a per-map flag, ForeverCore's an area id); each is a judgement |
| `AuctionEntry` (`bidder`, `Id`, `bid`, `buyout`, `itemGuidLow`, `owner`, ...) and `AuctionHouseObject` | 155 | ForeverCore has a different auction object (ids, owner as guid); the auction code (`AhBot*`) is a port of its own |
| `GetNPCIfCanInteractWith(guid, flags)` | 25 | ForeverCore wants a second flag word; mechanical (`UNIT_NPC_FLAG_2_NONE`), left for the next pass because every NPC flag name has to be checked in both enums |
| `ObjectGuid` as a bool (`if (guid)`) | 29 | ForeverCore has no conversion; the site is an expression, not a call: needs `IsEmpty()` at each, by hand |
| update-field reads (`GetUInt32Value`, `HasFlag`) | about 100 | field indexes differ (out of scope as in the first pass) |
| battleground types (`BattleGround`, `GetBattleGround`, `BG_*`, `Position::x/y/z` of the BG static data) | about 270 | battlegrounds are not ported |
| `HeadlessSessionState` and the host adapters' `override` | 39 + 24 | the host's own types; belong to the host slice |
| `sObjectMgr` getters, `Player`, `Guild`, `WorldSession` misc | see the final census tables | one-off names |
| `GroupIdW2` sites of the second pass still on this branch | 9 | see "Numbers": the review's revert is not on `tbf/main`; they and the Commander binding must move together |

## Wiring tests

`tools/test_*wiring*.py` that name a changed file (23 tests, found by file base name): all pass unchanged; no test pinned `GetSource()`, `getZoneId()`, `GetString()`, `unique_ptr<QueryResult>`,
`sAuctionMgr.` or `GetTitle()`. New test: `forever/tools/test_vm_census_order.py` (Part A).

## Gates

| gate | command | result |
|---|---|---|
| whole-tree census, baseline x2 | `FOREVER_CENSUS_NAME=wide3-base1 forever/tools/vm-census.sh "" 12` (and `-base2`) | both `files 649  compile clean 356  with errors 293  error places 5995 (in 312 files)  includes of a header ForeverCore does not have 40` |
| whole-tree census, after the five families | same | `files 649  compile clean 364  with errors 285  error places 5533 (in 302 files)  includes of a header ForeverCore does not have 40` (`wide3-f5`; the final pair on the last code commit is in the next row) |
| whole-tree census, final x2 | `FOREVER_CENSUS_NAME=wide3-final1 forever/tools/vm-census.sh "" 12` (and `-final2`) | both `files 649  compile clean 364  with errors 285  error places 5533 (in 302 files)  includes of a header ForeverCore does not have 40` — equal line for line, run folders `~/forever-build/census-runs/wide3-final1` and `-final2` |
| slice 4 | `FOREVER_CENSUS_LIST=forever/SLICE4.txt forever/tools/vm-census.sh "" 12` | `files 174  compile clean 174  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`wide3-slice4`, re-run on the final commit) |
| slice 3 | same with SLICE3.txt | `files 170  compile clean 170  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`wide3-slice3`) |
| slice 2 | same with SLICE2.txt | `files 160  compile clean 160  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`wide3-slice2`) |
| slice 1 | same with SLICE1.txt | `files 133  compile clean 133  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`wide3-slice1`) |
| ForeverCore module, slice option OFF | `ssh yulon-ubuntu2 'FOREVER_SLOT= ~/forever-build/build.sh 12 tortoisebots'` | `CMAKE_RC=0 MAKE_RC=0`, `TOTAL_SECONDS=6` (`module sync: 307 changed/new/deleted files`, none of them recompiled: this option does not build the legacy brain files the pass edited, the census is their compile check) |
| wiring tests that name a changed file | `ls tools/test_*wiring*.py \| xargs -P 8` (all 187 re-run on the final commit, stricter than the 41 found by file base name) | all 187 pass; one re-pinned: `test_bot_area_trigger_wiring.py` `sScriptMgr.` -> `ScriptMgrW3().` in the same five expressions (nothing weakened) |
| Turtle build | `vm-build ~/wt-wide3 --wait` | `MAKE_RC=0` at `sha=96ddb8506`, 220 module files changed, `COMPILE_SECONDS=35  TOTAL_SECONDS=40`, job rc=0 |
| rewrite check (all five families) | `forever/tools/rewrite_wide_pass3.py --family <f> --check` (guidstring and quest with `--errors` from the run-1 `errors.tsv`) | 0 rewrites: every site of every family is in the tree |
| Turtle tests | `vm-test tb ~/wt-wide3` | `RESULT: PASS  kind=tb wt=wt-wide3 sha=96ddb8506 dirty=0 files=3796`, `rc=0  vm_duration=1442s` (2659 unittest tests, 61 skipped; `ALL TORTOISEBOTS CHECKS PASSED CLEANLY`). The first two runs found what the families broke and are fixed in this branch: 4 unit tests that compile or pin production text (`hire_deferred_dismissal_fixture.inc` and `test_raid_strategy_restore.py` got a double of the Turtle body of `GuidStringW3` / `DbEscapeW3`; `test_guild_order_status_honesty.py` and `test_guild_order_store_columns.py` pin `FieldTextW3(fields[...])` where they pinned `fields[...].GetCppString()`), and `runtime/CombatMonitorService.cpp`, which `tools/verify_all.sh` compiles with a test seam that has no `ai/` on its include path: the wrapper is now included only in the branch that has the world database |
| helper files | `git diff --stat e57bd1849 HEAD -- forever/src/compat ai/cmangos-compat-w3.h ai/playerbot/TbCompatW3.h` | only the three new files (124 added lines); the pass's diff has nothing under `forever/src/slice`, no slice-listed file and no `*Commander*` file |

Live tests on the VM: none were made in this pass (the Turtle gates are a compile and the unit/wiring suites; they ran while the VM's `forever-world` containers were up and touch neither).

## Fix round (two reviews)

### Codex: a failed census was masked as a successful run (`forever/tools/vm-census.sh:31-36`)

**Fix.** The remote session no longer pipes `census.sh` into `tail`: it runs the census into `~/forever-build/census-runs/<name>.census.log`, keeps `census.sh`'s own status, prints the log's
last three lines, and fails with that status when it is nonzero (`vm-census.sh:31-33`). It then requires `summary.txt` to be non-empty and `where.tsv`, `names.tsv`, `kinds.tsv` to exist before
printing a summary (`:34-35`); a missing one fails with exit 1. `forever/tools/census.sh:17-19` also refuses a run that wrote no summary (exit 3). The local side captures the coproc
descriptors (`SIN`/`SOUT`) so a session that has already ended is an error, not an `unbound variable`.
**Test.** `forever/tools/test_vm_census_order.py` 8 -> 10: fake docker exits 7 without a summary -> the run exits 7, prints `the census failed`, prints no `compile clean`, and the lock is free;
fake docker exits 0 without a summary -> the run fails too.

### Gemini 1: DB queries do not check table existence (missing tables abort ForeverCore)

**Fix (documentation).** The claim was false and is corrected in this file (Family 3, the `PExecute` paragraph): `MySQLConnection.cpp:594-606` sleeps 10 s and `ABORT()`s on
`ER_NO_SUCH_TABLE`, `ER_BAD_FIELD_ERROR` and `ER_PARSE_ERROR`, so a compile-clean file is not schema-safe.
**Reject (code).** A table/column guard needs a per-table decision (create module SQL, skip the feature, or route to the unported list) — a port of its own, not a call-site rewrite; this pass
never executes those queries and nothing in it added a new query. The risk is recorded above for the lead with the affected files in the review.

### Gemini 2: `auto` / `if (auto ...)` printf `PQuery` calls were missed

**Fix.** Confirmed: those shapes compile on ForeverCore (`PQuery` takes an fmt format string; extra arguments are accepted and dropped, which the census could not see). `rewrite_wide_pass3.py`
now rewrites them through the same helpers (20 sites, 10 files) and `PQuery("%s", text)` to `DB.Query(text)` (4 sites); a scan outside the exclusion list finds 0 remaining.
**Test.** `forever/tools/test_rewrite_wide_pass3.py` (10 tests; 7 failed before the change) pins the five shapes, multi-line literals with nested parentheses, `%%`, a query without a conversion,
and a commented-out call. `ItemUsageValue.cpp` (3 sites) and `Commander.cpp` keep theirs: both are excluded (slice-listed / the Commander port), so the claim is scoped to the list.

### Gemini 3: zero-argument `DbPQueryW3` went through printf

**Fix.** `forever/src/compat/TbCompat_W3.h:36-45`: `LegacyFormatW3` returns the text unchanged with zero arguments, and `DbPQueryW3` sends it with `Query(format)` — a query with a literal `%`
(`LIKE '100%'`) is no longer read as a printf conversion.
**Test.** `tools/test_w3_zero_argument_query.cpp` (compiled and run by `tools/verify_all.sh`): it fails on the old header (`sent` empty) and passes now.

### Gemini 4: `vm-census.sh` masks failures

Same finding as Codex, fixed once (above): nonzero census status, required summary and tables, tests for both.
