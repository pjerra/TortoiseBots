# Commander port, package P2: binding lifecycle and persistence

Spec: `~/TortoiseBots-audit/FOREVER-COMMANDER-PORT-SPEC.md` (P2 row in section 5; sections 4, 7 and 10, decisions D2 and D3); P1's "P2 needs"
(`CMD-P1-NOTES.md`). Branch `cmd/p2` from `tbf/main` (P1 merged there).

## What it is

* **The record beside h3's table** now carries what a restart and a release need: the binding's flags (in-party, raid, player-led, system), the stored
  member list, the followers whose `manual follow target` the bind pointed at the lead (with their own value), the lead's live group counter (in memory
  only) and the two timers (a dead lead, an absent commander).
* **Release from hooks, not a poll.** `GroupScript` (add/remove/leader/disband) ends a binding whose lead, group or leadership changed or whose party
  gained a real player; `PlayerScript` OnLogin/OnLogout handles the commander's grace and the lead's departure; `UnitScript::OnUnitDeath` marks a dead
  lead. A 1 Hz `Maintain` only enforces the two timers, the raid/size reasons and a pending restore.
* **Persistence (D3), off by default.** `TortoiseBots.Commander.KeepBindings = 1` writes one row per commander to `tb_commander_binding` in the
  characters database; the module creates the table itself (and `forever/sql/characters/2026_11_20_00_tb_commander_binding.sql` ships the same DDL).
  When the commander and the lead are back in the world the party is formed again through the lead and every member is bound to the commander.
* **The host questions** `IsCommandedBy` / `IsInOwnBindingGroup` now read the record (`TbHostOwnership.cpp`).
* **Dashboard**: the dash-runs collector calls `cmd::AppendBindings` for every standing binding and `cmd::FillRunOrders` for the run cards it builds
  itself.
* **The order-log hook** moved from the run tick into the executor `Issue` (one path for the run tick and `.tb run order`).

## Rules as built

* **Guids only (D2).** The stored row has five fields: `commander_guid`, `lead_guid`, `members`, `flags`, `bound_at`. No group id, no group counter
  (the core reuses its database group ids, `FreeGroupDbStoreId`). The live group is always found through the lead; the in-memory counter is refreshed
  whenever the lead's group holds a bound follower, so a group re-created around the lead keeps the binding and the card reports the new counter.
* **A fallback does less.** With the key off nothing is written or read. With the key on but the table missing (no privilege, no migration) one error
  line is logged and every read/write is skipped: persistence is off for the session, the in-memory binding is unaffected.
* **Every characters query is guarded.** `CharacterQueryCompat` (TbCmdBindingStore.cpp) is `WorldQueryCompat`'s characters twin: it asks
  information_schema for the table and the columns first, because a query naming a missing table aborts this core (MySQLConnection.cpp).
* **Nothing is written on a shutdown.** `NoteLogout` returns while `World::IsStopped()`: a server restart keeps the row and the next start restores it.
  A real logout starts the commander's grace (`TortoiseBots.Slice.MasterGraceSeconds`: the same grace the bots themselves wait for their master).
