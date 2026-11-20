# Commander P3 Notes: One Order Path (`.tb cmd`)

Branch: `feat/cmd-p3-orders` (merged `origin/main` after `origin/cmd/p2` base).

## What was done

1. **Lifted `run::Issue` to `Party` (`forever/src/host/run/TbRunOrders.{h,cpp}`, `TbRunOrdersPure.h`):**
   - Added `run::Party` (`leader`, `members`, `owner`, `runRec`, `fromRun`, `persistentIcons`, `resolveTarget`).
   - `Issue(Party const&, ...)` is the single judge and executor for both dungeon runs and `.tb cmd`. `Issue(RunRec&, ...)` is a thin wrapper passing `ResolveHandle` and `fromRun = true`.
   - `cmd::NoteRunOrder` now lives once inside `Issue(Party const&, ...)` (removed from `TbRun.cpp` after merging `origin/main`), so both run orders and `.tb cmd` orders log to the commander card and dashboard `COMMANDERS` `orders[]`.
   - Added `Verb::Hold` (`hold` -> `+passive` in combat and non-combat, matching `.tb order ... hold`), and added `"hold"` to `h3::PreflightOrder` (`forever/src/host/TbOrders.cpp`).
   - Extended `ParseOrder` in `TbRunOrdersPure.h` to accept 32-hex `ObjectGuid` targets (`IsGuidHex32`) alongside run handles for `pull`, `pullback`, `attack`, and `focus`.
   - Added `SetPersistentSkull` in `TbRunOrders.cpp` for `DoAttack` and `DoPull` when `party.persistentIcons` is true and the lead has a live `Group*` (`Group::SetTargetIcon(7, targetGuid, changedBy)`). With no group, no icon is set.

2. **`.tb cmd` command handler (`forever/src/host/cmd/TbCmdOrders.{h,cpp}`, `forever/src/TbScripts.cpp`):**
   - Added `cmd::ResolveBinding(commander, lead, members)` in `TbCmdBinding.{h,cpp}` to read the caller's standing binding from `h3::CommanderBindings()` and `Meta::lead`.
   - `HandleCmd` parses grammar v2 tokens, requires a player sender (`needs-player-sender` from console) and a standing binding (`not-bound`), builds `run::Party` with `fromRun = false`, `persistentIcons = true`, and `h3::ParseGuidText` as `resolveTarget`, and calls `run::Issue`.
   - Replies in grammar v2 (`cmd-begin` / `cmd` / `cmd-end` when accepted; `error code=<code> cmd=cmd msg=<encoded> #token` when refused). Option-OFF build answers `error code=engine-not-built`.
   - Because `fromRun = false`, `h3::QueueOrder` bumps `LastOrder(bot).seq` and triggers `outside-order` pause on the next tick of any active `.tb run`.

3. **Docs & Tests:**
   - Documented `.tb cmd` in `forever/docs/TB-COMMANDS.md` (section 14).
   - Extended `forever/tools/test_run_orders.cpp` (`TestCommanderP3`) and added `forever/tools/test_cmd_p3_wiring.py` (with `CMD_P3_MUTATE=1` mutation check).

## Gate results

- `g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_orders.cpp -o /tmp/test_run_orders && /tmp/test_run_orders`:
  `RESULT: PASS (259 checks)`
- `test_run_tasks`: `test_run_tasks: 148 checks, 0 failed`
- `test_run_c6`: `RESULT: PASS: 28 checks, 0 failed`
- `test_run_cancel`: `RESULT: PASS (test_run_cancel)`
- `cmd_binding_check`: `cmd_binding_check: 57 checks, 0 failed`
- `cmd_binding_life_check`: `cmd_binding_life_check: 73 checks, 0 failed`
- `python3 forever/tools/test_cmd_p3_wiring.py`: `Ran 5 tests in 0.003s` / `OK`
- `CMD_P3_MUTATE=1 python3 forever/tools/test_cmd_p3_wiring.py`:
  `MUTATION FAILED AS EXPECTED: TbCmdOrders.cpp does not route orders through run::Issue`
- `python3 forever/tools/slice_registry_check.py`:
  `summary: (a)=0  (b)=224 keys to register or stub later`
