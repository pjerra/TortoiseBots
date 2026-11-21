# Commander port, package P2: binding lifecycle and persistence

Spec: `~/TortoiseBots-audit/FOREVER-COMMANDER-PORT-SPEC.md` (P2 row in section 5; sections 4, 7 and 10, decisions D2 and D3); P1's "P2 needs"
(`CMD-P1-NOTES.md`). Branch `cmd/p2` from `tbf/main` (P1 merged there).

## What it is

* **The record beside h3's table** now carries what a restart and a release need: the binding's flags (in-party, raid, player-led, system), the stored
  member list, the followers whose `manual follow target` the bind pointed at the lead (with their own value), the lead's live group counter (in memory
  only) and the two timers (a dead lead, an absent commander).
* **Release from hooks, then the next tick.** `GroupScript` (add/remove/leader/disband) notices a binding whose lead, group or leadership changed or
  whose party gained a real player, and records the reason (character guid + live group counter) instead of releasing: the hook runs while the core is
  still walking the group, and a release there removes members (the group can disband and delete itself under the core). `PlayerScript` OnLogin/OnLogout
  handles the commander's grace and the lead's departure directly; `UnitScript::OnUnitDeath` marks a dead lead. `Maintain` (every world tick) runs the
  recorded releases outside the core's group walk, re-finding the binding and its group by guid; the same pass enforces the two timers, the raid/size
  reasons and a pending restore.
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
* **Every characters statement is guarded, including the writes.** `CharacterQueryCompat` (TbCmdBindingStore.cpp) parses the query form and the table
  (the world guard's own parser, `compat/TbSqlNames.h`), refuses any table but this store's own, and asks information_schema for the table and its columns
  first, because a statement naming a missing table aborts this core (MySQLConnection.cpp). The raw statements themselves are `compat/TbCompat_CharStore.h`'s
  (the characters twin of the world guard in `compat/TbCompat_S5.h`; the repo's schema-guard test names that file and pins its three calls), so no module
  file outside the guards makes a pool call. The same five-column check runs before EVERY write (`Writable`/`CheckSchema`): a cached "the table is there" is
  never trusted, so a table dropped or altered after the load disables persistence instead of reaching the core.
* **One atomic write.** A save is one `INSERT ... ON DUPLICATE KEY UPDATE` on a synchronous connection (the module's row has no prepared statement of its
  own on this core), so there is no DELETE-then-INSERT window: a crash, a disconnect or a refused INSERT can never leave the binding deleted. The
  in-memory view changes only after the row is read back; a write that did not land logs one line.
* **A delete that does not land is pending, not done.** `store::Erase` reads the row back; when it is still there the commander is kept in
  `pendingErase`, `Rows()` never offers that row to a restore, a config reload keeps the mark, and the 1 Hz `Maintain` (`store::RetryErases`) retries the
  DELETE until the database takes it. `ReleaseCommander` then logs `the release is not complete` instead of `released` (the live binding is still
  cleared: a fallback does LESS), and `RestoreOne`, `TryRestore` and `LoadBindings` all refuse a pending commander. A later bind of the same commander
  (`Save`) clears the mark, so the retry can never delete what a newer bind wrote; `RefuseRow` and `PruneToStanding` get the same handling from the
  store. The mark is in memory: a process that stops before a retry lands still has the row and the next start restores it (a durable tombstone would
  need a sixth column, refused by D2).
* **A restore only takes what the host says is this commander's.** Every stored member is checked against h3's table, the host's master table and the
  host-bot table (`life::Claim`): a bot bound or mastered elsewhere, or a character that is no host bot, is dropped from the row, never joined or bound.
  A lead that is no longer this commander's ends the row (and deletes it); the commander itself must be a real network player session or a host bot.
  An in-party binding puts the commander back into the party during the restore and, when the row says he led it, puts him back at the party's head
  (the recreated group would otherwise stay led by the bot); a released bot loses its master (`ClearBotMaster`), not only its h3 pair. The commander's
  membership is read by guid from the group's own member list, so an offline or zoning commander still in the party is not "left" - his logout is the
  grace timer's business. A leaving or kicked in-party commander is noticed in the removal hook and ends his binding with `commander-left-party` on the
  next tick, before the core's promoted leader or disbanded party can report it as a leader/group change (and outside the removal, which goes on using
  the group).
* **Nothing is written on a shutdown.** `NoteLogout` returns while `World::IsStopped()`: a server restart keeps the row and the next start restores it.
  A real logout starts the commander's grace (`TortoiseBots.Slice.MasterGraceSeconds`: the same grace the bots themselves wait for their master).
* **The two timers.** A lead dead for `TortoiseBots.Slice.DeadBackstopSeconds` (the module's existing "a commanded bot's body lies dead" grace) releases
  with `lead-dead`; a commander away for `MasterGraceSeconds` releases with `commander-away`.
* **The release is T's.** Followers get their own follow target back unless something else changed it; h3's pairs are cleared; the row is deleted; an
  in-party binding uses `RemoteCommander::DecideReleaseParty` (the bots leave the party, or the lead goes back to the commander).
* **Which bot is the lead.** `.tb master party <leader> <commander>` names a host bot this commander commands. When the addressed party's leader is
  a player (a commander leading his own party names himself), there is no bot to name, so the lead is a bound bot of that party, a tank first and
  otherwise in the party's order; a previous lead still bound is kept across a rebind. The arbitrary first bot h3 holds is the last resort only.
* **Who leads the party.** The binding stands while the lead bot leads or, for a binding made IN the party, while its own commander leads (an
  in-party commander binding his own party: released within seconds per leader change before this, live run 2026-11-21); leadership going to anyone
  else - a third real player, or a bot that is not this lead - releases it with `leader-changed`. A binding made from outside the party keeps its old
  rule in full: its commander joining the lead's party and taking the lead is a leader change. `FlagPlayerLed` records the in-party commander's own
  lead: read from the group at the bind and in the 1 Hz fact read, and in the leader hook from the guid the hook is about to store (the core fires
  `OnGroupChangeLeader` before it records the new leader, so `group->IsLeader` inside the hook answers the previous one).
* **What was not ported here:** the threat-backoff and autonomy ledgers are dead weight on this port (the slice factory gives a commanded non-tank the
  threat back-off in its kit and never gives a bot the open-world kit), so no ledger is kept. No `.tb master ... inparty` command exists on ForeverCore;
  the in-party and player-led flags are read from the live group (the commander is a member; the commander himself holds the lead).

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

## Which group hooks may not release (audit)

Every hook below is called by the core while it is still using the group, and the binding's release is not read-only: an in-party release removes
every bound bot (`Group::RemoveMember`), which can take the group below two members, call `Disband()` and `delete this` (Group.cpp:786), after which
the core keeps using the freed group. All four therefore record the event (`DeferRelease`) and never touch a group; `Maintain`'s `RunPendingReleases`
does the work on the next tick, re-finding binding and group by guid (no `Group*` crosses a tick).

| Hook | Core call site | Why a release there is unsafe |
|---|---|---|
| `OnRemoveMember` | `Group::RemoveMember`, the first statement (Group.cpp:565) | its own removal continues: leader promotion (`ChangeLeader`), member erase, `SendUpdate`, and `Disband()` when the group drops below two |
| `OnAddMember` | `Group::AddMember`, mid-way (Group.cpp:506) | the core goes on with packets, pet updates and the member list of the group a release could have freed |
| `OnChangeLeader` | `Group::ChangeLeader`, before the new leader is stored (Group.cpp:690) | the core then writes the leader, the DB row and broadcasts on the group a release could have freed; a nested `ChangeLeader` from `BotsLeave` also re-enters |
| `OnDisband` | `Group::Disband`, its first statement (Group.cpp:724) | `Disband` ends with `RemoveGroup(this); delete this;` - any release (even a lookup of its members) runs on a group being deleted |

The player hooks (`NoteLogin`, `NoteLogout`) and `Maintain` release directly: they run outside a group operation (login/logout/player removal), so the
group they touch is not in an outer `Group` call.

## Files

| File | What |
|---|---|
| `forever/src/host/cmd/TbCmdBindingLifePure.h` | pure: the stored row's five fields and `Valid`, the release verdict `Decide` (the shared `CheckBinding`'s reasons plus the two graces), the party reason mapping, `DeferredRelease` (what a group hook's recorded event earns on its tick), the command gate `MayCommand`, `Departed`, and the restore gate `PlanRestore` (commander AND lead in the world). Tested standalone |
| `forever/src/host/cmd/TbCmdBindingStore.{h,cpp}` | the characters-database row: config (re-read on every reload), the guard `CharacterQueryCompat` (query form, its own table, the five-column check before every write; the raw statements are `forever/src/compat/TbCompat_CharStore.h`'s), the CREATE TABLE IF NOT EXISTS once per config, one atomic upsert / one DELETE with a read-back, a failed DELETE kept pending (never offered to a restore, retried every second, replaced by a newer save), load once per load |
| `forever/src/compat/TbCompat_CharStore.h` | the characters-database side of the schema guard: the information_schema probe, the guarded SELECT and the guarded synchronous statement (`forever/src/compat/TbCompat_S5.h` is the world twin) |
| `forever/src/host/cmd/TbCmdBinding.{h,cpp}` | the record beside h3's table (flags, members, followers and their saved follow targets, the in-memory group counter, the two timers), the release, the party action, the restore, the command gate (`HasPending`/`ResolveBinding`/`IsCommandedBy`), the P1 card and the P2 hooks |
| `forever/src/TbScripts.cpp` | the `GroupScript` row (`OnAddMember`/`OnRemoveMember`/`OnChangeLeader`/`OnDisband`), `PlayerScript` OnLogin/OnLogout, `UnitScript::OnUnitDeath`, `Maintain` in the tick, `LoadBindings` at world start and on a config reload |
| `forever/src/host/TbHostOwnership.cpp` | `IsCommandedBy` / `IsInOwnBindingGroup` answer from the record |
| `forever/src/dash/TbDashRuns.cpp` | `cmd::AppendBindings` + `cmd::FillRunOrders` (every binding on the Commander tab, run cards keep their orders) |
| `forever/src/host/run/TbRunOrders.cpp`, `TbRun.cpp` | the order-log hook moved into `Issue` |
| `forever/sql/characters/2026_11_20_00_tb_commander_binding.sql`, `forever/sql/README.md` | the row's DDL and its delivery |
| `forever/conf/tortoisebots.conf.dist` | `TortoiseBots.Commander.KeepBindings = 0` with its documentation |
| `forever/tools/cmd_binding_life_check.cpp`, `test_cmd_p2_wiring.py` | the P2 check (89 checks) and the wiring pins/mutations |
| `forever/tools/cmd_binding_store_check.cpp`, `forever/tools/cmd_binding_store_fakes/*` | the store's own TU compiled against fake `Config.h`/`DatabaseEnv.h`/`Log.h`/`StringFormat.h`, whose result set has the core's contract (`Query` answers a set already standing on its first row, and null for no rows): one and many rows, Save's read-back, a write/DELETE that did not land, a refused DELETE kept pending (out of `Rows()`, across a reload, retried, replaced by a newer save), refused rows, the key off, a missing column |

## Gates (slot 7, core pin 6517af05; counts are after the final fix round of 2026-11-20)

| Gate | Result |
|---|---|
| census, list `forever/SLICE5.txt` (P2 adds no legacy file) | `files 176  compile clean 176  with errors 0  error places 0` |
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0` (64,706,480-byte binary); the ON binary carries `store::RetryErases` and the store's pending-delete path |
| option-OFF `worldserver` | `CMAKE_RC=0 MAKE_RC=0` (55,819,952 bytes); the OFF binary has no `ReleaseCommander`/`RetryErases` (the no-op hooks only) |
| `forever/tools/cmd_binding_check.cpp` (P1) | `cmd_binding_check: 57 checks, 0 failed` |
| `forever/tools/cmd_binding_life_check.cpp` (P2) | `cmd_binding_life_check: 73 checks, 0 failed` |
| `forever/tools/cmd_binding_store_check.cpp` (store TU + fake core, g++) | `cmd_binding_store_check: 66 checks, 0 failed` |
| `test_cmd_p1_wiring.py` with `CMD_P1_MUTATE=1` | 12 tests OK (three mutations caught) |
| `test_cmd_p2_wiring.py` with `CMD_P2_MUTATE=1` | 17 tests OK (ten mutations caught: `lead-offline`, `dead-grace`, `away-grace`, `commander-member`, `members-overflow`, `claimed-steal`, `rows-skip-first`, `erase-forgotten`, `pending-restored`, `retry-deletes-a-rebind`; the row checker refuses a group column) |
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

### Final round live (2026-11-20)

The final-round ON binary (slot 7, md5 `7c1eb512…`) was installed as `bin/worldserver` (the release saved as `bin/worldserver.pre-fix4`) with
`TortoiseBots.Commander.KeepBindings = 1`, `MasterGraceSeconds = 5`, `DeadBackstopSeconds = 10`; `tb_commander_binding` was cleared first.

* **Persistence ON, restart restores.** Logins `Mcmd`, `Mtank`, `Mpriest`, `Mdpsa`; `.tb party Mtank Mpriest Mdpsa`; `.tb master party Mtank Mcmd`:
  `binding cmd=Mcmd cguid=5 online=yes lead=Mtank group=1 bound=3 bots=3 ... boundat=1795176754`; row `5 | 2 | 2,3,4 | 0 | 1795176754`; `groups` row 1
  (leader 2) with `group_member` 2/3/4. `world.sh stop` left the row and emptied `groups`. Start + the same four logins:
  `the commander binding of Mcmd was restored (lead Mtank, 1 bot(s))`, then 2 and 3; status showed the same commander, lead, `bound=3` and
  `boundat=1795176754`; `groups` was re-created (row 1, leader 2, members 2/3/4).
* **Release, restart stays released.** `.tb master party Mtank none`: `the commander binding of Mcmd was released: released by the commander (unbound)`,
  `commander-end count=0 records=0 bindings=0`, and `SELECT COUNT(*) FROM tb_commander_binding` = 0. Stop/start + the four logins: the status card was
  `commander-end count=0 records=0 bindings=0`, no `restored` line, and the table and `groups` stayed empty.
* **Persistence OFF restores nothing and queries nothing.** A known row `5|2|2,3,4|0|1795176754` was written with `KeepBindings = 0`; the restart with
  the same ON binary and the four logins logged no `commander binding` line, left `groups` empty and left the row untouched. With MySQL `general_log`
  (table output) on for that boot and off before the check, `SELECT COUNT(*) FROM mysql.general_log WHERE argument LIKE '%tb_commander_binding%'`
  answered `0`: the key off sends no statement naming the table.
* **Restore.** The release `bin/worldserver` (`md5 ce7e446e…`, same as `worldserver.pre-fix4`) and the original `tortoisebots.conf`
  (`tortoisebots.conf.pre-fix4`) were put back, the test row deleted (general_log back off, `log_output = FILE`), and the release booted to `ready` with
  `0` commander lines; the server was stopped afterwards.

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
cmd_binding_life_check.cpp` / `cmd_binding_store_check.cpp` and their mutations in `test_cmd_p2_wiring.py`, or a wiring pin on the C++ path).

| Review | Finding | Verdict | Where / why |
|---|---|---|---|
| Codex | [critical] Restore can assign bots to an arbitrary online character | fix | `RestoreOne`: the commander must be a real network player session or a host bot (`AuthorizedCommander`) and every stored member must be free per `life::Claim` (h3 table, master table, host-bot table) before any grouping or bind; a foreign lead ends the row, which is deleted (`RefuseRow`). Pure check: `TestClaim`; mutation `claimed-steal`. Limit: no account ownership table exists to compare against (see Limits). |
| Codex | [high] Writes bypass the missing-schema compatibility guard | fix | `store::Writable`/`CheckSchema` run the five-column check before EVERY write (no cached trust), `store::DirectExecute` only; the guard refuses any table but this store's own. Wiring pins in `test_persistence_is_off_by_default_and_every_read_is_guarded`. |
| Codex | [high] Saving a binding can permanently lose its row | fix | one `INSERT ... ON DUPLICATE KEY UPDATE` in `store::Save`, read back before the in-memory view changes; `store::Erase` read back too; a write that did not land logs one line. No DELETE/INSERT pair is left. |
| Codex (re-check) | [high] `ReadRows` skips the first database row | fix | the reader advanced with `while (result->NextRow())` before its first `Fetch`, but ForeverCore's `Query` hands the set over already standing on row 1 (`BasicStatementTask::Query` calls `NextRow`; an empty answer is a null result). So the first row was dropped: a one-row table read as empty (Save's read-back failed, its in-memory view never updated) and a DELETE that did not land looked landed. Now `do { Fetch ... } while (NextRow())`. Check: `cmd_binding_store_check` compiles the store's own TU against a fake characters database with that contract (one row, three rows, Save's read-back, a refused write and DELETE); the old loop reports 8 failures there. Mutation `rows-skip-first`; pin `test_the_stored_rows_start_at_the_first_row`. |
| Gemini | [critical] `Read()` makes a record for a run's tank with no lead, so `Maintain` wipes the run's bindings | fix | `Maintain` (and `TryRestore`, the release walk) skip a record whose lead is empty: such a record is a card the read made, not a binding of this module. Wiring pin in `test_the_release_reasons...`. |
| Gemini | [critical] `TryRestore`/`RestoreOne` steal bots from other commanders | fix | `TryRestore` only re-joins an UNBOUND member; `RestoreOne` re-checks h3 before each bind and cuts foreign members via `Claim`. Pins in `test_a_restore_never_takes_a_bot_from_another_commander`. |
| Gemini | [high] An in-party binding never puts the commander back into the party | fix | `RestoreOne` adds the commander to the lead's group when the row says `FlagInParty`; the release rule then reads him present. Pin on `group->AddMember(commanderPlayer)`. |
| Gemini | [high] `Save`/`Erase` are independent async statements that can reorder | fix | same as the Codex "lose its row": one synchronous atomic statement per write. |
| Gemini | [high] `Maintain` runs on every world tick | fix | 1 Hz throttle (`kMaintainMs`) before the work; wiring pin asserts the throttle precedes `TryRestore`. |
| Gemini | [high] `ReleaseCommander` leaves the bots' master bound | fix | `ClearBotMaster(bot)` for every released pair, as `.tb master none` does. Pin in the release assertions. |
| Gemini | [medium] `NoteMaster` corrupts `m.lead`/`m.members` | fix | the release path removes only the addressed bots that h3 really released, promotes a released lead to a still-bound follower (and re-points the followers), and keeps offline stored members; a rebind over a pending row replaces it instead of keeping its offline lead. |
| Gemini | [medium] Displaced rows and the order log grow without bound | fix | `PruneToStanding` deletes the row of a card with no bound bot left (it is not re-created after a restart); `ReleaseCommander` retains the log to the standing keys. Pin on `store::Erase` in `PruneToStanding`. |
| Gemini | [medium] `Save`/`Erase` bypass the guard, `EnsureTable` retries DDL, `LoadConfig` never resets | fix | all three: `Writable` before each write, the CREATE only once per config (`tableTried`), `LoadConfig` clears `loadTried`, `tableReady`, the warning and the rows on a reload; `Save`/`Erase`/`Rows` configure first. |
| Gemini | [medium] Tests leave invariants untested | fix | `cmd_binding_life_check.cpp`: the restore round-trips the stored text instead of comparing a copy; every release reason with `FlagRaid`/`FlagPlayerLed`/`FlagSystem`/`groupIsBg`; `Claim`; commander-in-members; a >64-bit member word. `test_cmd_p2_wiring.py`: the persistence, throttle, restore and steal pins above, three new mutations (`commander-member`, `members-overflow`, `claimed-steal`), each must FAIL the check. Limit at the time: the core-facing C++ had no unit test (it needs a live core); the store's own fake-core check was added in the re-check round below. |
| Gemini | [low] `IsCommandedBy` ignores h3 ownership; `Valid`/`DecodeMembers` edges; guard escaping | fix | `IsCommandedBy` requires `h3::CommanderGuid(bot) == commander`; `Valid` refuses the commander inside its own members; `DecodeMembers` drops a word past 64 bits instead of wrapping; `CharacterQueryCompat` refuses any table but this store's own literal, so no name can ever be escaped in. |
| Codex (final round) | [high] Release completes despite persistence deletion failure | fix | `store::Erase` marks a commander whose row survived the DELETE (`pendingErase`); `Rows()` never offers it, `Maintain` retries it once a second (`RetryErases`), a config reload keeps the mark, and `ReleaseCommander` logs `the release is not complete` instead of `released` while still clearing the live binding. `RestoreOne`/`TryRestore`/`LoadBindings` refuse a pending commander; a later `Save` of the same commander clears the mark. `RefuseRow`/`PruneToStanding` get the same store behaviour. Check: `cmd_binding_store_check` `TestAFailedEraseIsPendingNotGone` (the old code fails it at `Rows().empty()`); mutations `erase-forgotten`, `pending-restored`, `retry-deletes-a-rebind`; wiring pin `test_a_failed_delete_is_pending_and_never_restored`. Limit: the mark is in memory (see Limits). |

