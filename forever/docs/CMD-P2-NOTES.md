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

Answer (live, see the live section): **yes**. `BotMgr::JoinParty` forms a bot party through the public `Group` API (`Group::AddMember`), and the core
calls `sScriptMgr->OnGroupAddMember` from inside it (Group.cpp:506, the same for removal :565, leader :690, disband :724). The module's GroupScript row
sees `.tb party`, `.tb group`, `.tb ungroup`, a kick and a disband.