- `python3 forever/tools/slice_gen_context.py --check`:
  `SliceAiObjectContext.cpp is up to date`
- `tbf-build.sh <worktree> tortoisebots` (`TORTOISEBOTS_SLICE1=ON`):
  `[286/286] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `TB_SLICE=OFF tbf-build.sh <worktree> tortoisebots` (`TORTOISEBOTS_SLICE1=OFF`):
  `[29/29] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `tbf-build.sh <worktree> census:forever/SLICE9.txt`:
  `files 176  compile clean 176  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`

## Live test steps (for combined lead test)

Setup: 5-bot party (`Mtank`, `Mpriest`, `Mdpsa`, `Mdpsb`, `Mdpsc`) in Ragefire Chasm (map 389), engines on, bound to GM character `Mcmd` via `.tb master party Mtank Mcmd`.

1. **Pull by 32-hex guid (persistent skull icon):**
   - Run as `Mcmd`: `.tb cmd pull <32-hex-mob-guid> #c1`
   - Expect: `cmd-begin #c1`, `cmd verb=pull status=accepted bots=1 result=bot%3AMtank%201 #c1`, `cmd-end count=1 records=1 verb=pull lead=Mtank bots=1 #c1`; target gets `skull` raid icon and `Mtank` pulls.
2. **Safe move:**
   - Run as `Mcmd`: `.tb cmd move <safe_x> <safe_y> <safe_z> #c2`
   - Expect: `cmd-begin #c2`, `cmd verb=move status=accepted bots=5 result=party%205 #c2`, `cmd-end ... bots=5 #c2`; bots move to point.
3. **Move across/into lava:**
   - Run as `Mcmd`: `.tb cmd move <lava_x> <lava_y> <lava_z> #c3`
   - Expect: `error code=unreachable cmd=cmd msg=... #c3` (or `no-ground` for `direct`); bots do not move.
4. **Rest & Hold:**
   - Run as `Mcmd`: `.tb cmd rest #c4` -> `cmd verb=rest status=accepted bots=5 ... #c4`
   - Run as `Mcmd`: `.tb cmd hold #c5` -> `cmd verb=hold status=accepted bots=5 ... #c5` (`hold=yes` in `.tb state`).
5. **Run interaction (`outside-order` pause):**
   - Start `.tb run start Mtank ragefire_chasm`, then as `Mcmd` run `.tb cmd stay #c6`.
   - Expect: `.tb cmd stay` answers `cmd-begin` / `cmd verb=stay status=accepted ... #c6`; on next run tick, run pauses with `why=outside-order` (no double orders).
6. **Commander card log:**
   - Run `.tb commander status Mcmd #c7`
   - Expect: `order cmd=Mcmd ... verb=pull ...`, `verb=move ...`, `verb=rest ...`, `verb=stay ...` in the card's `order` lines.

## Integration A+B (P3 + P6)

Merged `feat/cmd-p6-interact` into `feat/cmd-p3-orders`:

- Resolved conflicts in `forever/src/TbScripts.cpp` (single `.tb cmd` row -> `TortoiseBots::cmd::HandleCmd`), `forever/src/host/run/TbRun.cpp` (kept `Issue` owning `NoteRunOrder` and `tasks::Update(diffMs)` before `Table().empty()`), and `forever/docs/TB-COMMANDS.md` (unified section 14 covering order and task verbs).
- Wired `TbCmdOrders.cpp::HandleCmd` to dispatch P6 task verbs (`loot`, `open`, `use`, `gossip`, `state`, `lootmethod`, `ritual`, `scout`) through `TbCmdTasks.cpp::TryHandleTaskVerb(..., &taskOk)` after `SenderPlayer` + `ResolveBinding` and before `run::Issue`, removing the duplicate `HandleCmd` from `TbCmdTasks.cpp`.
- Rewired `TbCmdTasks.cpp::ResolveCommanderParty` to use P3's `cmd::ResolveBinding`.
- Updated `forever/tools/test_cmd_p2_wiring.py` (`test_the_order_log_lives_in_the_executor` checks lifted `Issue(Party const&, ...)`) and `forever/tools/test_slice9_doors_wiring.py` (`*ctx.runRec`).

### Gate lines (post-integration)

