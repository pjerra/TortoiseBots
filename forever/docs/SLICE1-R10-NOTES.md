# Slice 1, package R10: the last fix round (re-review findings S1 to S6)

Branch `slice1/integ` (worktree `~/wt-slice-integ`), default build slot. T = Turtle core, F = pinned ForeverCore. Input: `FOREVER-SLICE1-FINDINGS.md`,
"Re-review on 28b050e9d". Only `forever/` changed (`git diff --stat 28b050e9d HEAD` is in the gates).

## 1. What changed

| Row | Reason (short) | Where |
|---|---|---|
| S1 | `Group::IsFull` is the raid's 40 for a raid, so the host added a bot to a raid; and a group the call had just made stayed saved with one member when `AddMember` failed. Now: a master that leads a raid is refused (at `Login`, before a session is made, and in `JoinParty`, also for a bot already in that raid); a master that is not the leader is refused; the five are counted against `MAX_GROUP_SIZE` with `GetMembersCount()`, not through `IsFull`; and a group this call created (`createdHere`) is taken down with `Group::Disband(true)` (it removes the saved rows, the group manager's entry and the group) when `AddMember` refuses, with the leader flag cleared. A group the master already had is left as it was. | `TbBotMgr.cpp:92-100` (Login), `:130-200` (JoinParty, disband at `:186-197`) |
| S2 | A master's logout logged every bot out at once; the original keeps hired bots through the disconnect grace (`HireLifecycle::SweepGracePeriod`, `runtime/HireLifecycle.cpp:781-896`). Now `OnPlayerLogout` DETACHES the bots of the leaving master: `DropBrain` (the engine's master pointer goes with the brain), the bot stays in the world and in its party, keeps the master's guid, stops moving and attacking, does no Think and no engine tick. Each update `UpdateMasterAway` counts the grace down and looks for the master: when the master is in the world again (after it was seen gone once, the logout itself runs in the same update) the bot is re-bound, placed next to it again, and the engine is built again by `Think`; when the grace is spent the bot is logged out. A party that does not hold the master any more (a bot master leaves its party when it leaves the world) is left and the master's own is joined (`wantParty`). `.tb where` prints `brain: none (detached)` and `master away, N s left`. A bot that is already logging out (server shutdown, the account owner logging in) is not detached. | `TbBotMgr.cpp:44-50` (config), `:308-374` (DetachFromMaster, UpdateMasterAway), `:377-402` (OnPlayerLogout), `:596-599` (Update), `:640-646` (Think), `:280-286` (DescribeBrain); `TbBotMgr.h:88-104` |
| S3 | A short same-map teleport was acknowledged inside `BotSession::Tick` before the engine's tick could see `IsBeingTeleported()`, so `PlayerbotAI::HandleTeleportAck` never ran: `transitionGeneration` and the teleport grace were not updated, stale queues not drained, the landing logic did not run. Now the session has a hook (`SetTeleportAckHook`, set by `BotMgr::Login`) that calls `engine::HandleTeleportAck` (`TbEngineBrain.cpp`, behind the slice macro) for a same-map teleport BEFORE the session acknowledges it: the engine acknowledges it itself through `AckNearTeleportCompat`, as it does on Turtle (`runtime/BotManager.cpp:2390-2406`), and its landing logic reads `IsBeingTeleportedNear()`; the session then acknowledges only what is still pending. For a far teleport the hook runs AFTER the session completed it (the Player is in the new world; `HandleTeleportAck` needs a Player in the world). Without a brain nothing changes. Counter: `.tb where` prints `teleport acks handled by the engine: N, transition generation: G`. | `TbBotSession.cpp:65-105`, `TbBotSession.h:33-39`, `TbBotMgr.cpp:79-84`, `host/TbEngineBrain.cpp` (`HandleTeleportAck`, `Describe`), `host/TbEngineBrain.h` |
| S4 | `CmangosLogProxy` formatted every line (two `snprintf` passes, string copies) before the logger dropped it; `Engine::LogAction` sends one `outDetail` per action per tick per bot. The level is now asked first (`Log::ShouldLog`) in `Emit`, which every formatting method uses. ForeverCore side only (`forever-compat-shim.h`); the engine's own code is untouched. The arguments are still evaluated by the caller. | `forever/src/compat/forever-compat-shim.h:476-509` |
| S5 | `WorldPosition::loadMapAndVMap` asks `NavMeshLoadTileCompat` again for a missing tile on every `getMap`/`getHeight`/path query (`AddBadMmap` is commented out, `WorldPosition.cpp:1254-1255`), each time building the file name and calling `MMapManager::loadMap` (a `fopen`). A failed `(map, x, y)` whose answer names the FILE (`FileNotFound`, `VersionMismatch`, `ReadFromFileFailed`) is remembered in a bounded set (16384 keys, a mutex) and answered `false` without the load; `LibraryError` is not remembered. No shared-code change. | `forever/src/compat/TbCompat_C.cpp:78-114` |
| S6 | `ExcludeSteepSlopesCompat` stays a no-op: note below. | this file, sections 3 and 4 |

