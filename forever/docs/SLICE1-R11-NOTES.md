# Slice 1, package R11: the crash when a master changes maps (findings row S7)

Branch `slice1/integ` (worktree `~/wt-slice-integ`), default build slot. T = Turtle core, F = pinned ForeverCore. Input: `FOREVER-SLICE1-FINDINGS.md` row S7, R10 notes section 4 ("A crash that is not R10's").

## 1. The crash, under gdb

Reproduced on the first boot of the new binary, with the module built with `-g` (new CMake switch `-DTORTOISEBOTS_DEBUG_INFO=ON`, module objects only, option OFF by default). Two engine warriors (Tbalpha, Tbgamma) following Tbbeta; `tele name Tbbeta Darnassus` on the console:

```
Thread 1 "worldserver" received signal SIGSEGV, Segmentation fault.     fault address 0x8 (si_addr), si_signo 11, rax = 0 (the null pointer)
#0  std::future<std::map<unsigned, std::vector<std::tuple<ai::TravelDestination*, ai::WorldPosition*, float>>>>::operator=(future&&)   (inlined, move assignment: future(std::move(__fut))._M_swap(*this))
#1  PlayerbotAI::Reset (this=0x73df2a034800, full=true)   ai/playerbot/PlayerbotAI.cpp:2719
#2  PlayerbotAI::DoNextAction (this=0x73df2a034800)       ai/playerbot/PlayerbotAI.cpp:3504
#3  PlayerbotAI::UpdateAIInternal (elapsed, minimal=false)  PlayerbotAI.cpp:2567
#4  PlayerbotAI::UpdateAI (elapsed=2)                     PlayerbotAI.cpp:1045
#5  TortoiseBots::engine::Tick (diff=2)                   forever/src/host/TbEngineBrain.cpp:217
#6  TortoiseBots::BotMgr::Update (diff=2)                 forever/src/TbBotMgr.cpp:618
#7  tortoisebots_world::OnBeforeMapsUpdate               forever/src/TbScripts.cpp:119
#8  ScriptMgr::OnWorldBeforeMapsUpdate  #9 World::Update  #10 WorldUpdateLoop  #11 main
```

## 2. Root cause

`PlayerbotAI.cpp:2719`: `*AI_VALUE(FutureDestinations*, "future travel destinations") = FutureDestinations();` inside `Reset(true)`. The key `future travel destinations` was not registered by the slice, so the missing-key guard (`SliceMissingKeyGuard.h`) answered with its inert stand-in, whose `Get()` for a pointer type is `nullptr`; the assignment wrote through it (the `std::future` move assignment swaps into `this` at offset 8: fault address 0x8). The log line said so one line before the crash: `slice: missing value future travel destinations`.

Why a map change reaches it: `DoNextAction` runs the master-adoption step (`PlayerbotAI.cpp:3470-3505`): when the master leaves the world for a far teleport, `UpdateAI` nulls its cached `master` (`:437-442`, `FindPlayer` misses it); once the master is in the world again on the other map the step finds the party leader (`GetGroupMaster`), sees `master != newMaster`, rebinds (`BindBotMaster`) and calls `Reset(true)` + `ResetStrategies()` (`:3499-3504`). On Turtle the same sequence runs and `Reset(true)` works (the value exists). A same-map teleport never nulls the master, so it never got there. Every other full reset hit it too: the `reset` chat command (`:3100`), `UnstuckAction`.

## 3. The fix