- `test_run_orders`: `RESULT: PASS (259 checks)`
- `test_run_tasks`: `test_run_tasks: 160 checks, 0 failed`
- `test_run_c6`: `RESULT: PASS: 28 checks, 0 failed`
- `test_run_cancel`: `RESULT: PASS (test_run_cancel)`
- `cmd_binding_check`: `cmd_binding_check: 57 checks, 0 failed`
- `cmd_binding_life_check`: `cmd_binding_life_check: 73 checks, 0 failed`
- `python3 forever/tools/test_cmd_p2_wiring.py`: `Ran 17 tests in 0.070s` / `OK (skipped=4)`
- `python3 forever/tools/test_cmd_p3_wiring.py`: `Ran 5 tests in 0.009s` / `OK`
- `CMD_P3_MUTATE=1 python3 forever/tools/test_cmd_p3_wiring.py`: `MUTATION FAILED AS EXPECTED: TbCmdOrders.cpp does not route orders through run::Issue`
- `python3 forever/tools/test_cmd_p6_wiring.py --mutate`: `PASS: test_cmd_p6_wiring (all 3 mutations caught)`
- `python3 forever/tools/test_slice9_doors_wiring.py`: `Ran 24 tests in 0.203s` / `OK`
- `python3 forever/tools/slice_registry_check.py`: `summary: (a)=0  (b)=224 keys to register or stub later`
- `python3 forever/tools/slice_gen_context.py --check`: `SliceAiObjectContext.cpp is up to date`
- `tbf-build.sh <wt> tortoisebots` (`TORTOISEBOTS_SLICE1=ON`): `[309/309] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `TB_SLICE=OFF tbf-build.sh <wt> tortoisebots` (`TORTOISEBOTS_SLICE1=OFF`): `[30/30] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `tbf-build.sh <wt> census:forever/SLICE9.txt`: `files 265  compile clean 265  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`

### Combined live steps (P3 + P6)

Setup: 5-bot party (`Mtank`, `Mpriest`, `Mdpsa`, `Mdpsb`, `Mdpsc`) in Ragefire Chasm (map 389), engines on, bound to GM character `Mcmd` via `.tb master party Mtank Mcmd`.

1. **Pull, move, lava refusal, rest, hold (P3):**
   - `.tb cmd pull <32-hex-mob-guid> #c1` -> `cmd verb=pull status=accepted ...`, target gets `skull` raid icon, `Mtank` pulls.
   - `.tb cmd move <safe_x> <safe_y> <safe_z> #c2` -> `cmd verb=move status=accepted bots=5 ...`.
   - `.tb cmd move <lava_x> <lava_y> <lava_z> #c3` -> `error code=unreachable cmd=cmd ...` (bots do not move).
   - `.tb cmd rest #c4` and `.tb cmd hold #c5` -> `status=accepted`.
2. **Loot, open, use, gossip, lootmethod, unsupported (P6):**
   - Kill a mob, `.tb cmd loot my Mtank #p6_1` -> `status=accepted`; `.tb cmd state Mtank #p6_2` -> `status=done verb=loot ...`.
   - Repeat `.tb cmd loot my Mtank #p6_3` on same corpse -> `.tb cmd state Mtank` reports `status=failed ... Nothing is left to loot`.
   - `.tb cmd open entry:<chest_entry> Mtank #p6_4` and `.tb cmd use entry:<door_entry> Mtank #p6_5` -> `status=accepted`, `.tb cmd state` -> `status=done`.
   - `.tb cmd gossip my Mtank #p6_6` -> lists menu; `.tb cmd gossip my Mtank 99 #p6_7` -> `status=failed ... No option 99`; `.tb cmd gossip my Mtank 1 #p6_8` -> `status=done`.
   - `.tb cmd lootmethod ffa #p6_9` -> `status=ok method=ffa`; `.tb cmd ritual #p6_10` -> `error code=unsupported`.
3. **Run interaction (`outside-order` pause) & card log:**
   - Start `.tb run start Mtank ragefire_chasm`, then `.tb cmd stay #c6` (or a task walk) -> run pauses with `why=outside-order` on next tick.
   - `.tb commander status Mcmd #c7` shows the issued orders in the card's `order` lines.