Test helpers added with the rows (console only, GM): `.tb hop <bot> <dx> <dy>` (a same-map teleport, for S3) and `.tb raid <master>` (`Group::ConvertToRaid`, for S1), `TbScripts.cpp:272-291`, `TbBotMgr.cpp` (`Hop`, `ConvertToRaid`).

### The grace period

New key `TortoiseBots.Slice.MasterGraceSeconds` (`conf/tortoisebots.conf.dist`), **default 300 s**, 0 = the bots log out with their master at once, capped at 3600 like the original. The default is the
hire lifecycle's own: `AiPlayerbot.HireDisconnectGracePeriod` defaults to 300 (`ai/playerbot/PlayerbotAIConfig.h:621`, read with `GetIntDefault(..., 300)` at `PlayerbotAIConfig.cpp:1050`, capped at 3600 at
`:1051-1052`) and is what `HireLifecycle::SweepGracePeriod` (`runtime/HireLifecycle.cpp:781`, expiry at `:891-896`) and the regroup bounds (`:370`, `:386`) read. The engine's own key is not read by the host (the host
must work when the engine is not built in).

## 2. Different answers (new)

* **R10-1 master away.** On T a hire whose master disconnects is kept by the lifecycle's ledger (`masterOfflineSince`), the bot guards in place, and is dismissed at the grace's end. On F the host detaches the bot (no brain, no
  movement, no attack, in its party) and logs it out at the grace's end: same shape, but the engine is torn down and rebuilt (its queues and values start fresh when the master is back), and no hire ledger exists on F.
* **R10-2 party shape.** The host forms 5-man parties only; raids are refused (R8-4 stays).
* **R10-3 failed navmesh tiles.** F remembers a missing or unreadable tile for the life of the process (T asks again each time). A tile file that appears while the server runs is not seen until a restart.
* **R10-4 far teleports.** On T the engine acknowledges a far teleport itself and then runs `HandleTeleportAck`'s far branch (a 2 to 5 s AI delay). On F the session completes it and the engine hears of it afterwards: the bookkeeping, `Reset()` and the generation bump run, the AI delay of the far branch does not.
* **S6 steep slopes (this finding).** `ExcludeSteepSlopesCompat` permits steeper paths than T: T excludes steep polygons from a plan, F's `PathGenerator` filter is private and has only a slope penalty. Known, not fixable from the module
  (R9 notes section 1, row 655); `NavExcludeFlagsCompat` still excludes steep ground for the corrected-point query.

## 3. Requests for the core's author

1. A public `PathGenerator` way to exclude a polygon flag (or the steep-slope area) from one plan, so the module can do what `PathFinder::ExcludeSteepSlopes()` does on T (S6).
2. A public getter for the session's kick (`bool WorldSession::IsKickRequested() const { return forceExit; }`, `forever/docs/SPIKE.md` "The one gap: a kick is invisible"): `KickPlayer` on a bot session only sets the private `forceExit`, so a GM `.kick` or ban of a bot is not seen and
   the bot stays; the module can only log it out by name (`.tb logout`) or when the account owner logs in (watched through the real session).

## 4. Live evidence

The shared test server (`~/forever-run`, VM `yulon-ubuntu2`) was running another worker's `forever-world` when the code and the builds were done. It was not touched; I checked every 30 s from 19:45 and it was free at 20:09 (CET). Two boots
of mine, both stopped, binary and conf put back as found. Characters as before (Tbbeta priest = master, Tbalpha and Tbgamma warriors = engine bots) plus three new level 1 warriors for the party tests (Tbdelta, Tbepsilon, Tbzeta: `.tb create 1#1 <name> 1 1`;
they stay in the characters table). All bots are console-driven bot sessions on map 0 (Northshire). Grace set to 30 s in the VM's conf for the test (`TortoiseBots.Slice.MasterGraceSeconds = 30`).

