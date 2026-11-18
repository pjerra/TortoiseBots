# Slice 1, package R6B: link it, wire the host, boot it (R6 link + R7 boot)

Branch `slice1/integ` (worktree `~/wt-slice-integ`), build slot: the default one (`~/forever-build/build`). T = Turtle core, F = pinned ForeverCore.
Result: `worldserver` links with `-DTORTOISEBOTS_SLICE1=ON` (227 undefined symbols -> 0), boots, and a warrior switched to the engine gets a
real `PlayerbotAI` that ticks without a crash. It does not yet follow a bot master (section 6, finding 1).

## 1. Build VM

* `~/forever-build/build.sh` had no way to pass a cmake argument. ONE optional environment variable was added: `FOREVER_CMAKE_EXTRA`
  (appended to the cmake line, empty by default). Backup: `~/forever-build/build.sh.bak-r6b-20261118`. The CMake cache is sticky: after an
  option-ON build the OFF gate passes `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF` (the build dir was left OFF).
* `~/forever-run/setup.sh`: one added step copies `aiplayerbot.conf.dist` to `etc/aiplayerbot.conf`. Backup `setup.sh.bak-r6b-20261118`.
* No new build directory (the default slot's tree was reused; disk 9 -> 12 GB free at the end, after one derived runner image, below).
* `forever-runner-gdb:latest` (+0.15 GB): `forever-runner` with gdb installed (the runner image has none), made with `docker commit`; used by a
  private start script (the scratchpad `world-gdb.sh`, same mounts as `world.sh`) to get a backtrace. `forever-runner` itself is untouched.

## 2. Real-build compile errors (the census was syntax only)

* **One** error class, in the root `CMakeLists.txt`: the pre-include was set as `COMPILE_OPTIONS "SHELL:-include <file>"` on the source files;
  the `SHELL:` prefix is not read for a source property, so the compiler got the literal text and treated the header as a second input
  ("cannot specify -o when generating multiple output files"). Now `"-include;<file>"`.
* After that all 127 legacy files compiled: no template instantiation error, none of the 20 generic-lambda blocks failed on the F side, no ODR clash between the compat pairs.
* A runtime crash at the first `Initialize()` is in section 5.

## 3. The 227 undefined symbols (525 references) and what became of them

Counts: first link **227** -> after the first round of stand-ins 5 -> 2 -> **0**.

| Choice | Symbols | Where |
|---|---|---|
| (a) route the call site through an unported feature | 0 | none: the call sites are in 40+ shared files (about 200 sites); see the decision below |
| (b) file added to the slice | 10, from 4 files | `Helpers.cpp` (split x2, trim, iequals, strstri), `MoveStyleValue.cpp`, `WorldPvpSeekLog.cpp`, `RaidTankHealing.cpp` (+ `StopUnsafeRaidHealerFiller` ...). Census was clean for all four; `forever/SLICE1.txt` now lists 131 files |
| (c) host service, answers from `TbBotMgr` or "not on this host" | 59 | `forever/src/host/TbHostBotManager.cpp`, `TbHostOwnership.cpp`, `TbHostTracing.cpp` (built only with the option: CMake excludes `host/TbHost*.cpp` from the default glob) |
| (c') link stand-in with a counter | 158 | `forever/src/slice/SliceLinkStubs.cpp`, each tagged with the file that owns the real definition |

**Decision on (a).** Every stand-in answers the value the caller already treats as "this feature is not there" and counts itself under a feature
of `ai/playerbot/UnportedFeatures.h` (14 new rows: boss and raid tactics 70 symbols, travel and exploration 25, other legacy services 15, random item tables 9,
talent spec tables 8, loot object stack 7, pool and hire services 7, bot factory 6, bot texts and help 5, flee manager 5, chat filter chain 3, plus rows for
tracing, player summon and camp spots used by the host services), so `.tb unported` shows which of them the engine reached. Routing every
call site through a helper would have edited about two hundred sites of shared files and needed a Turtle behaviour proof for each; the stand-ins keep Turtle
byte-identical. If a file named in `SliceLinkStubs.cpp` is ported into the slice, its stand-ins must go (a duplicate symbol at link says so).

### 3a. Host services (`forever/src/host/`), each answer

| Symbol | Answer on a host bot | Why |
|---|---|---|
| `BotManager::Instance()` | an object whose tables stay empty | the legacy call sites need an object; `BotEntry::~BotEntry` and `PlayerbotAIAdapter::~PlayerbotAIAdapter` are defined (`= default`) because the empty table's destructor names them |
| `BotManager::IsBot(guid)` | `BotMgr::IsBot` (an entry from `Login` on) | findings items 1 and 10: the bot counts as OWNED before `new PlayerbotAI` |
| `BotManager::IsRandomBot` | false | no random-bot pool |
| `BotManager::FindBot`, `GetOwnedCharacter` | null / false | no legacy record; callers fall through to the account check |
| `BotManager::GetBotsForMaster` | `BotMgr::BotsOfMaster` (new) | formations |
| `BotManager::BindBotMaster` / `ClearBotMaster` | `BotMgr::SetMaster` (new) + `PlayerbotAI::SetMaster` | the host's master guid and the engine's pointer move together; see finding 1 |
| `BotManager::RemoveBot` | `BotMgr::Logout` | idle-logout path |
| `AltBotService::IsAutonomous` | false | no autonomous alts |
| `RandomBotFacade::IsRandomBot / IsFreeBot / GetValue` | false / false / 0 | not a pool bot |
| `HireLifecycle::IsHired / GetMaster`, `ClaimLifecycle::IsClaimedByPlayer`, `BotActivityLeaseManager::GetActivity` (Idle) | not hired, not claimed, no lease | |
| `PlayerConvenience::RequestSummon` | false, counted | the host's own placement teleports a far bot next to its master |
| `AhBotSellerAccount`, `RandomBotAccountRegistry` (`LoadValidatedAccounts` leaves it unvalidated) | none | |
| `GuildOrders::Find / GetEnchantTrade / IsOrderProtectedItem / DisenchantQueued / Note*` | nullopt / empty / false / no-op | no order store |
| `BotCommands::IsInCommandedGroup`, `IsInRunningDungeonRun`, `IsRunKillFirst` | false | the commander is not ported (the "commanded-group query") |
| `BotCommands::RunArmedAreaTriggers`, `BoundRunPlanMap`, `RaidTankHealerSeat`, `NoteHumanChatOrder` | null / 0 / not seated / true ("always true while no run exists") | |
| `WhyTraceService`, `RunTraceService` (+ `RunTraceWriter::Close`), `ObservabilityEmitter` (`IsEnabled` false), `CombatMonitorService`, `CampMgr` | switched off, hooks are no-ops | no state to flush |

The world update-time average (R4M request 5) stays `WorldAverageDiffCompat` -> 0 (R6A).

### 3b. Stand-ins that are decisions, not "false"

`HealTopUpAllowedFor` true, `FankrissDpsMayAttack` true, `FankrissHealReachable` true (the permissive end: no boss rule forbids it); the two boss hold multipliers 1.0; `GetBotText` returns the
text NAME (an empty text would make the caller say nothing); `GetNumberOfEmoteVariants` 1; `TravelMgr::MapTransDistance` 0.

## 4. Host wiring

* **Owned before `new PlayerbotAI`** (findings 1, 10): see 3a. `PlayerbotAI::IsOwnedBot` reads `BotManager::IsBot && !IsRandomBot` (PlayerbotAI.cpp:268), true for a host bot from `Login`.
  `TbEngineBrain.cpp` `Create`: the constructor's only write to the Player is the taxi-cheat flag (PlayerbotAI.cpp:212); the flag is read first and put back on
  every failure path (a thrown exception, no context, not usable). Nothing else in the constructor path writes to the Player (checked: `HasCheat`, `AccountIdOfCharacterCompat`, the engines' `Init`).
* **R6A's three lines** (`TbScripts.cpp`): `#define TB_UNPORTED_TABLE_ONLY 1` + `compat/TbCompat_Unported.h`; the boot line `TortoiseBots: slice: 72 features not ported: ...` after the "enabled" line (only when
  the build has the engine); the console command `.tb unported` (one `UnportedReport()` line per feature).
* **Config** (finding 19): `CMakeLists.txt` adds `forever/conf/aiplayerbot.conf.dist` to the module's `CONFIGS` when the option is ON (the core copies it to `worldserver.conf.d/`, where it is inert);
  `setup.sh` copies it to `<dir of worldserver.conf>/aiplayerbot.conf`; `EnsureConfig` checks that path first and says "no <path> (copy aiplayerbot.conf.dist from worldserver.conf.d to that path)"
  instead of leaving only the engine's own line. `AiPlayerbot.ConfigFile` unset prints one harmless WARN per `Initialize` (the core's `GetStringDefault` is not quiet).
* **Logger.** `forever/conf/tortoisebots.conf.dist` gains `Logger.tortoisebots = 3,Console Server`: without a logger of its own the category falls to `Logger.root=5` (errors only) and
  every module INFO/WARN line, including `slice: missing ...`, was invisible. This is a log-visibility change in the default conf.
* **`.tb where`** now also prints the engine's master (and whether it counts as a real player), the "follow target", its distance and the motion type.

### Threading (findings item 2)

`WorldScript::OnBeforeMapsUpdate` is `ScriptMgr::OnWorldBeforeMapsUpdate` (ScriptMgr.cpp:1544-1546), called at **World.cpp:2465**, and `sMapMgr->Update(diff)` is at **World.cpp:2475**: it is strictly before the map update is
scheduled. `MapManager::Update` (MapManager.cpp:416-451) hands each map to the `MapUpdater` workers at :438 (`schedule_update`, MapUpdater.cpp:221) and **blocks on `m_updater.wait()` at :444-445**
(`pending_requests == 0`, MapUpdater.cpp:214-219) before it returns, so no map worker runs outside that window. Map threads can therefore not touch a bot Player while the engine runs, and
the bot session itself is updated only from `BotMgr::Update` (the same world thread; `BotSession::Tick`). Packets sent to a bot session are dropped (`BotSession::SendPacket`).
The engine is a caller of Player/Unit/MotionMaster in exactly the position a client's packet handler is (`WorldSession::Update` also runs on the world thread, outside the map update), which is the
position the core supports for a Player being driven from outside `Map::Update`. The tick stays in `OnBeforeMapsUpdate`; no move was needed. Not checked: anything the engine does that the core
expects to happen INSIDE `Player::Update` (regeneration timers etc. run in the map update as before; the engine only issues orders).

## 5. Boot (G5)

Server: `~/forever-run`, `setup.sh && world.sh start` (the last runs under `world-gdb.sh`: same container, gdb in batch mode). `ready...` and no `ASSERTION FAILED`.
Attempts: **3 of 6** (1: crash, 2: crash-free after the fix; 3: with the logger and `.tb where` diagnostics).

| Attempt | What | Result |
|---|---|---|
| 1 | `tb login Tbbeta`, `tb login Tbalpha Tbbeta`, `tb engine Tbalpha on` | **crash**: `strlen` in `PlayerbotAIConfig::LoadAhBotPriceKeys()` (backtrace via gdb) |
| fix | PlayerbotAIConfig.cpp (shared file): the AhBot price tables use `ai::kAhBotPriceMaxItemClass/Quality` (16 / 7) instead of the core's `MAX_ITEM_CLASS / MAX_ITEM_QUALITY`. F has 21 classes and 8 qualities: the name table had a null at index 7 (`std::string + nullptr`) and the loop wrote 5 rows past a 16-row array. Same values on T | fixed |
| 2, 3 | same commands, 30 s+ | `PlayerbotAI attached for Tbalpha (...) master Tbbeta`, `slice: registered 3/10/27/139`, `.tb where`: `brain: engine`, `state: Non Combat`, combat `arms, arms pve, buff, buff arms pve, close, dps assist`, non-combat `arms, arms pve, dps assist, follow`; `tb unported` works; `tb logout` both: `BotMgr: session ended`; `world.sh stop`: `Inferior 1 exited normally` |

`slice: missing` lines (7): strategy `wander`; triggers `master target active`, `out of free move range`, `formation station`; actions `check mount state`, `check values`; value `has enemy player targets`.
Three of them belong to the follow strategy itself (`FollowMasterStrategy.cpp`: "master target active" -> dps assist, "out of free move range" -> follow (ACTION_HIGH), "formation station"); the
slice registers only "update follow", so follow depends on that trigger alone.

`.tb unported` (one snapshot, about 4 minutes after the engine was switched on): boss and raid tactics 607,289 calls, loot objects 203,732 (`LootObject`/Creature::loot touches), cinematic and camera 101,866,
talent spec tables 15, quest packets 11, group packets 9, trade 8, travel 3, others single digits. The three big ones are per-tick checks, not features in use.

## 6. Findings for the live test (R8), most important first

1. **The warrior did not follow, and the reason is the master.** Both masters in the test are bot sessions. `HasNetworkTransportCompat(session)` is `!session->IsBot()`, so
   `HasRealPlayerMaster()` / `HasActivePlayerMaster()` are false, and `PlayerbotAI::DoNextAction` runs the rule "remove bot masters not in our group" (PlayerbotAI.cpp:3392-3397): master not in the bot's group, not
   a real player -> `ClearBotMaster`. The host's `ClearBotMaster` clears both the engine's pointer and `BotMgr`'s master (the `.tb where` line reads `master: none (no network session)` within the first tick).
   A real client as the master (or the bot in the master's group) keeps it. Without a client there is no console command that makes a group (`.group` has no join), so R8 must either use a real client
   or decide what a console-driven master counts as (a host switch for `HasNetworkTransportCompat`, or `ClearBotMaster` refusing). I did not commit either: it is a behaviour decision.
   **Verified by one throw-away build** (`HasNetworkTransportCompat` returned true for every session, not committed): `.tb where` then reads `master: Tbbeta (real player)`, `follow target: Tbbeta, safe: yes`,
   motion type 14 (follow), and after `tb go Tbbeta 20 0` the warrior stood 3.1 yd from its master (it moved from -8803.1 to -8788.5 while the master went to -8783.1). It was not run with a real client.
   The same run: `tb attack Tbbeta` (the master fights a Timber Wolf) -> after 15 s the warrior was still out of combat, victim none: the follow strategy's "master target active" trigger ->
   "dps assist" is one of the 7 missing keys, and the slice's combat strategies only run once the bot is itself in combat.
2. 7 missing keys above; the follow trigger set is the first thing to extend (`master target active`, `out of free move range`, `formation station` + its action).
3. `boss and raid tactics` / `loot objects` / `cinematic and camera` calls are per-tick atomic increments; cheap, but the counts will dominate `.tb unported`.
4. `DBErrors.log` on `~/forever-run/logs` is 150 MB (not from this package); `forever-world` was always stopped at the end.

## 7. What I am unsure about

* The 158 stand-ins' polarity was read from the header comments and call sites; the permissive ones are in 3b. A raid boss rule that a stand-in turns off would show up only in a raid.
* `BindBotMaster` / `ClearBotMaster` move the host's master (finding 1): a decision that the legacy rules own the master of an engine bot.
* The AhBot fix touches a shared file; the values are identical on T (16 / 7). The Turtle gates below prove it compiles and the tests pass, not that nothing else reads those tables on F.
* Other core constants used to size tables (`MAX_RACES`, `MAX_CLASSES` ...) were checked for the same trap only by grep: the arrays are sized by the same macros as their loops.

## 8. Gates

| Gate | Result |
|---|---|
| Option OFF module build: `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF ~/forever-build/build.sh 12 tortoisebots` | `CMAKE_RC=0 MAKE_RC=0`; the build graph has 9 module objects, none from `ai/`, `host/TbHost*` or `slice/` (`grep -c` on `build.ninja` = 0); cache left at OFF |
| Option ON `worldserver` (final code) | `CMAKE_RC=0 MAKE_RC=0` |
| Slice census `FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14` | `files 131  compile clean 131  with errors 0  error places 0` |
| Turtle `vm-build ~/wt-slice-integ --wait` at `2c11845b2` (later commits: notes only) | `COMPILE_SECONDS=2198 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0` |
| Turtle `vm-test tb ~/wt-slice-integ` | `VM-TEST-EXIT 0` |
| `policy-tests.sh` (tree synced to `forever-build/tb`) | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL: the same set as the baseline in SHIM-R3-NOTES.md (`test_combat_monitor_service`, `_race`, `test_decision_capture_files`, `test_run_trace_writer`, `test_decision_capture_codec`, `test_log_file_rotation`) |
