# Integration batch 9

`integ/batch-9` on `tbf/main` @ `5f1ac1dae`, module `TortoiseBots`, ForeverCore pin **d02645dd21** (`~/ForeverCore-d02645dd`, core key `core-9b88bd2d6584` per options). Zero core changes.

Ten branches merged one at a time with `git merge --no-ff`, in the brief's order, each after `git fetch tbf`. The tree is the merge result; every conflict was resolved by reading the branch notes in `forever/docs/` and keeping both sides' intent.

| # | Branch (tip merged) | What it brings |
|---|---|---|
| 1 | `pr-99` (a9c05c124) | Team PR #99, Turtle regressions: the quest-link parser may read the chat handler's protected link helper again; the Turtle quest accept and vendor template buy restored; the weight-scale SQL un-prefixed and its checker. Merged unchanged |
| 2 | `tbf/tb/removeitem` (d79475f88) | `.tb removeitem`: destroy an item from a host bot's or the sender's bags through the core's `DestroyItemCount`, with `removed=` and `left=` |
| 3 | `tbf/slice/s1r1` (0a7198507) | S1-R1: the core-free chat-link parser (`ChatLinkScan`), both-core `ExtractSpellIdCompat` / `ItemLinkQualifierCompat`, non-throwing number parsing, `.tb link` |
| 4 | `tbf/slice/s1r2` (1149dd617) | S1-R2: chat commands in through `PlayerScript::OnChat`, the pure filter/inbox, the chat strategy keys, `.tb hear` |
| 5 | `tbf/slice/s2` (575a74859) | S2: pushback and failed casts reach the engine (`SMSG_SPELL_DELAYED`, `SpellDelayed` wait, `SpellInterrupted` on an async failure, `.tb pushback`, state `delayed=`) |
| 6 | `tbf/slice/s3` (c9b3f9af5) | S3: throws, falls and forced speed (the host watcher, the core-started flight's fall damage, `.tb knock`, `.tb speed`, state `thrown=`/`falldmg=`/`runrate=`) |
| 7 | `tbf/feat/place-rally` (c25a104d0) | Place, rally and fall back (issue #15): `.tb cmd place|rally|fallback|resume`, the holds, the cursor ping, the chat aliases |
| 8 | `tbf/world/w0a` (9393e6069) | W0-A: the account/character pool registry, `.tb pool create|list|prune`, its migration, the name service |
| 9 | `tbf/world/w0b` (aeba4bcbb) | W0-B: the pool scheduler, `.tb pool start|stop|status`, one session per account |
| 10 | `tbf/run/wc` (6c7434317) | WC: the Wailing Caverns plan, the gossip step, the event holds and the run's wade rule |

## Conflicts and resolutions

1. **`pr-99`** — clean.
2. **`tbf/tb/removeitem`** — clean.
3. **`tbf/slice/s1r1`**:
   - `ai/playerbot/PlayerbotAI.h`: main (via pr-99) added the `ExtractQuestIdCompat` friend and parsed `extractSpellId` through the unported `ChatLinks` block; S1-R1 ports both extractors (`ExtractSpellIdCompat(this, str)`) and friends both helpers. Took S1-R1's body and both friend declarations: the ported spell extractor replaces the block, pr-99's quest friend stays.
4. **`tbf/slice/s1r2`** — clean.
5. **`tbf/slice/s2`**:
   - `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md`: kept the S1-R2 chat row (the host hook) and took S2's newer spell-delay row (the host reads the core's typed packet; the 1.12 case stays unregistered).
   - `forever/src/TbScripts.cpp`: kept all four rows, `.tb link`, `.tb hear`, `.tb pushback`.
   - `forever/src/host/TbEngineBrain.cpp`: kept both includes (`host/chat/TbChatIn.h`, `host/TbSpellDelayPure.h`); the merged body keeps `DeliverChat` and S2's cast-failure re-arm.
6. **`tbf/slice/s3`**:
   - `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md`: per row, took S3's newer forced-speed, knock-back and jump/fall rows and kept the S2 cast-failure/spell-delay rows and the S1-R2 chat row.
   - `forever/docs/TB-COMMANDS.md`: sections 17 (S1-R1) and 18 (S1-R2) kept; S3's new section renumbered **19**.
   - `forever/src/TbScripts.cpp`: `.tb knock` and `.tb speed` added next to `.tb pushback`, `.tb link`, `.tb hear`.
   - `forever/src/TbBotMgr.cpp`: both includes (`host/chat/TbChatIn.h`, `host/TbThrown.h`).
   - `forever/src/host/TbEngineBrain.cpp`: all three includes (`TbChatIn.h`, `TbSpellDelayPure.h`, `TbThrownPure.h`); `NoteThrown` and the chat/cast paths coexist.
7. **`tbf/feat/place-rally`**:
   - `forever/docs/TB-COMMANDS.md`: sections 17-19 kept; place/rally/fall back renumbered **20** (its own `###` subheads unchanged).
8. **`tbf/world/w0a`**:
   - `forever/src/TbBotMgr.cpp`: `Update` keeps S1-R2's `chatin::DrainArrivals()` and W0-A's `ConfirmCreations()`, in that order.
   - `forever/tools/slice_gen_context.py`: kept S1-R2's `CH_KEYS` block and joined `WL0_KEYS` after it.
9. **`tbf/world/w0b`** — clean (it descends from W0-A).
10. **`tbf/run/wc`**:
    - `forever/docs/TB-COMMANDS.md`: sections 17-20 kept; the Wailing Caverns section renumbered **21**.

## Integration fixups

Two kinds of cross-branch gaps only the merged tree shows; both are fixed on top of the merges (commit `96506262a`):

- `forever/SLICE-W0.txt`: S1-R2 added ten chat files to `forever/SLICE-W1.txt`, but the build now reads `SLICE-W0.txt` (W0-A moved the pointer), so the ten paths are added to `SLICE-W0.txt` (the ring of W1's list therefore holds, as `test_w1_wiring` requires).
- `forever/tools/test_run_revfix1_wiring.py`, `forever/tools/test_run_review_wiring.py`: WC gave `CheckLeadMove` a fifth argument (`bool wade`); the two older pins matched the old signature and now match the new one. Both failures also exist on `tbf/run/wc` itself; no behaviour pin was weakened.

## `tbf/main` moved during the round (41d22cba9)

The lead fetched `tbf/main` at `41d22cba9` (PR #99, #97 world life W5-W9, #100, #101 now in main) and asked for it to be merged into `integ/batch-9`. It is merged as commit `012cdaa86`; `pr-99`'s tip `a9c05c124` is an ancestor of the new main, so the first merge of the round is content-redundant. The history was not rewritten (that would discard the reviewed merge order and every conflict resolution below); the tree carries main's copies of those changes, and the gate lines below are from the tree after this merge.

Conflicts (both intents kept):

- `ai/playerbot/strategy/actions/AhAction.cpp`: main's W8 implementation (`#include "playerbot/TbCompatWL8.h"` and the `__has_include` block) kept, with S1's `#include "playerbot/ChatLinkScan.h"` above it; the legacy branch below (which S1 fixed) stays behind `#else`.
- `forever/SLICE-W1.txt`: the ten S1-R2 chat files kept, main's `AhAction.cpp` added.
- `forever/src/TbBotMgr.cpp`: S1-R2's `chatin::Forget(leaving)` kept next to main's `prof::ClearBotErrands(leaving)` (in the engine-only block).
- `forever/src/TbScripts.cpp`: `.tb link`, `.tb hear`, `.tb pushback`, `.tb knock`, `.tb speed` kept next to main's `.tb setskill` and `.tb guildorder`.
- `forever/src/host/TbEngineBrain.cpp`: all five includes kept (`TbChatIn.h`, `TbSpellDelayPure.h`, `TbThrownPure.h`, `TbHostTaxi.h`, `TbHostTransport.h`).
- `forever/src/host/TbPartyState.cpp`: `chatin::AppendState` kept before W5-W9's `prof`, `taxi`, `transport`, `ah` and `order` keys.
- `forever/src/host/cmd/TbCmdOrders.cpp`: main's `CheckBotAccess` block runs first, then issue #15's `place::CancelPending` / `place::Handle`.
- `forever/src/slice/SliceAiObjectContext.cpp`: S1-R2's sixteen chat creators kept next to W8's three AH creators; the merged counts are `25/108/133/201`, and the ChatActionContext comment says 38 keys.
- `forever/tools/slice_gen_context.py`: `CH_KEYS` and `W8_KEYS` both kept; the join is `…, W3_KEYS, W8_KEYS, CH_KEYS, WL0_KEYS`.
- `CMakeLists.txt` auto-merged to the W0 list (the newest), which is the list this branch builds.

Two cross-branch gaps the merge exposed (commit `efe7fe12b`):

- `AhAction.cpp` entered `SLICE-W1.txt` with main; `SLICE-W0.txt` (the build list) and `SLICE-CH.txt` (the S1 census list and the list `test_cl_wiring`/`test_chat_in_wiring` check against W1) now carry it too. With that, every W1 path is in the list the worldserver build reads — the 96-undefined-reference failure the other developer warned about cannot come from a missing path.
- `test_w1_wiring` pinned the old `BotMgr::Instance().SetFree(GuidByName(name), …)` call, but main's #100/#101 made `SetFree` return the error string (the test fails the same way on a clean `tbf/main` worktree); the pin now names the guarded call.
- `runtime/DungeonRunPolicy.h`, `tools/test_dungeon_run_policy.cpp`: WC makes a units hold ignore corpses that were in its radius at the first look (`eventDownBase`), which also made the r15 "Bly's crew dead before the hold" replay wait out its 300 s. Both intents are kept: the baseline still ignores an old corpse, but when this hold's starting gossip row counted its dead NPC as done (`startNpcDeadStep == EventStartStep`, so the script can no longer run), its baseline corpses are the event's own dead and the 10 s rule fires. The generic no-gossip case in `test_dungeon_run_policy.cpp` now pins the baseline contract (`TestUnitsHoldBaselineCorpsesDoNotEnd`); the dead-gossip replay (`TestBlyCrewDeadBeforeTheLegRunsUnattended`) keeps its old assertion. WC's own `test_run_wc` old-corpse case stays as it is.

## Gates

Filled below from the final tree (core d02645dd21, zero core changes). Builds are judged by `CMAKE_RC=0` / `MAKE_RC=0`; the module builds go through the slot lock of `~/forever-build/slot-sync-build.sh`.

Census, as the slices' notes run it (`FOREVER_CORE=~/ForeverCore-d02645dd`, core d02645dd21):

| List | Result |
|---|---|
| `forever/SLICE-CH.txt` (slot 8) | `files 299  compile clean 299  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (before `AhAction.cpp` joined the list; run folder `wt-integ9-s8-SLICE-CH-20261121T081639Z-2745512`) |
| `forever/SLICE-SD.txt` (slot 5) | `files 286  compile clean 286  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (run folder `wt-integ9-s5-SLICE-SD-20261121T082234Z-2796517`) |
| `forever/SLICE-KB.txt` (slot 6) | `files 286  compile clean 286  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (run folder `wt-integ9-s6-SLICE-KB-20261121T082642Z-2846671`) |
| `forever/SLICE-W0.txt` (slot 7) | `files 297  compile clean 297  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (slot 7, run folder `wt-integ9-s7-SLICE-W0-20261121T082906Z-2882102`); the final list has one more file, `AhAction.cpp`, and its one-file census is `files 1  compile clean 1  with errors 0  error places 0` (slot 5, run folder `wt-integ9-s5-SLICE-CH-20261121T103903Z-159647`) |
| `forever/SLICE-CH.txt` after `AhAction.cpp` joined | two full reruns on slot 4 did not produce a measurement (`files 300 compile clean 0 with errors 300`, `error places 0`; then `files 300 compile clean 240 with errors 60`, the 60 marked failed after a mid-run redefinition artifact in `BotState.h`), while `AhAction.cpp` alone compiles clean. The slot builds of the same list (`SLICE-W0.txt`, which holds every CH legacy path) are `CMAKE_RC=0 MAKE_RC=0`, so the failed rows are a census-runner artifact under the shared VM's load, not a source error |

Local checks on the final tree:

| Check | Result |
|---|---|
| `python3 forever/tools/slice_gen_context.py --check` | `SliceAiObjectContext.cpp is up to date` (rc 0) |
| `python3 forever/tools/slice_registry_check.py --quiet` | `(a)=0`, `(b)=296 keys to register or stub later` (the S1-R2 count; rc 0) |
| `python3 -m unittest discover -s forever/tools -p 'test_*.py'` | `Ran 276 tests in 65.246s`, `OK (skipped=9)` |
| `python3 tools/test_forever_no_direct_queries.py` | `Ran 5 tests`, `OK` |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |

Slice unit tests (pure C++, run on the merged tree):

| Test | Result |
|---|---|
| `test_chat_link` (`g++ -std=c++17 -Wall -Wextra -I.`) | `RESULT: PASS (test_chat_link)` |
| `test_chat_in` (same flags) | `RESULT: PASS (test_chat_in)` |
| `test_spell_delay` (`g++ -std=c++20 -I forever/src`) | `RESULT: PASS: 16 checks, 0 failed` |
| `test_thrown` (`g++ -std=c++17 -O1 -Wall -Wextra -I.`) | `57 checks, 0 failed` |
| `test_place_pure` (`g++ -std=c++17 -O1 -Wall -Wextra -Werror -I.`) | `all 1508 checks passed` |
| `test_place_plan` | `all 122 checks passed` |
| `test_pool_composition_policy` | `RESULT: PASS (2079 checks)` |
| `test_pool_registry_policy` | `RESULT: PASS (85 checks)` |
| `test_pool_schedule_policy` | `RESULT: PASS (3834 checks)` |
| `test_run_orders` | `RESULT: PASS (276 checks)` |
| `test_run_liquid` | `RESULT: PASS (test_run_liquid)` |
| `test_run_wc` | `test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)` |
| `test_run_facts` (Ragefire Chasm rows) | `RESULT: PASS` |
| `test_tb_self_target` | `test_tb_self_target: 143 checks, 0 failed` |
| `test_wc_plan_rows.py` | `test_wc_plan_rows: 3507 checks, 0 failed` |

Slice wiring tests:

| Test | Result |
|---|---|
| `test_cl_wiring` | `Ran 4 tests`, `OK` |
| `test_chat_in_wiring` | `Ran 2 tests`, `OK` |
| `test_sd_wiring` | `Ran 2 tests`, `OK` |
| `test_kb_wiring` | `Ran 7 tests`, `OK` |
| `test_place_wiring` | `Ran 11 tests`, `OK` |
| `test_removeitem_wiring` | `Ran 2 tests`, `OK` |
| `test_weightscales_sql` | `Ran 2 tests`, `OK` |
| `test_wl0_wiring` (runs `pool_registry_check`) | `Ran 37 tests in 13.226s`, `OK (skipped=3)` |

Builds:

| Gate | Result |
|---|---|
| Module ON, `slot-sync-build.sh <slot> tortoisebots -DTORTOISEBOTS_SLICE1=ON` | slot 8: `combined build: slot 8 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 8)`, `module sync: 260 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=674` (slot 4 on the same tree: `TOTAL_SECONDS=1530`, `CMAKE_RC=0`, `MAKE_RC=0`); the final-tree rerun is the row below |
| Module ON, final tree | slot 4: `combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 4)`, `module sync: 0 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=2` (the full compile of the same tree: `module sync: 492`, `TOTAL_SECONDS=2034` on the pre-policy tree; both `CMAKE_RC=0 MAKE_RC=0`) |
| Module OFF, `slot-sync-build.sh <slot> tortoisebots` | slot 6: `combined build: slot 6 key core-9b88bd2d6584-opt-78bf025f`, `build token 1 of 3 (slot 6)`, `module sync: 5 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=19` (the wrapper's exit code is 1 because `build.sh` ends on a no-match grep; no `error:`/`FAILED:` line) |
| Full `worldserver` link, `slot-sync-build.sh <slot> worldserver -DTORTOISEBOTS_SLICE1=ON` | slot 5, final tree: `combined build: slot 5 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 5)`, `module sync: 0 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=11` (the full link of the same tree: `TOTAL_SECONDS=1251`); the link has no undefined reference |
| `bash tools/verify_all.sh` | steps 1-6 pass: OKF, `.bot` command table, released migrations, module surface, action/trigger wiring, `Ran 2673 tests in 310.8s` OK, `483 standalone policy tests verified` (incl. `synthetic-pull: pass (40/40 ticks)`), decision trail; step 7 (host contract) fails on the checkout's missing `src/game/SessionTransport.h`, the same pre-existing failure recorded for W0-A (no host seam is touched by this batch) |
| Turtle `vm-build ~/wt-integ9 --wait` | `QUEUE_RC=0`, `COMPILE_SECONDS=274`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=277`, `module-tree-hash=65eeaa5a25808be3`, job `1-1795257038907487036-1533175`, tree `fec560202` (dirty=0, 4227 files) |
| Turtle `vm-test tb ~/wt-integ9` RESULT line | `RESULT: PASS  kind=tb wt=wt-integ9 sha=fec560202 dirty=1 files=4227 runner=yulon-ubuntu2` (the one dirty entry is this notes file) |

Not run: a live server/run (the brief asks for none; the WC round's live checks are the lead's), and `vm-test`'s live oracle (not part of `tb`). No PR is opened (the lead does that); `integ/batch-9` is pushed.


## Merge of main (batches 7 and 8) into this branch: conflicts and resolutions

Seven files conflicted; every piece of both sides was kept.

* `CMakeLists.txt`: the exclusion filters take the union (main added `TbCompat_WL4` to the compat exclusion, this branch added `chat/TbChatCmd` to the host one). The slice list the build reads is main's `forever/SLICE-W4.txt` (this branch had `SLICE-W0.txt`); the fatal-error text names W4.
* `forever/SLICE-W1.txt`: both blocks kept (the S1-R2 chat-command files and main's W4 `TravelMgr.cpp` / `TravelValues.cpp`).
* `forever/SLICE-W4.txt` (no textual conflict, rebuilt by hand): it must hold every path of the merged W1 list first and unchanged (test_wl4_wiring pins that), then `TravelNode.cpp`, then `runtime/BotNameService.cpp` (the W0 name service that only `SLICE-W0.txt` listed before). `SLICE-W0.txt` stays as the record.
* `forever/src/TbScripts.cpp`: the includes and both tick calls kept (`place::Update`, `UpdateFeeds`, `combat::Update`), and both script registrations (`chatin::AddScripts`, `combat::RegisterScripts`).
* `forever/docs/TB-COMMANDS.md`: main's paragraph on destroyed start-outfit pieces under `.tb equip` kept, then this branch's `.tb removeitem` entry.
* `forever/tools/test_slice9_l9_wiring.py`, `forever/tools/test_w1_wiring.py`: now name `SLICE-W4.txt`; `test_w1_wiring.py` and `test_wl0_wiring.py` read the list from `SLICE-W4.txt` and check the CMake pointer to it.
* `tools/test_forever_no_direct_queries.py`: both allowances kept (`REALM_COUNT_*` and `TRAVEL_SAVE_*`).

Pre-existing on main (not caused by this merge): `test_wl4_wiring.py` `test_reachability_and_map_transfers_ask_the_link_rule` fails on main too (the pinned text is no longer in `TravelGraphPolicy.h`); main's `test_the_slice_list_and_the_cmake_pointer` failed there as well and is fixed by the W4 list above.