**(1) A raid.** Tbbeta leading Tbalpha and Tbgamma (`groups`: `1 2 0 3` = id, leader, type 0 party, 3 members), then `.tb raid Tbbeta` (`1 2 2 3`: type 2 = raid):
```
tb login Tbdelta Tbbeta   -> TortoiseBots: Tbdelta not logged in: the master leads a raid: raids are not in this slice, the bot is not logged in.
tb login Tbdelta          -> bot Tbdelta entered the world on map 0        (no master)
tb group Tbdelta Tbbeta   -> TortoiseBots: Tbdelta not grouped: the master leads a raid: raids are not in this slice, the bot was not added.
groups after the refusals: 1 2 2 3     (the raid is unchanged: 3 members)
```
**(2) Full party, no orphan.** Tbbeta + Tbalpha, Tbgamma, Tbdelta, Tbepsilon = 5 (`1 2 0 5`). The sixth character, Tbzeta:
```
tb login Tbzeta Tbbeta  -> not logged in: the party of that master is full (5 members at most, raids are not in this slice).
tb group Tbzeta Tbbeta  -> not grouped: the party is full (5 members at most, raids are not in this slice).
groups after: 1 2 0 5      (one group, five members, nothing new saved)
```
The `AddMember` failure path itself (the disband of a group the call just made) was NOT forced: ForeverCore's `Group::AddMember` fails only for a raid whose subgroups are all full, and a raid is refused before (check 1), a full party before that. The path is by reading only (`Group::Disband(true)` clears the `groups` and
`group_member` rows, `Group.cpp:722-770`). What the live run shows is that every refusal leaves the saved groups as they were, and after the last logout the `groups` table was empty (no orphan).

**(3) Master away.** Party of five (Tbalpha, Tbgamma on the engine, Tbdelta, Tbepsilon on the spike behaviour), master Tbbeta (a bot session) logged out with `tb logout Tbbeta`:
```
bot Tbbeta left the world
BotMgr: the master of Player-70-00000006 / -05 / -03 / -01 is away: the bot stays and waits 30 s for it       (4 lines)
tb where Tbalpha -> brain: none (detached) / master away, 27 s left          tb where Tbdelta -> brain: none (detached) / master away, 26 s left
tb status -> enabled, 4 bot session(s).           (the bots are still in the world; the party, led by a bot now: 4 members)
```
About 20 s later `tb login Tbbeta` (no master named):
```
bot Tbbeta entered the world on map 0
BotMgr: the master of Player-70-00000006 / -05 / -03 / -01 is back: the bot follows again                     (4 lines)
BotMgr: Tbepsilon joined the party of Tbbeta (2 member(s)) ... Tbalpha joined the party of Tbbeta (5 member(s))   (they left the party without the master and joined the master's)
TortoiseBots engine: brain of Tbgamma built / PlayerbotAI attached for Tbgamma ... master Tbbeta   (and Tbalpha)
```
`groups`: `1 2 0 5`. Follow after the re-bind (`tb go Tbbeta 15 0`, 9 s later): Tbbeta at `-8677.4 -74.5`, Tbalpha `-8678.1 -71.4`, Tbgamma `-8678.0 -74.3`; `master: Tbbeta (no network session)`, `follow target: Tbbeta ... distance to master target: 1.6, motion type: 14`.
Then `tb logout Tbbeta` again and no login: `tb where Tbalpha` after 20 s: `master away, 7 s left`; after 40 s:
```
BotSession: logout asked for Player-70-00000006 / -05 / -03 / -01 (the master did not come back within the grace period)
bot Tbepsilon / Tbdelta / Tbgamma / Tbalpha left the world        tb status -> 0 bot session(s);  groups table: empty
```
A real player as master (the core keeps a logged-out member in the saved party) was not possible on the VM (no network client): with a bot master the party is left by the master's own logout (R9) and rebuilt on its return, as above. The `IsMember` test that skips that step for a real master was not exercised.

**(4) Teleport handler.** Two engine bots, Tbalpha hopped with `.tb hop Tbalpha 10 0` (a same-map teleport; `.tb where` counters):
```
before:  teleport acks handled by the engine: 0, transition generation: 0
after:   teleport acks handled by the engine: 1, transition generation: 1        (second hop -6 4: 2 and 2)
```
and the bots' own placement teleport next to a master `.tele`d to Ironforge (same map, far away): 3 and 3. Before R10 both counters did not exist, `HandleTeleportAck` was never called for a host teleport. The engine's landing logic ran with the ack (`not ported yet: movement interrupt and heartbeat`: the two legacy calls it makes, `InterruptCurrentMovementCompat` and `SendHeartBeatCompat`, are in the unported table).