1. **Register the value** (the real fix): `future travel destinations` joins the generator list (`forever/tools/slice_gen_context.py`), `SliceAiObjectContext.cpp` regenerated (141 values, `FutureTravelDestinationsValue` with the full build's creator text and its `TravelValues.h` include). `Reset(true)` now does what it does on Turtle.
2. **A stand-in cannot be a null write again** (`SliceMissingKeyGuard.h`): the one pointer type a caller WRITES through, `FutureDestinations*`, has a specialised stand-in that points at an owned empty future instead of `nullptr`. The other pointer stand-ins stay null (`Unit*`: callers test it; `LootObjectStack*`: its slice stubs, `SliceLinkStubs.cpp:314-317`, ignore `this`).
3. **A follower that cannot reach its master** (`TbBotMgr.cpp` `Think`, `TbBotMgr.h`): see section 5.

### Audit of `Reset`'s callee set (the null-stand-in pattern)

`Reset(false)` and `Reset(true)` read these values: `old target`, `current target`, `pull target`, `attack target`, `explicit attack target`, `rpg target`, `combat start time`, `last movement`, `last spell cast`, `ignore rpg target` (all registered); `last area trigger`, `last taxi`, `travel target`, `manual string`, `manual int` (registered; `TravelTarget`/`TravelMgr::SetNullTravelTarget` are stubs in `SliceLinkStubs.cpp:325-352`, no dereference of a null); `loot target` (**not registered**: `RESET_AI_VALUE` on the stand-in is a no-op, harmless, the loot stack is out of the slice); `future travel destinations` (**the crash**, now registered). Other callees: `ClearStaleTransportStateCompat`, `PullStrategy::Get`/`OnPullEnded`, `InterruptSpell`, `StopMoving`, `ResetJumpDestination`, `CancelLogoutCompat`, `engines[i]->Init()`, `ResetStrategies` ran live in the adoption path without a fault (checks a and b below). The pointer-typed stand-ins that exist (registry check, `slice_registry_check.py`): `LootObjectStack*` (`available loot`, 22 call sites, 3 in slice-compiled files: `->Add` on a stubbed method that ignores `this`), `Unit*` (6 keys, callers null-test), `FutureDestinations*` (this crash). Nothing else is written through.

## 4. What Turtle does and what the bots do now

On Turtle a bot of a real player follows its master through a map change by these paths (none is "the engine walks to a master on another map"): the dungeon catch-up (`PlayerbotAI.cpp:3544-3556`, `PlayerConvenience::RequestSummon`, every 20 s, for a real master in the party with the follow strategy), the summon when the master leaves a dungeon (`host/BotPlayerAdapter.cpp:155-196`), the portal follow (`:3640-3730`), and for hires `HireProvisionService::Reunite` (`runtime/HireProvisionService.cpp:1426-1460`) / `HireLifecycle::Reunite`: a plain `TeleportTo` to the master's map and position. F's host does the same thing in `BotMgr::Think` (R8/R10): when the bot is on another map from its master it clears its movement and `TeleportTo`s the master (every 500 ms Think; the engine's `HandleTeleportAck` follows, R10 S3). R11 keeps that and adds the instance id (the master's own copy) and the refusal handling.

## 5. A master in an instance the bot cannot enter

`Think` now asks `Map::PlayerCannotEnter(masterMap, bot)` (the check `Player::TeleportTo` makes, `Entities/Player/Player.cpp:1432`; `Maps/Map.cpp:1829-1875`) before the teleport, and passes `master->GetInstanceId()` for an instance. When the core refuses (or `TeleportTo` returns false): the bot stays where it is, its engine is taken down (live, with the engine running the follow action asked for a path to a point on another map at every tick: 395 `[BOT PATH BUG]` error lines a minute for two bots; with the engine down: 0), `BotMgr: <bot> cannot follow its master ...: <reason>` is logged once per (map, instance, reason), and `.tb where` prints `follow: blocked, the master is on map N (instance I) and the bot cannot go there: <reason>; asks again in S s or when the master changes map`. It asks again after 30 s (`FollowRetryMs`) or at once when the master is on another map or instance; it clears when the master is on the bot's map again. Test helper `.tb enter <character> <mapid>` prints the core's answer and the difficulty it used.

## 6. Live evidence (VM `yulon-ubuntu2`, `~/forever-run`)

Boots: the shared server was in use by the other worker at several moments; I waited for a free window each time with a script that boots only after `forever-world` has been absent for 20 s and installed my binary first (`bin/worldserver` is my build as I leave; the other worker installs its own before its boots). All runs under gdb (`-batch`, bt on a fault). Characters as before: Tbbeta = master (bot session), Tbalpha and Tbgamma = engine warriors, party formed by `tb login <bot> Tbbeta`.

**(a) another continent.** Ironforge to Darnassus, `tele name Tbbeta Darnassus`, 12 s later: Tbalpha map 1 `distance to master target: 2.1, motion type: 14`, Tbgamma `1.8, 14`, `teleport acks handled by the engine: 1, transition generation: 1`, no `SIGSEGV` in the log (crashed run: `bt` above). `tb go Tbbeta 15 0`: both walked with it.
**(b) back.** `tele name Tbbeta Ironforge`: both on map 0 within 12 s, acks 2, follow 1.5 / 1.4 yd; again after a 15 yd walk.
**(c) an instance.** Findings in order:
1. `tele name Tbbeta RFC|SFK|WC|Deadmines` (maps 389, 33, 43, 36): the **core refuses the master itself**, silently (`tele` prints "You are teleporting" and nothing happens). `Map::PlayerCannotEnter` returns `TRANSFER_ABORT_DIFFICULTY` (8): `Maps/Map.cpp:1841-1843`, `GetDownscaledMapDifficultyData(mapid, 1)` is null: this server's DB2 `MapDifficulty` data has no row for a 5-man dungeon at difficulty 1 (`hotfixes.map_difficulty` has 11 rows, none for these maps). Read with gdb (return value 8 of `PlayerCannotEnter`) and with `.tb enter`: `Tbbeta`, `Tbalpha` identical, party or alone, for maps 389, 36, 43, 33, 34; maps 0 and 1 "allowed". A real player would meet the same refusal: it is the core's data, not the session type. So no 5-man dungeon can be entered on this test server as it stands.
2. To get a data point anyway I added two test rows to `hotfixes.map_difficulty` (`Message = 'R11-TEST'`, IDs 90000389 and 90000036, difficulty 1, 5 players; **deleted afterwards**, table back at 11 rows) and restarted. Then `tele name Tbbeta RFC`: the master entered Ragefire Chasm (map 389); **both engine bots entered it within 15 s**, in the master's own instance: `Tbalpha map 389 at 2.1 -13.2 -16.2`, `acks 1`, strategies became `arms raid`, `follow target: Tbbeta, distance 0.6, motion type: 14` (Tbgamma 0.3); `tb go Tbbeta 6 6`: both followed (2.8 / 3.1 yd); `tele name Tbbeta Ironforge`: both back on map 0 within 12 s. **So a bot session can enter and leave an instance through `Player::TeleportTo` and the engine follows in it.**
3. The refusal path for the bots, with the row for RFC changed to `MaxPlayers = 1` (the master enters alone, the bots get `TRANSFER_ABORT_MAX_PLAYERS`, `InstanceMap::CannotEnter`): `BotMgr: Tbalpha cannot follow its master Tbbeta to map 389 (instance 1): the instance is full (TransferAbortReason 2); it stays and asks again in 30 s` (once per bot, no more lines in 40 s); `.tb where Tbalpha`: `brain: spike (engine requested, waiting for the bot to be placed next to its master)` and `follow: blocked, ... the instance is full (TransferAbortReason 2); asks again in 19 s ...` then 12 s (the retry timer runs, no error lines). Before the engine drop: 395 `[BOT PATH BUG]` lines a minute. When the master left the instance the bots were with it within 12 s and `follow: blocked` was gone.
**(d) the R10 checks.** Follow after a far walk (above). Assist: `tb attack Tbbeta` (Kobold Worker): Tbalpha `in combat, victim Kobold Worker, in melee range: yes`, 14 s later `state: Non Combat`, `follow target: Tbbeta, motion type: 14`; Tbgamma the same. (An earlier fight against a Defias Thug killed both level 2 warriors: not a fault, `.revive`d; they were `Non Combat` and following again.) Master away and back: `tb logout Tbbeta` -> `brain: none (detached)`, `master away, 296 s left`; `tb login Tbbeta` 12 s later -> `the master of ... is back: the bot follows again`, `joined the party of Tbbeta (3 member(s))`, brains built again, `follow ... distance 0.0 / 1.6, motion type 14`.

## 7. Gates (final commit `82ec6bbb3` plus this file)

| Gate | Result |
|---|---|
| Slice census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`) | `files 133  compile clean 133  with errors 0  error places 0` |
| Option ON `worldserver` build (`-DTORTOISEBOTS_SLICE1=ON`, default slot) | `CMAKE_RC=0 MAKE_RC=0` |
| Option OFF module build (`-DTORTOISEBOTS_SLICE1=OFF ... build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, the same 6 objects (`TbBotMgr`, `TbBotSession`, `TbScripts`, `compat/TbItemPrototype`, `compat/TbSpellEntry`, `host/TbEngineBrain`), 0 `build.ninja` entries for `forever/src/slice`, `host/TbHost`, `ai/playerbot` |
| `slice_gen_context.py --check`, `slice_registry_check.py` | file current; `(a)=0  (b)=163` (was 164: the registered key) |
| Turtle `vm-build` / `vm-test tb` | not run: only files under `forever/` and the root `CMakeLists.txt` changed, and that file is ForeverCore-only (its own header: "The Turtle build does not read it") |
| The server on the build VM | stopped after every boot of mine; `docker ps` shows no `forever-world`; `forever-test1` untouched; test rows deleted from `hotfixes.map_difficulty`; Tbbeta's saved position put back to Ironforge (it was saved inside the instance); `groups` table empty |

## 8. Different answers and things to know

* **R11-1 far-teleport reset.** After a master's far teleport the engine rebinds the master and runs `Reset(true)` + `ResetStrategies()` (as on T); the strategy set changes by the map kind (`arms pve` in the open world, `arms raid` in the dungeon, from `ResetStrategies`).
* **R11-2 instance entry.** F's host sends the bot into the master's own instance id (T's catch-up uses `RequestSummon`). On this server no 5-man dungeon is enterable without the map-difficulty rows (section 6, c1): a data fact for the core's author, not a module bug.
* **R11-3 blocked follower.** A bot whose master is on a map it cannot enter has no engine until the block ends; T keeps the engine (and has the summon retry every 20 s).
* `.tb enter` is a new console helper (like `hop`, `raid`); the lead may prefer it out of the shipped build.

## 9. What I am unsure about

* The map-difficulty finding was read through the core's code and `.tb enter`; the DB2 file itself was not decoded. With a real client the same refusal would show as "difficulty not available".
* The instance entry was tested with two test rows and one map (Ragefire Chasm); an instance with scripts, a lock (`LockID`) or a raid group may behave differently.
* An incident: after one of my first boots (a rebuilt binary I thought I had installed) the other worker's server was the one running, and four of my console lines (`tb login ...`, `tele name Tbbeta <dungeon>`) were typed into its console before I noticed. The four `tele name Tbbeta <dungeon>` lines had no effect (the core refused them, section 6 c1); `tb login Tbbeta`, `tb login Tbalpha Tbbeta`, `tb login Tbgamma Tbbeta` and `tb engine Tbalpha|Tbgamma on` ran in its session: the first two characters were logged in there already (its log shows `tb login Tbalpha Tbbeta` just before), whether `Tbgamma` was logged in or engine-marked by my lines I cannot tell. I stopped sending anything until I could boot my own, and boot only through a script that waits for a free window from then on.
