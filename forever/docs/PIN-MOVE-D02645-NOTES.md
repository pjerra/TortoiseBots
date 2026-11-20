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

The account-owner case keeps its earlier workaround (`BotMgr::Update`, `ownerLoadingMs` >= 2 s -> logout): it leaves before
the core's kick is seen and works whether or not the core ever kicks, so both remain.

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
  one virtual-ish call per boss per feed fill. `GetBossStates()` (InstanceScript.h:290) returns every state in boss id order
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

(pending)

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

**Finding: that key costs one UPDATE per spawn at creature load.** With 86,533 spawns, all 86,531 with `zoneId = 0`, the
`Loading Creature Data...` step runs a prepared `UPDATE creature SET zoneId, areaId` per row and measured 736 rows/min
(12.3/s, two one-minute counts), so the load alone is about 1 h 57 min on this box (a container MySQL with one fsync per
row, `innodb_flush_log_at_trx_commit` at its default). Nothing else starts until it ends.

(pending: boot result, zoneId after, checks, restore)

