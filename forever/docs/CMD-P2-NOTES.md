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
* **Every characters statement is guarded, including the writes.** `CharacterQueryCompat` (TbCmdBindingStore.cpp) is `WorldQueryCompat`'s characters
  twin: it asks information_schema for the table and the columns first, because a statement naming a missing table aborts this core
  (MySQLConnection.cpp). The same five-column check runs before EVERY write (`Writable`/`CheckSchema`): a cached "the table is there" is never trusted,
  so a table dropped or altered after the load disables persistence instead of reaching the core. The guard also refuses any table but this store's own.
* **One atomic write.** A save is one `INSERT ... ON DUPLICATE KEY UPDATE` on a synchronous connection (the module's row has no prepared statement of
  its own on this core), so there is no DELETE-then-INSERT window: a crash, a disconnect or a refused INSERT can never leave the binding deleted. The
  in-memory view changes only after the row is read back; a write that did not land logs one line.
* **A restore only takes what the host says is this commander's.** Every stored member is checked against h3's table, the host's master table and the
  host-bot table (`life::Claim`): a bot bound or mastered elsewhere, or a character that is no host bot, is dropped from the row, never joined or bound.
  A lead that is no longer this commander's ends the row (and deletes it); the commander itself must be a real network player session or a host bot.
  An in-party binding puts the commander back into the party during the restore, and a released bot loses its master (`ClearBotMaster`), not only its
  h3 pair.
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
| `forever/src/host/cmd/TbCmdBindingStore.{h,cpp}` | the characters-database row: config (re-read on every reload), the guard `CharacterQueryCompat` (WorldQueryCompat's characters twin), the five-column check before every write, the CREATE TABLE IF NOT EXISTS once per config, one atomic upsert / one DELETE with a read-back, load once per load |
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
* A member that was offline at the restore is joined when it comes back in the same session **and is still unbound**; a member bound to another
  commander is never taken back. After a restart the row is what was bound then, and a bot released by `.tb master <bot> none` is dropped from the row.
* The card's `boundat` and flags come from the bind; `group=` is always the lead's live counter (no group number is stored).
* The threat-backoff and autonomy ledgers are not ported (dead weight on this port, see above); the slice factory already carries the commanded
  threat back-off and gives no open-world kit.
* **The commander's ownership cannot be checked against an account.** This host has no durable ownership table (`BotManager::GetOwnedCharacter`
  answers false), so the restore authorizes a commander that is a real network player session or a host bot of this module, and authorizes every
  stored bot against h3's table, the master table and the host-bot table. A hand-inserted row naming an online human is still a binding to that human:
  the row is the operator's, and the module has nothing else to compare it with.
* A run's tank is a card record made by the read, not a binding this module restores: it has no `.tb master` lead, so `Maintain` and the release
  commands leave its h3 pairs to the run (`TbRun.cpp`).

## Fix round (reviews of 2026-11-20)

Every finding was checked against the code first. `fix` means changed here with a check that fails on the previous code (`forever/tools/
cmd_binding_life_check.cpp` and its mutations in `test_cmd_p2_wiring.py`, or a wiring pin on the C++ path).

| Review | Finding | Verdict | Where / why |
|---|---|---|---|
| Codex | [critical] Restore can assign bots to an arbitrary online character | fix | `RestoreOne`: the commander must be a real network player session or a host bot (`AuthorizedCommander`) and every stored member must be free per `life::Claim` (h3 table, master table, host-bot table) before any grouping or bind; a foreign lead ends the row, which is deleted (`RefuseRow`). Pure check: `TestClaim`; mutation `claimed-steal`. Limit: no account ownership table exists to compare against (see Limits). |
| Codex | [high] Writes bypass the missing-schema compatibility guard | fix | `store::Writable`/`CheckSchema` run the five-column check before EVERY write (no cached trust), `store::DirectExecute` only; the guard refuses any table but this store's own. Wiring pins in `test_persistence_is_off_by_default_and_every_read_is_guarded`. |
| Codex | [high] Saving a binding can permanently lose its row | fix | one `INSERT ... ON DUPLICATE KEY UPDATE` in `store::Save`, read back before the in-memory view changes; `store::Erase` read back too; a write that did not land logs one line. No DELETE/INSERT pair is left. |
| Gemini | [critical] `Read()` makes a record for a run's tank with no lead, so `Maintain` wipes the run's bindings | fix | `Maintain` (and `TryRestore`, the release walk) skip a record whose lead is empty: such a record is a card the read made, not a binding of this module. Wiring pin in `test_the_release_reasons...`. |
| Gemini | [critical] `TryRestore`/`RestoreOne` steal bots from other commanders | fix | `TryRestore` only re-joins an UNBOUND member; `RestoreOne` re-checks h3 before each bind and cuts foreign members via `Claim`. Pins in `test_a_restore_never_takes_a_bot_from_another_commander`. |
| Gemini | [high] An in-party binding never puts the commander back into the party | fix | `RestoreOne` adds the commander to the lead's group when the row says `FlagInParty`; the release rule then reads him present. Pin on `group->AddMember(commanderPlayer)`. |
| Gemini | [high] `Save`/`Erase` are independent async statements that can reorder | fix | same as the Codex "lose its row": one synchronous atomic statement per write. |
| Gemini | [high] `Maintain` runs on every world tick | fix | 1 Hz throttle (`kMaintainMs`) before the work; wiring pin asserts the throttle precedes `TryRestore`. |
| Gemini | [high] `ReleaseCommander` leaves the bots' master bound | fix | `ClearBotMaster(bot)` for every released pair, as `.tb master none` does. Pin in the release assertions. |
| Gemini | [medium] `NoteMaster` corrupts `m.lead`/`m.members` | fix | the release path removes only the addressed bots that h3 really released, promotes a released lead to a still-bound follower (and re-points the followers), and keeps offline stored members; a rebind over a pending row replaces it instead of keeping its offline lead. |
| Gemini | [medium] Displaced rows and the order log grow without bound | fix | `PruneToStanding` deletes the row of a card with no bound bot left (it is not re-created after a restart); `ReleaseCommander` retains the log to the standing keys. Pin on `store::Erase` in `PruneToStanding`. |
| Gemini | [medium] `Save`/`Erase` bypass the guard, `EnsureTable` retries DDL, `LoadConfig` never resets | fix | all three: `Writable` before each write, the CREATE only once per config (`tableTried`), `LoadConfig` clears `loadTried`, `tableReady`, the warning and the rows on a reload; `Save`/`Erase`/`Rows` configure first. |
| Gemini | [medium] Tests leave invariants untested | fix | `cmd_binding_life_check.cpp`: the restore round-trips the stored text instead of comparing a copy; every release reason with `FlagRaid`/`FlagPlayerLed`/`FlagSystem`/`groupIsBg`; `Claim`; commander-in-members; a >64-bit member word. `test_cmd_p2_wiring.py`: the persistence, throttle, restore and steal pins above, three new mutations (`commander-member`, `members-overflow`, `claimed-steal`), each must FAIL the check. Limit: the core-facing C++ has no unit test (it needs a live core); the wiring pins are the check there. |
| Gemini | [low] `IsCommandedBy` ignores h3 ownership; `Valid`/`DecodeMembers` edges; guard escaping | fix | `IsCommandedBy` requires `h3::CommanderGuid(bot) == commander`; `Valid` refuses the commander inside its own members; `DecodeMembers` drops a word past 64 bits instead of wrapping; `CharacterQueryCompat` refuses any table but this store's own literal, so no name can ever be escaped in. |