## Fix round 3 (review of 2026-11-21)

* **[high] the release ran inside `GroupScript::OnRemoveMember`.** `Group::RemoveMember` fires the hook as its first statement (Group.cpp:565), then
  promotes a leader, erases the slot and may `Disband()` (which deletes the group at Group.cpp:786). The release's `BotsLeave` removes every bound
  bot; a commander + lead + bots party drops below two and the outer `RemoveMember` goes on using the freed group. The hook now records the event
  (binding guid, reason, live group counter) and `Maintain` releases it on the next tick (`RunPendingReleases`), re-finding binding and group by guid
  (`life::DeferredRelease`); a group that is gone still ends the binding with its reason but skips the party action, and a binding released or rebound
  meanwhile drops the event (`DropPending` on `.tb master`). Every other group hook was audited and defers for the same reason (see "Which group hooks
  may not release"); `NoteLogin`/`NoteLogout`/`Maintain` run outside a group operation and release directly.
* **The wiring pin is behavioural now.** The string pin that accepted an immediate `ReleaseCommander` inside `NoteGroupRemove` is replaced by: the
  recorded `DeferRelease` (binding + reason + group counter), no release call in any of the four hooks, the drain before the 1 Hz gate, and the pure
  `DeferredRelease` cases. Mutations prove each pin fails on the old shape: an immediate release in the removal or another hook, a missing drain, a
  drain behind the gate, a stale event kept across a rebind. The round-1 crash shape (release in the removal hook) no longer passes.

Gates (module builds slot 4, premerge slot 5; `df` before: `/mnt/tb` 8.7 G, `/mnt/forever` 6.6 G free):

| Gate | Result |
|---|---|
| module ON, `tortoisebots` (`-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=55`) |
| module OFF, `tortoisebots` (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=10`) |
| full `worldserver` link, ON | `CMAKE_RC=0` `MAKE_RC=0`; `[2/2] Linking CXX executable bin/Release/bin/worldserver` (74,912,856 bytes, carries `cmd::(anonymous namespace)::DeferRelease` and `store::RetryErases`) |
| `cmd_binding_check.cpp` / `cmd_binding_life_check.cpp` / `cmd_binding_store_check.cpp` | `57 checks, 0 failed` / `89 checks, 0 failed` / `66 checks, 0 failed` |
| `test_cmd_p1_wiring.py` / `test_cmd_p2_wiring.py` (`CMD_P2_MUTATE=1`) | 12 tests OK / 23 tests OK |
| `tbf-premerge <worktree>` | `merge: clean (5 commits over tbf/main)`; `turtle: PASS`; `slice1-ON: PASS`; `slice1-OFF: PASS`; `PREMERGE: PASS` |

## Fix round 4 (reviews of 2026-11-21, part 2)

* **[medium] an order still used a binding whose release a group hook had recorded.** The four group hooks record the event and `Maintain` drains it
  on the next tick; until then `s.metas` and h3's table still bound the bots. `IsCommandedBy` and `ResolveBinding` now ask the pure gate
  `life::MayCommand(stands, pending, bound, inParty)`: a binding with an event in `s.pending` (`HasPending`) commands nothing, and the bot must still
  be a member of the lead's live group. `ResolveBinding` returns only those bots (a follower whose slot was erased, whose removal hook recorded
  nothing for this commander, is refused at once) and refuses a meta that is not restored. `ReleaseCommander` drops the commander's event
  (`DropPending`), so a hook's event can never end a later binding of the same commander. Core check: `Group::RemoveMember` fires the hook as its
  first statement and `Group::ChangeLeader` before it stores the new leader (ForeverCore `Group.cpp`), so at hook time the change is already true.
* **[medium] the hooks recorded the binding's remembered group counter.** All four `NoteGroup*` hooks now pass `Ctr(group->GetGUID())`, the group the
  core handed them, never `m.groupId` (0 before the 1 Hz refresh, stale after the lead switched groups). `RunPendingReleases` only looks the
  recorded counter up (`GetGroupByGUID`) and never falls back to `ReleaseGroup`, and `life::DeferredRelease` treats a zero counter as
  `WithoutParty`: a recorded event can no longer run `BotsLeave`/`LeadToCommander` on another party.
* **[low] the group-script overrides were outside the pin.** `check_group_hooks_defer` now also inspects `TbScripts.cpp`'s `tortoisebots_group`
  (no `ReleaseCommander`/`ApplyPartyAction`, no `Group` mutator, the events still forwarded), forbids any `Release*` helper in the four hook bodies,
  and pins every `DeferRelease` to the hook's own group counter; the drain fallback and the remembered counter are mutations that fail the pin.
  The pure check covers the gate (`TestMayCommand`) and the zero counter (`TestDeferredRelease`), with the matching mutations
  (`pending-commands`, `left-commands`, `zero-group-releases`).

Gates (module builds slot 6, premerge slot 5; `df` before: `/mnt/tb` 14 G, `/mnt/forever` 6.4 G free):

| Gate | Result |
|---|---|
| module ON, `tortoisebots` (`-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=64`) |
| module OFF, `tortoisebots` (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=28`) |
| full `worldserver` link, ON | `CMAKE_RC=0` `MAKE_RC=0`; `[2/2] Linking CXX executable bin/Release/bin/worldserver` (74,912,840 bytes, carries `cmd::IsCommandedBy`, `cmd::ResolveBinding`) |
| `cmd_binding_check.cpp` / `cmd_binding_life_check.cpp` / `cmd_binding_store_check.cpp` | `57 checks, 0 failed` / `94 checks, 0 failed` / `66 checks, 0 failed` |
| `test_cmd_p1_wiring.py` / `test_cmd_p2_wiring.py` (`CMD_P2_MUTATE=1`) | 12 tests OK / 24 tests OK; the cohort's `test_cmd_p3`..`p6` and `test_place_wiring.py` unchanged and OK |
| `tbf-premerge <worktree>` | `merge: clean (7 commits over tbf/main)`; `turtle: PASS`; `slice1-ON: PASS`; `slice1-OFF: PASS`; `PREMERGE: PASS` |