**(5) `.tb perf`, two engine warriors following a bot master, 52,000 to 55,000 world ticks per window.** Idle windows, 60 s: before S4 (R10 without the header change) avg 25 and 24 us, worst 3,095 and 5,570 us; after: avg 25 and 26 us, worst 781 and 2,256 us. Fight windows (four `.tb attack Tbbeta` in 60 s, the fights differ each time): before avg 187 us (worst 16.8 ms); after avg 222 and 198 us (worst 26.7 and 5.0 ms).
World update time avg 1.12 to 1.13 ms in all. So **S4 has no effect that `.tb perf` can see** at two bots: the formatting it removes is a few microseconds per second (a detail line per action, the engine decides about ten times a second per bot); the saving scales with the number of bots and with
`Logger.server` below detail only. It is kept because it is free and correct, not because it was measured. (The "before" binary is the same code as the "after" one with only `forever-compat-shim.h` taken from `28b050e9d`; ccache made the rebuild take 40 s.)

**(6) Follow, assist, after combat** (once each, with the final code, after the re-bind): follow as in (3); assist `tb attack Tbbeta` -> `Tbalpha ... in combat, victim Kobold Worker (health 45)`, `state: Combat`, `target reach: distance 0.499827 yd, melee range 5.000000 yd, in melee range: yes`; 15 s later `state: Non Combat`,
`follow target: Tbbeta, safe: yes, distance to master target: 0.5, motion type: 14` (Tbalpha) and `1.9` (Tbgamma).

**A crash that is not R10's: a far teleport of the master.** `tele name Tbbeta Darnassus` (the master goes to map 1, the bots follow through `Think`'s teleport) kills the server: SIGSEGV on the world thread in
`PlayerbotAI::Reset(bool) <- DoNextAction <- UpdateAIInternal <- UpdateAI <- engine::Tick <- BotMgr::Update` (gdb, `world-gdb.sh`). The same crash with the binary from before R10 (the VM's `worldserver` of 18:24, `bin/worldserver.pre-r10` here), so it is older than this round and none of S1 to S5 touches it.
Not diagnosed (no symbols in the batch backtrace). It also means the far branch of S3 (the engine hears of a far teleport after the session completed it) could not be proved live: it is compiled and read, not run. The server container was gone after each crash and the leftovers were removed (the saved group and the online flags of the three characters).


## 5. Gates (final commit)

| Gate | Result |
|---|---|
| Slice census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`) | `files 133  compile clean 133  with errors 0  error places 0` |
| Option ON `worldserver` build (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON ~/forever-build/build.sh 12 worldserver`, default slot) | `CMAKE_RC=0 MAKE_RC=0` (a first run of the default slot's build dir was still `TORTOISEBOTS_SLICE1=OFF` from R9 and built the OFF configuration: also green, `worldserver` link included; the ON one is the gate) |
| Option OFF module build (`-DTORTOISEBOTS_SLICE1=OFF ... build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, the same 6 objects as R9 section 4 (`TbBotMgr`, `TbBotSession`, `TbScripts`, `compat/TbItemPrototype`, `compat/TbSpellEntry`, `host/TbEngineBrain`); `build.ninja` has 0 entries for `forever/src/slice`, `host/TbHost`, `ai/playerbot` |
| `slice_gen_context.py --check`, `slice_registry_check.py` | file current; `(a)=0  (b)=164` (unchanged) |
| Turtle `vm-build` / `vm-test tb` | not run: `git diff --stat 28b050e9d HEAD` lists only files under `forever/` (10 files, 317 insertions, 20 deletions) |
| The server on the build VM | not running after my boots; `docker ps` shows `forever-bnet`, `forever-db`, no `forever-world` of mine; `forever-test1` untouched; the VM's `bin/worldserver` and `worldserver.conf.d/tortoisebots.conf` put back byte for byte |


## 6. What I am unsure about

* **S4 is not measurable at two bots** (section 4, check 5): kept as cheap and correct; no benefit is claimed.
* **The far-teleport hook is untested** because the far teleport crashes older code (section 4). The order differs from the near case on purpose (after the session's own ack, because `Reset()` and the bookkeeping need a Player in the world); the far branch's 2 to 5 s AI delay is lost (R10-4).
* **A real player as master** was not tested (no network client on the VM): the party-keeping branch (`IsMember` true, nothing to do) is read, not run. What the core does with a party whose only online members are bots after the leader's logout was not looked at.
* **The `AddMember` failure disband** is read, not forced (check 2).
* **`.tb hop` and `.tb raid`** are two new console helpers (GM, console) in the production module; the lead may prefer them out of the shipped build.
* **S5 remembers a missing tile until a restart**; a mmtile file copied in while the server runs is not seen.