* **The two timers.** A lead dead for `TortoiseBots.Slice.DeadBackstopSeconds` (the module's existing "a commanded bot's body lies dead" grace) releases
  with `lead-dead`; a commander away for `MasterGraceSeconds` releases with `commander-away`.
* **The release is T's.** Followers get their own follow target back unless something else changed it; h3's pairs are cleared; the row is deleted; an
  in-party binding uses `RemoteCommander::DecideReleaseParty` (the bots leave the party, or the lead goes back to the commander).
* **What was not ported here:** the threat-backoff and autonomy ledgers are dead weight on this port (the slice factory gives a commanded non-tank the
  threat back-off in its kit and never gives a bot the open-world kit), so no ledger is kept. No `.tb master ... inparty` command exists on ForeverCore;
  the flag is read from what the bind saw (the commander in the lead's party).

## U6: do `GroupScript` hooks fire for the module's bot groups?

Answer (live, 2026-11-20, forever-test2): **yes**. `BotMgr::JoinParty` forms a bot party through the public `Group` API (`Group::AddMember`), and the
core calls `sScriptMgr->OnGroupAddMember` from inside it (Group.cpp:506, the same for removal :565, leader :690, disband :724).

Experiment: four host bots were logged in and bound (`.tb party Mtank Mpriest Mdpsa`, `.tb master party Mtank Mcmd`), then `.tb ungroup Mtank` (the
lead leaves its party) logged `TortoiseBots: the commander binding of Mcmd was released: the group was disbanded or the lead bot left it
(group-changed)` and `.tb commander status` answered `commander-end count=0 bindings=0`. The GroupScript row sees the module's own bot groups, so the
release does not need the Turtle 1 Hz sweep (a `Maintain` timer stays only for the two graces and the pending restores).

Two things the live run taught, both fixed here:
* the group hooks must be as silent during a shutdown as `NoteLogout` is: the bots leave their groups while the world stops, and a release there would
  delete the row the next start restores (`World::IsStopped()` guards all four);
* a saved group can hold bot characters that are not logged in, and `BotMgr::IsBot` only knows the ones that are: "a real player joined" counts a
  member that is in the world and no host bot, and a real player logging into an already saved bound party is caught in `NoteLogin` (the core attaches
  saved members at login without an AddMember event).

## Files

| File | What |
|---|---|
| `forever/src/host/cmd/TbCmdBindingLifePure.h` | pure: the stored row's five fields and `Valid`, the release verdict `Decide` (the shared `CheckBinding`'s reasons plus the two graces), the party reason mapping, `Departed`, and the restore gate `PlanRestore` (commander AND lead in the world). Tested standalone |
| `forever/src/host/cmd/TbCmdBindingStore.{h,cpp}` | the characters-database row: config, the guard `CharacterQueryCompat` (WorldQueryCompat's characters twin), the CREATE TABLE IF NOT EXISTS, load once, save/erase with `{}` formats |
| `forever/src/host/cmd/TbCmdBinding.{h,cpp}` | the record beside h3's table (flags, members, followers and their saved follow targets, the in-memory group counter, the two timers), the release, the party action, the restore, the P1 card and the P2 hooks |
| `forever/src/TbScripts.cpp` | the `GroupScript` row (`OnAddMember`/`OnRemoveMember`/`OnChangeLeader`/`OnDisband`), `PlayerScript` OnLogin/OnLogout, `UnitScript::OnUnitDeath`, `Maintain` in the tick, `LoadBindings` at world start and on a config reload |
| `forever/src/host/TbHostOwnership.cpp` | `IsCommandedBy` / `IsInOwnBindingGroup` answer from the record |
| `forever/src/dash/TbDashRuns.cpp` | `cmd::AppendBindings` + `cmd::FillRunOrders` (every binding on the Commander tab, run cards keep their orders) |
| `forever/src/host/run/TbRunOrders.cpp`, `TbRun.cpp` | the order-log hook moved into `Issue` |
| `forever/sql/characters/2026_11_20_00_tb_commander_binding.sql`, `forever/sql/README.md` | the row's DDL and its delivery |
| `forever/conf/tortoisebots.conf.dist` | `TortoiseBots.Commander.KeepBindings = 0` with its documentation |
| `forever/tools/cmd_binding_life_check.cpp`, `test_cmd_p2_wiring.py` | the P2 check (53 checks) and the wiring pins/mutations |

## Gates (slot 7, core pin 6517af05)

| Gate | Result |
|---|---|
| census, list `forever/SLICE5.txt` (P2 adds no legacy file) | `files 176  compile clean 176  with errors 0  error places 0` |
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0` (64,693,656-byte binary); the ON library defines `ReleaseCommander`, `RestoreOne`, `store::Save` |
| option-OFF `worldserver` | `CMAKE_RC=0 MAKE_RC=0` (55,819,952 bytes); the OFF library has the no-op hook stubs only |
| `forever/tools/cmd_binding_check.cpp` (P1) | `cmd_binding_check: 57 checks, 0 failed` |
| `forever/tools/cmd_binding_life_check.cpp` (P2) | `cmd_binding_life_check: 53 checks, 0 failed` |
| `test_cmd_p1_wiring.py` with `CMD_P1_MUTATE=1` | 12 tests OK (three mutations caught) |
| `test_cmd_p2_wiring.py` with `CMD_P2_MUTATE=1` | 12 tests OK (`lead-offline`, `dead-grace`, `away-grace` each FAIL the check; the row checker refuses a group column) |
| `test_run_c6.cpp`, `test_run_cancel.cpp`, `test_run_orders.cpp` | `PASS: 28 checks`, `PASS`, `PASS (159 checks)` |
| `test_run_review_wiring.py`, `test_slice4_d4_wiring.py`, `test_dash_runs_wiring.py`, `test_run_revfix1/2_wiring.py` | OK |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `summary: (a)=0  (b)=215`; `SliceAiObjectContext.cpp is up to date` |
| `tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent).` |
| Turtle `vm-build` / `vm-test tb` | not run: no file outside `forever/` changed (`git diff --name-only tbf/main...HEAD` has 0 such files) |

## Live (forever-test2, release binary kept as `bin/worldserver.pre-cmdp2` and restored after)

The module's ON binary (slot 7) was installed with `TortoiseBots.Commander.KeepBindings = 1`, `MasterGraceSeconds = 5`, `DeadBackstopSeconds = 10`;
four host bots (Mtank, Mpriest, Mdpsa, Mcmd; two fresh accounts) were created, logged in and bound.

* **The table.** The ON boot logged and created `characters.tb_commander_binding` (the module's own CREATE; `SHOW TABLES` lists it).
* **A binding.** `.tb party Mtank Mpriest Mdpsa` + `.tb master party Mtank Mcmd`: `commander-end count=4 records=4 bindings=1`,
  `binding cmd=Mcmd lead=Mtank group=1 bound=3 bots=3`. The row: `5 | 2 | 2,3,4 | 0 | 1795167126`; `groups` row 1 (leader 2) with three members.
* **U6.** `.tb ungroup Mtank` logged `TortoiseBots: the commander binding of Mcmd was released: the group was disbanded or the lead bot left it
  (group-changed)` and the card became `commander-end count=0 records=0 bindings=0`: the GroupScript hook fires for the module's bot parties.
* **The commander's grace.** Bound again, `.tb logout Mcmd`: after 5 s `the commander binding of Mcmd was released: the commander has been away past
  the grace (commander-away)` and the bots logged out on their master's grace (PlayerScript OnLogout + the 1 Hz timer).
* **Restart, persistence ON.** Bound again (row `5|2|2,3,4`). `world.sh stop`: the row was still there, `groups` had no rows. Start: the four
  bots logged in one by one; the log showed `the commander binding of Mcmd was restored (lead Mtank, 1 bot(s))`, then `2 bot(s)`, then `3 bot(s)`;
  `.tb commander status` showed the same commander, lead, `bound=3 bots=3` and the original `boundat=1795167126`; `groups` held a new row (process
  counter 1 again) with members 2, 3 and 4 (`group_member`), and no release line.
* **Restart, persistence OFF.** `KeepBindings = 0`, restart, the four bots logged in: `.tb commander status` answered
  `commander-end count=0 records=0 bindings=0` and no restore line; the old row is still in the table and is simply not read (nothing is deleted with
  the key off).
* **Restore.** The release `bin/worldserver` and the original `tortoisebots.conf` were put back and the server restarted with them (ready, no
  commander lines). The test characters/accounts and the one `tb_commander_binding` row are left on that test server; nothing else was changed.

## Limits (things that do less)

* A binding of one bot with no party is released by the group rule (as T's), so a single-bot binding does not survive a restart.
* A member that was offline at the restore is bound when it comes back in the same session; after a restart the row is what was bound then, and a bot
  released by `.tb master <bot> none` is dropped from the row.
* The card's `boundat` and flags come from the bind; `group=` is always the lead's live counter (no group number is stored).
* The threat-backoff and autonomy ledgers are not ported (dead weight on this port, see above); the slice factory already carries the commanded
  threat back-off and gives no open-world kit.
