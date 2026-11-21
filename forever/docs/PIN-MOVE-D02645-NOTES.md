# Pin move 25fc7ca -> d02645dd21 (0.1.39), 2026-11-20

Branch `pin/d02645dd` off `tbf/main` (af248eff2). The core adds `WorldSession::IsKickRequested()` (#370; `forceExit` is now a
`std::atomic<bool>`) and three module hooks: `InstanceScript::GetBossStates()` (#372), `World::IsInWorldThread()` (#373) and
`WorldScript::OnAfterReload(std::string_view)` plus `ScriptMgr::OnAfterReload`, called after any successful `.reload` (#374);
`doc/NewClientBuild.md` comes with it. 10 source files, 311 lines in the range (plus the doc page). The core was read from a
separate worktree (`git -C ~/ForeverCore worktree add ~/ForeverCore-d02645dd d02645dd21`, sparse like `~/ForeverCore`: no
`sql/`; read it with `git show`). `~/ForeverCore` stays on 6517af05. Commits: the pin (`forever/PINNED_CORE` only), then the
cite refresh, then the two module changes (kick poll, reload hook) with their docs.

## 1. What the new core pieces change for the module

### 1a. A kick is visible: `IsKickRequested()` (#370)

`WorldSession::KickPlayer` sets `forceExit` and, for a bot session, nothing else; with no socket the session's own `Update`
never takes the expire branch, so a GM `.kick`, a ban or any other `KickPlayer` left the bot in the world (`SPIKE.md`, "The
one gap: a kick is invisible"). `BotSession::Tick` now reads `IsKickRequested()` (WorldSession.h:1104) before the base
`Update` and asks for the same logout a console `.tb logout` runs, so the bot leaves and `BotMgr::Update` reaps the session
on the next tick (`RequestLogout` -> `LogoutPlayer(true)`). The reason string is not kept, so the log line names the request
only.

The kick poll normally wins: the core sets the kick flag at once, so `BotSession::Tick` sees it first. The account-owner
workaround (`BotMgr::Update`, `ownerLoadingMs` >= 2 s -> logout) is the fallback: it works whether or not the core ever kicks,
so both remain.

### 1b. `.reload` answered: `OnAfterReload` (#374)

The hook fires after a `.reload` that succeeded (in game, console or remote; `ChatCommand.cpp:318`), with the command path
("reload all", "reload config", "reload creature_template", ...). The module's DB-fed caches, and what the hook does:

| Cache | Read from | A core `.reload` changes its source? | Hook |
|---|---|---|---|
| Dungeon plans (`DungeonPlanTable`), boss tactics, the plan's row facts | world DB `ai_dungeon_plan`, `_leg`, `_step`, `_leg_need`, `_boss`, `ai_boss_tactic` | no (the module's own tables) | **reloaded on `reload all`** |
| Step unit rows (`ai_dungeon_step_unit`) and the spawn index (`map`, `entry` -> spawns) | world DB | no | **reloaded on `reload all`**, with the plans, all or nothing |
| Plan check cache (newest 16 per plan/map/instance) | the two above | no | dropped by the unit install |
| Commander bindings (`tb_commander_binding`) | character DB | no (`.reload all` reads no character table) | nothing |
| Item mirror (`TbItemPrototype`), spell mirror (`TbSpellEntry`), talent rows | DB2 stores | no (the core never reloads a DB2 store: `PORT-PLAN.md` rule 3) | nothing |
| Dashboard item file | the item mirror at boot | no | nothing |
| Engine config (`aiplayerbot.conf`) | file | `reload config` calls `WorldScript::OnConfigLoad(true)`, which the module already answers | already handled there |
| Table of unported features | compile time | no | nothing |

`reload all` is the operator's "read the database again", so that is the one command the module answers: it runs the same
all-or-nothing path as `.tb plan reload` (`ai::ReloadDungeonPlans` with `FillUnitReloadHost` and `PlansInUse`), on the world
thread, and logs one line (swapped: plans, steps and unit rows; refused: the loader's reason and "kept"). A plan edited in
the database is then in use without the extra command; nothing else is touched. `reload config` is not doubled: its
`OnConfigLoad` already runs, before the hook.

### 1c. Public boss states (#372) and the world-thread check (#373)

- **Boss states.** The port's commander-side feeds (branch `cmd/p4`, not on `tbf/main` yet) read a map instance's boss
  states through `InstanceScript::GetBossState(boss)` once per boss index, 0..`GetEncounterCount()-1`, plus
  `GetBossDungeonEncounter(boss)` to name it (`.tb instance info`, `TbCmdFeeds`); the states of one instance therefore cost
  one call per boss per feed fill. `GetBossStates()` (InstanceScript.h:290) returns every state in boss id order
  in one call, so a feed fill reads the vector once and pairs the values with the encounter ids it already gets per index;
  an index-based plan's states stay index-based (the vector is indexed by boss id, which is what `GetBossState` takes).
  It changes no behaviour and does not need the index workaround replaced to be useful; when `cmd/p4` merges, `TbH4.cpp`
  and `TbCmdFeeds.cpp` can switch to it in one commit. Nothing on `tbf/main` reads boss states yet, so this pin move uses
  none of it.
- **World-thread check.** `World::IsInWorldThread()` (World.h:609) answers whether the caller runs on the thread that runs
  startup and `World::Update`. The module assumes the world thread in its host package and says so in comments
  (`TbH3.h`, `TbRunFacts.h`); nothing in it runs on another thread today (the dashboard emitter has its own thread and
  takes no core object; the bot sessions tick in `OnBeforeMapsUpdate`). The check is available for a future assert or for
  code that must queue work instead of touching world state (e.g. a dashboard or async DB callback); the port adds no
  debug-only assert on it now.

### 1d. Cites and PORT-PLAN rule 3

`forever/tools/check_core_cites.py --old 25fc7ca --new d02645dd21` listed 44 cites into the 9 changed core files: 8 same,
11 MOVED, 25 MOVED? (no `T`/`F` marker on the line: reviewed), 0 GONE. All refreshed with one `--apply` (this file and the
other edits of this commit come with it). Two cites were fixed by hand: `TbH3.h:10` and `TbRunFacts.h:9` pointed at
`World.cpp:2470`, which was the `{` of the maps block even before the move; they now name the hook call, `World.cpp:2481`.
Rule 3's re-check is in `PORT-PLAN.md`: `World.cpp` is the only cited file in the range and every loader line moved by 14;
`Main.cpp`, `ObjectMgr.cpp`, `SpellMgr.cpp` and `cs_reload.cpp` are unchanged, and the new `.reload` hook calls no loader.

## 2. Gates (slot 2, core d02645dd21)

Core: `~/ForeverCore-d02645dd` (a git worktree of `~/ForeverCore` at d02645dd21f78c, sparse, no `sql/`) synced to the VM's
`src-2` only (the same rsync `vm-census.sh` makes; slot 9, which holds c0341987 for the Lua engine, and slots 0/1/3-9 were
not touched; `~/ForeverCore` stays at 6517af05). `build.sh` reads `src-2`; the OFF and ON builds share the tree keyed by the
core fingerprint, so the second option set reuses the core objects.

| Gate | Result |
|---|---|
| zero core change | `git -C ~/ForeverCore-d02645dd status --short` empty; the worktree is a clean checkout of the pin |
| module build, option OFF (`FOREVER_SLOT=2 build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=39`, 0 `warning:` lines |
| worldserver, option ON (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON`, full core build) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=1638` (VM at load 30-40 from other workers), `bin/Release/bin/worldserver` 72,061,256 bytes; a second ON build after the module changes (the kick poll and the reload hook) `MAKE_RC=0 TOTAL_SECONDS=144` |
| census SLICE-W1 (newest list, 270 files) | `files 270  compile clean 270  with errors 0  error places 0  includes of a header ForeverCore does not have 0` (`census-runs/pin-d02645-w1-s2`) |
| `check_worldlocation_scope.py` (SLICE-W1 list) | `checked 270 files, 0 could not be preprocessed, 0 core headers under the macro` |
| `slice_gen_context.py --check` | `up to date` |
| `slice_registry_check.py` | `(a)=0  (b)=218` |
| the 16 `forever/tools/test_*_wiring.py` | all OK |
| policy tests (`policy-tests.sh` copy on `tb-pin2`/`pt-pin2`) | `472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL` = the baseline (the four need extra sources/flags, the two extra arguments; the same ones `verify_all.sh` compiles separately) |
| `bash tools/verify_all.sh` (steps 1-6) | 482 standalone policy tests verified; decision trail self-test passed; `verify_okf.py` `PASSED (60 nodes)`. Step 7 (host contract) fails on `required core file is missing: src/game/SessionTransport.h`: the local `../tortoise-wow` checkout is stale. Not a new failure and not from this branch: `tools/` is unchanged and the checker reads only that core checkout |
| Turtle `vm-build ~/wt-pin2 --wait` | rc 0, `COMPILE_SECONDS=23 CMAKE_RC=0 MAKE_RC=0`, `mangosd`/`realmd` linked, `module-tree-hash=01e7fbb68710fa22` |
| Turtle `vm-test tb ~/wt-pin2` | `RESULT: PASS kind=tb sha=af4269456 dirty=0 files=4016`, rc 0, `vm_duration=1827s` (log `TortoiseBots-audit/vm-test-logs/wt-pin2-20261120-210316-1554814-86fc.log`) |
| nothing outside `forever/` changed | `git diff --name-only tbf/main...HEAD -- . ':!forever'` empty (the branch base is af248eff2; `tbf/main` itself moved on to 9753dcf9e during the work and is not merged here) |


## 3. Live boot on forever-test1

Core d02645dd21, module this branch, `TORTOISEBOTS_SLICE1=ON`. The server lock was held for the whole run (the guide's
`flock` on `/tmp/forever-live-forever-test1-setup.lock`; the helper held it for 2 h and a second one queued behind it,
because this boot takes longer than 2 h, see below).

Before: the four databases dumped to `~/ft1-backup/d02645-pre.sql.zst` (233 MB, sha256 `b09e45100f61b99f...`), the running
binary kept as `bin/worldserver.pre-d02645` (sha256 `9beb2972a8d7f72e...`), the config as `etc/worldserver.conf.pre-d02645`
and the SQL tree as `~/ft1-backup/sqlsrc.pre-d02645`. The SQL between the two pins is identical (`git diff --stat
25fc7ca..d02645dd21 -- sql` is empty); the tree was still installed from d02645dd21 (the same 1073 files, `sql/old` and the
module's `tortoisebots/` excluded from the copy) so the install procedure matches a real update. The module SQL was copied
again (`forever/sql/world`, no changes).

Config for this boot: `Calculate.Creature.Zone.Area.Data = 1` in `etc/worldserver.conf` (shipped `0`), on top of the usual
setup (`TortoiseBots.Enable = 1`, the realm on 70959-accepting builds).

**Finding: that key costs one UPDATE per spawn at creature load, and the load does not finish in a usable time.** With
86,533 spawns, all 86,531 with `zoneId = 0`, the `Loading Creature Data...` step runs a prepared `UPDATE creature SET
zoneId, areaId` per row (ObjectMgr.cpp:2752) and measured 230-767 rows/min over four one-minute counts (a mean of about
330/min), so the load alone is 4-6 hours on this box (a container MySQL; the rate did not change when
`innodb_flush_log_at_trx_commit` was set to 2 for a minute, so the cost is the per-row loop, not the fsync). After 57 min
the boot was stopped: the count had gone 86,531 -> 69,387 rows with `zoneId = 0` (69,385 with `areaId = 0`, 69 distinct
zone ids filled), so **the key does fill `creature.zoneId`/`areaId` from each spawn's position, and keeps filling until it
is stopped**; it is simply too slow to run to the end at boot. The databases were then restored from the dump (the count
back to 86,531) and the key put back to `0` for the checks below. Recommendation: do not enable
`Calculate.Creature.Zone.Area.Data` on a boot-critical box; if the port needs `creature.zoneId` it must not rely on that
key (the plans match spawns by entry and position, not by zone; `SLICE4-M-NOTES.md`).

The second boot (key back to `0`, databases restored) is the functional one:

| Check | Result | Evidence |
|---|---|---|
| Boot clean, DB updater | PASS | `ready...` after 46 s; no `ASSERT`/`ABORT`/`Segmentation` (count 0) and no `Could not prepare`; all four databases "up-to-date" (the SQL range is empty, nothing new to apply); the three `Classic.ScourgeInvasion*`/`TortoiseBots.Slice.DeadBackstopSeconds` "Missing name" warnings are the usual ones |
| Module boot lines | PASS | dashboard spell file 32,981 rows, talent file 467 rows, 74 features not ported (unchanged), `Loaded 537 dungeon step units for 3 plans (0 rows refused)` |
| `tb status` | PASS | `enabled, 0 bot session(s)`, then 1 after the login |
| Bot login with the engine | PASS | `tb login Agentone`: `bot Agentone entered the world on map 389`; `tb engine Agentone on`: `Agentone engine on` |
| `.tb plan ragefire_chasm check Agentone` | PASS | `units=103 matched=95 near=1 missing=7 optional=7 steps=55 steps_ok=55` (the 7 are the optional 8996 rows), as before the pin move |
| `.reload all` and the new hook (#374) | PASS | console `reload all`; after the loaders: `TortoiseBots: reloaded 3 dungeon plans (435 steps, 537 step units) after reload all` (the loader's own `Loaded 3 dungeon plans, 435 steps ...` line above it) |
| GM `kick Agentone` and the new kick path (#370) | PASS | `BotSession: logout asked for Player-70-00000003 (the core asked this session to leave (kick or ban))`, `bot Agentone left the world`, `BotMgr: session of Player-70-00000003 ended`, then `tb status` `0 bot session(s)` |
| zoneId count with the key off | PASS (baseline) | 86,533 spawns, 86,531 with `zoneId = 0` — the same as before the pin move |

Restore: server and bnet stopped, `bin/worldserver` back from `bin/worldserver.pre-d02645` (sha256 `9beb2972a8d7f72e...`,
`cmp` identical), `etc/worldserver.conf` from its backup (the key back to `0`), `sqlsrc` from
`~/ft1-backup/sqlsrc.pre-d02645` (1,073 files), and the four databases re-imported from `d02645-pre.sql.zst`
(`world.creature` 86,531 with `zoneId = 0` again, 3 plans). The restored binary then booted once more: `ready...` after
48 s, `tb status` answers `enabled, 0 bot session(s)`, 0 `ASSERT`/`ABORT`/`Segmentation`; worldserver, bnetserver and the
database container were stopped, no container is left running and the server lock was released.

## 4. Updater manifest entry and follow-ups (describe only; the data repository is not edited here)

In `~/forever-server-data-repo/manifest.json`, a new entry copied from the current `2026-11-19-70959` one:

```
id        "2026-11-20-70959-d02645dd"            (any unused id)
date      the merge day
core      { repo "Karrz1/TrinityCore", branch "forever", commit "d02645dd21f78cccf12cd975b30232b46e2555f3" }
module    { repo "TortoiseBots/TortoiseBotsForever", branch "port/forever", commit <the 40-char sha of the merge of pin/d02645dd> }
accepted_builds / realm_gamebuild / also_accept_builds: unchanged
data      unchanged: `git diff --stat 25fc7ca..d02645dd21 -- sql` is empty, so no SQL, extractor, DB2, map, vmap or
          mmap version constant changed; the existing release stays
server    { tag "server-d02645dd2-<module8>", ... } from `tools/forever-package pack --core d02645dd21... --module <40> --sql-dir <core>/sql`
notes     "Core d02645dd21 (kick requested getter, public boss states, world thread check, reload hook) with the module at <module8>"
```

Follow-ups the next worker should know:

1. `cmd/p4`, when it merges, can replace its per-index boss-state reads (`.tb instance info` and `TbCmdFeeds`) with
   `InstanceScript::GetBossStates()` (section 1c).
2. `World::IsInWorldThread()` is unused so far; use it where a callback (dashboard, async DB) might not be on the world
   thread instead of adding an assert in a hot path.
3. `Calculate.Creature.Zone.Area.Data = 1` fills `creature.zoneId`/`areaId` correctly but costs a boot measured in hours
   here (section 3); keep it `0` in the shipped configuration and do not make the port depend on it.
4. The `.reload` hook only answers `reload all`. If the module gains a cache fed by a table a narrower core reload reads
   (`reload creature_template`, `reload item`...), add that command path to `OnAfterReload` in `TbScripts.cpp`.

