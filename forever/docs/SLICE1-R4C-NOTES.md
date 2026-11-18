# ForeverCore port, slice 1, package R4C: engine core, config, log, positions

Files: the 29 `.cpp` files of `forever/SLICE1-R4C.txt`, every header in `ai/playerbot/*.h` (except `PlayerbotAI.h`) and
`ai/playerbot/strategy/*.h`, the compat pair `forever/src/compat/TbCompat_C.h` (+ `TbCompat_C.cpp`) and `ai/cmangos-compat-r4c.h`, the new
`forever/conf/aiplayerbot.conf.dist` with its generator `forever/tools/gen_aiplayerbot_conf.py`. Measure:
`FOREVER_SLOT=5 FOREVER_CENSUS_LIST=forever/SLICE1-R4C.txt forever/tools/vm-census.sh "" 14`.

## Numbers

| | Error places | Files holding them |
|---|---|---|
| BEFORE (`9b32de181`) | 297 | 20 |
| AFTER (this branch) | 60 | 13 |

Of the 60, 52 are in files and headers R4C owns, 8 are in other packages' headers (V `ItemUsageValue.h` 4, A `GenericActions.h` 2 and
`ReachTargetActions.h` 1, P `PlayerbotAI.h` 1). The census compiles each file alone and counts a place once, so 22 of the 29 files now
have no error of their own; a place behind an incomplete type can still be hidden by one of the 13 (the header errors below).

| File | before | after |
|---|---|---|
| WorldPosition.cpp | 130 | 17 |
| ChatHelper.cpp | 39 | 3 (2 lines) |
| GuidPosition.cpp | 32 | 13 (one family) |
| BotActionLog.cpp | 21 | 0 |
| ServerFacade.cpp | 11 | 0 |
| BotLog.cpp | 10 | 0 |
| PlayerbotSecurity.cpp | 10 | 0 |
| Engine.cpp | 6 | 0 |
| PlayerbotAIConfig.cpp | 4 | 0 |
| Value.cpp | 2 | 0 |
| BotDiagnostics.cpp | 4 | 1 |
| CustomStrategy.cpp | 2 | 2 |
| headers: ServerFacade.h 5, GuidPosition.h 5 -> 3, RandomBotFacade.h 4, Talentspec.h 4 | 18 | 16 |

Methods: (a) name entries 8 (`TEAM_BOTH_ALLOWED`, `CONFIG_UINT32_MAX_PLAYER_LEVEL`, `CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP`,
`HIGHGUID_MO_TRANSPORT`, `MAP_LIQUID_TYPE_{MAGMA,OCEAN,SLIME,WATER}`, `MAP_ALL_LIQUIDS`); (b) both-core helpers 86 (ForeverCore side; 82 on the Turtle side, the rest are out-of-line or type names), every one with a
Turtle-side twin in `ai/cmangos-compat-r4c.h` that is the call the code always made (the same name and signature in both files); (c) the
helpers whose answer differs are listed under "Different answers". Nothing is stubbed.

## Where the bot config is read on ForeverCore (the brief's config task)

`PlayerbotAIConfig::Initialize` (`PlayerbotAIConfig.cpp:641-676`) tries three paths in this order, and stops at the first that
`Config::SetSource` (R3: `TbCompat_Config`, ACE ini rules, false for a missing file) opens:

1. the value of the main-config key `AiPlayerbot.ConfigFile`, read from `sConfig` = `ConfigMgr` (worldserver.conf plus everything in
   `worldserver.conf.d`). Not set by default. ForeverCore's `GetStringDefault(name, def)` is not quiet: every `Initialize` writes one WARN line
   "Missing name AiPlayerbot.ConfigFile in config file ..." when it is unset (harmless; `AiPlayerbot.ConfigFile =` in `tortoisebots.conf` silences it).
2. the directory of `sConfig.GetFilename()` + `aiplayerbot.conf`. `GetFilename()` is the `-c` path the server was started with
   (`Config.cpp:183`), so with `worldserver -c /run/etc/worldserver.conf` (the VM runner, `~/forever-run/world.sh`) this is
   `/run/etc/aiplayerbot.conf` = `~/forever-run/etc/aiplayerbot.conf`: the directory that holds `worldserver.conf` and the `worldserver.conf.d`
   folder where `tortoisebots.conf` lives. A `-c` path without a slash gives nothing here.
3. `_D_AIPLAYERBOT_CONFIG` (`PlayerbotAIConfig.h:16-19`). On ForeverCore neither `PLATFORM` nor `PLATFORM_WINDOWS` is defined, the `#if` is `0 == 0`
   and the value is the bare `aiplayerbot.conf`: the working directory of the server process (`/run/bin` on the VM runner).

When none opens, Initialize prints "AI Playerbot is Disabled. No configuration file at <path 2> or aiplayerbot.conf." and returns false;
`TbEngineBrain::EnsureConfig` turns that into "PlayerbotAIConfig::Initialize failed ..." (R5). `AiPlayerbot.Enabled = 0` in the file also stops it.

**The conf file.** `forever/conf/aiplayerbot.conf.dist` is generated, not hand picked: `python3 forever/tools/gen_aiplayerbot_conf.py`
reads every `config.Get{Bool,Int,Float,String}Default("Key", default)` call of `PlayerbotAIConfig.cpp` (718 calls, 696 distinct keys, 18 calls
with a non-literal key skipped) and writes `Key = default` under `[AiPlayerbotConf]` with a comment naming the type and the source line.
686 keys have a literal default and are written as set; 10 have a computed default (`6 * 3600` is evaluated, the rest are names or a nested
call) and are written commented out with the expression, so the code's default applies until the owner sets them. `--check` fails when the file
is not what the source gives (run it after any change to a `Get*Default` line). The file was read back with the real `TbCompat_Config.cpp`
(one scratch program, laptop, flock+nice): `SetSource` true, one section, 686 keys, the defaults come back as written.

**How it gets installed and loaded.** Nothing loads it by itself: the file is read by `PlayerbotAIConfig::Initialize` through `Config`,
not by `ConfigMgr`, so it must NOT be put in `worldserver.conf.d` (that folder is read by the core's own parser as part of the main config,
with `[AiPlayerbotConf]` as a section the core does not use). The steps:
1. the build installs `.conf.dist` files of a module through `trinity_add_module(... CONFIGS ...)` (`CMakeLists.txt:123-124`); the new file has to be
   in that list (NEEDS FROM the CMake owner below), which puts `aiplayerbot.conf.dist` next to the other module dist files;
2. the server directory gets a COPY named `aiplayerbot.conf` (without `.dist`) in the directory of `worldserver.conf` (`~/forever-run/etc/` on the VM);
3. the first engine brain calls `EnsureConfig()` (R5), which calls `sPlayerbotAIConfig.Initialize()` once, after the world is loaded.

## Logging: the macro collision and what writes to disk

`sLog` has two meanings in a bot file. In every bot file that includes `playerbot.h` it is `BotLog::Instance()` (`playerbot.h:36-38`), on both
cores; `BotLog` writes to its own file or falls back to the core's logger. In `BotLog.cpp` itself the fallback was spelled `Log::Instance()`, which is
the Turtle singleton and does not exist on ForeverCore (`sLog` there is the shim's printf proxy, `#define sLog (CmangosLogProxy())`, so the core's
own `TC_LOG_*` macros, which spell `sLog->...`, cannot be used in a legacy file). The macro was not changed: `BotLog.cpp` now asks for the
core logger through `LegacyLogCompat()` (Turtle: `Log::Instance()`, ForeverCore: the proxy). Everything else that printed with `%s` and a name now
hands it over as `CStrCompat(name)` (R3's) or `CStrOrCompat(name, fallback)` (new, for the places that tested the pointer).
Where a TUs needs the core's own macros (a log line from a compat helper) it lives in `TbCompat_C.cpp`, which is compiled without the shim
(`GuidFromRawCompat`: `TC_LOG_ERROR`).

Files the R4C code writes, with the default config (`aiplayerbot.conf.dist`), so the live round knows what appears on disk:

| What | Path | When |
|---|---|---|
| `BotLog` (`BotLog.cpp:23-60`) | `<LogsDir>/<AiPlayerbot.BotLogFile>`, opened "a" | only when `BotLogFile` is set; the default is empty, so nothing is written and every line falls back to the core log. `LogsDir` is the main config's key (`PlayerbotAIConfig::logsDirPath`) |
| `BotActionLog` (`BotActionLog.cpp:60-108`, `:224-234`) | `../logs/bots/<bot>_acc<accountId>_<YYYYMMDD-HHMMSS>[-seq].log`, relative to the working directory; `EnsureLogDir` also makes `logs/` and `logs/bots/` | `AiPlayerbot.EnableActionLog = 1` and `ActionLogScope = commanded`: one file per bot that is in scope when it first logs a line; scope comes from `runtime/CommandedGroupQuery` and the hire ledger, which are not in the slice: what their link stubs answer decides whether any file appears. A retention sweep removes old files (`ActionLogMaxAgeDays 3`, `ActionLogMaxTotalMB 2048`). On the VM runner `../logs` is `~/forever-run/logs` |
| named logs (`PlayerbotAIConfig::openLogLocked`, `:1861-1930`, `log()`) | `<LogsDir>/<name>`, rotated hourly into timestamped pieces that a background thread packs to `<piece>.gz` and deletes the plain piece; pieces older than `LogRetentionDays 3` are removed | a name is written only when it is in `AiPlayerbot.AllowedLogFiles`, default `bot_events.csv,deaths.csv` (both by `logEvent`) |
| `load_map_grid.csv` (`WorldPosition::loadMapAndVMap`) | same rules | only when listed in `AllowedLogFiles` (not by default) |

`BotDiagnostics.cpp`, `HealerDiagnostics.cpp`, `PerformanceMonitor.cpp` and `ModuleLog.cpp` open no file themselves (they write through the logs above or
the core log). No other R4C file creates a file, a directory or removes one.

## Different answers (one per helper whose meaning is not identical on the two cores)

* **Area key** (`AreaKeyAtCompat`, R3, and `AreaEntryOfKeyCompat`): T's key is a per-map area FLAG and `AreaEntry::GetByAreaFlagAndMap(flag, map)` finds the row by
  walking the table for the matching explore flag (else the map's linked zone); F has no flag, the key is the area ID and the row is `sAreaTableStore.LookupEntry(id)`.
  Callers decided one by one: `WorldPosition::GetArea` and `isEnemyHomeZoneFor` (R4C, both turn the key into a row: through `AreaEntryOfKeyCompat`, so each core reads
  its own key the way it made it); `AsyncGuidPosition` (`GuidPosition.h:110`) only stores and returns the key (fits its `int16`: F area ids are below 32768);
  `BotManager.cpp:238` tests for 0 (no area: the same on both). Outside R4C and NOT changed here: `TravelMgr.cpp:2775` (`GetFishZone`) and `FishAction.cpp:561` look the
  key up by flag in `TerrainManager::GetZoneAndAreaIdByAreaFlag`, `GuildOrderSources.cpp:123` (`ZoneOf`) does it with `AreaEntry::GetByAreaFlagAndMap`: each needs
  `ZoneAndAreaIdOfKeyCompat(zone, area, key, map)` / `AreaEntryOfKeyCompat` (OFFERED, same result as the T call on T). `TravelMgr.cpp:2868-2881` sorts and
  NAMES fish points by the key (`FISH_LOCATION_<map>_<key>_<n>`) and saves it in `ai_playerbot_named_location`: a name or description written by one core is not meaningful to the other.
* **Zone, team, name of an area row**: T `ZoneId` (0 for a zone) = F `ParentAreaID` (0 for none; one level in the 1.12 data, the code loops up the chain anyway); T `Team`
  (2 Alliance, 4 Horde, 6 both) = F `FactionGroupMask` (the same two bits; modern rows may carry a mask that classic data would not); name = the enUS string.
* **64-bit guid key** (`GuidKeyCompat`): T the raw guid, F `std::hash<ObjectGuid>` (FNV-1a over the 16 bytes). Used for "same target" keys (`Engine::FailureKey`, the why-trace), the
  throttle map in `BotActionLog_LogDamage`, the whisper-repeat map and every `0x%llx` in the action log: the log shows the hash, with the name beside it, not a legacy guid; never turn it
  back into a guid. `GuidFromRawCompat(raw)`: only 0 has a ForeverCore form; any other number gives the empty guid and ONE error line ("a legacy 64-bit guid has no ForeverCore form").
  Its callers: the three host hook wrappers `BotActionLog_LogAuraApply/Remove/CastStart` (they are called by the patched Turtle core, nothing calls them on ForeverCore) and
  `GuidPosition(uint64, pos)` (`GuidPosition(0, pos)` in four travel places, and `stoull(getQualifier())` in a travel command).
  `GuidFromNumbersCompat(hi, entry, counter)`: a guid-shaped record of the three numbers (world object format with map 0, or a plain database-id guid for a player/item); it names no live object.
  The text form of a guid inside a `GuidPosition` string and inside a `|Hfound:...|h` chat link (`GuidWriteCompat`/`GuidReadCompat`): T one decimal, F `high/low` (two decimals,
  lossless); a string written by one core cannot be read by the other.
* **Liquid** (`LiquidAtCompat`): F numbers the liquid types water 1, ocean 2, magma 4, slime 8; T magma 1, ocean 2, slime 4, water 8, and the policies (`SwimPathPolicy.h`,
  `MagmaHazardPolicy.h`) are written in T's numbers. The helper translates the type mask in both directions, so `MAP_LIQUID_TYPE_*` and `MAP_ALL_LIQUIDS` (defined in
  `TbCompat_C.h`) are T's numbers on both. A request of 0 or of all four types is "any liquid" (an empty request on F). The status values (`LIQUID_MAP_*`) are the same numbers.
* **Path type bits are NOT the same**: T's `PATHFIND_DEST_FORCED/FLYPATH/UNDERWATER` (0x20/0x40/0x80) do not exist on F, where 0x20 is SHORT, 0x40/0x80 FARFROMPOLY_START/END
  (`PathGenerator.h:39-48`). `SwimPathPolicy.h` and `getPathStepFrom` read those bits as T's. Not closed (see Left open): on F `KeepPathStep` would read a short or far-from-poly path as a forced/fly/underwater one.
* **Facing** (`SetOrientationAndSendCompat`): T sets the orientation and sends a movement heartbeat; F has no heartbeat call and uses `SetFacingTo(angle, true)`, a facing spline that sets the
  orientation at its end and tells the clients. `ServerFacade::SetFacingTo`'s other branch (`unit->SetFacingTo(angle)`) compiles unchanged and on F means `force = true`, a facing spline even while moving.
* **Creature templates**: T `civilian` column = F `CREATURE_FLAG_EXTRA_CIVILIAN` (the core's own `Creature::IsCivilian`); T guard bit 0x400 = F `CREATURE_FLAG_EXTRA_GUARD` (0x8000, same name);
  "never aggros" = T trigger 0x80 or no-target 0x20000, F trigger 0x80 only (F's 0x20000 is NO_CRIT, it has no untargetable flag); "above normal" = T `rank > NORMAL`, F the classification
  Elite, RareElite, Obsolete (T's world boss), Rare (not Trivial, MinusMob). `FactionTemplateEntry::IsHostileTo` takes a pointer on F, a reference on T; same rule.
* **Quest and item texts**: `QuestLevelCompat` is F's `GetClassicQuestLevel()` (the classic data's level, 0 when it has none); titles come from `GetLogTitle()` and `QuestTemplateLocale::LogTitle`;
  an item's localised name is `ItemTemplate::GetName(locale)` (the locale indexes 0-8 are the same numbers on both cores). The item random-property suffix has no ForeverCore store (Left open).
* **`MaxLevelCompat()`**: T `DEFAULT_MAX_LEVEL` is the compile-time 60, F reads the setting `MaxPlayerLevel` (`CONFIG_MAX_PLAYER_LEVEL`); the loops and the gate that used the constant
  now follow the server's setting.
* **`LegacyItemClassCountCompat`** = T's `MAX_ITEM_CLASS` (16, `ItemPrototype.h:164`); F's constant is 21. The AhBot tables keep 16 rows and every reader already refuses `itemClass >= 16`.
* **Queues**: `InBattleGroundQueueCompat` asks F's `InBattlegroundQueue()` whose default counts arena queues too (T has none); `IsPlayerInLfgQueueCompat` is `LFGMgr::GetState == LFG_STATE_QUEUED`
  (T `IsPlayerInQueue`).
* **Transport**: `TransportOfCompat` returns a `Transport` only; F's `GetTransport()` also answers a vehicle, which the legacy code did not mean. `PassengerPositionCompat`/`PassengerOffsetCompat` are
  `GetPositionWithOffset`/`GetPositionOffsetTo` (the same transform, by return value).
* **`HasSpellCategoryCooldownCompat`**: the remaining category cooldown above zero. **`IsNumericCompat`**: same loop (empty string true on both). **`ReputationStateOfCompat`**: T flag word bits 0x01/0x02 =
  F `ReputationFlags::Visible/AtWar`.
* **Spawn data**: `DoCreatureDataCompat`/`DoGameObjectDataCompat` walk F's `GetAllCreatureData()`; the key is a database spawn id. `GetGameObjectBySpawnIdCompat` asks the map by spawn id (T built a guid
  from entry and spawn id).

## Left open (not stubbed), with counts and what each needs

| Where | n | What | Needs |
|---|---|---|---|
| `WorldPosition.cpp:30,31,41,42` | 4 | the `static_assert`s that pin `SwimPathPolicy.h`/`SteepSlopePolicy.h` to the core's path types and nav flags: `PATHFIND_DEST_FORCED/FLYPATH/UNDERWATER` and `NAV_MAGMA/SLIME/STEEP_SLOPES` do not exist on F (see "Path type bits") | a decision: the two policies read the F bits (`NAV_GROUND_STEEP`, `NAV_MAGMA_SLIME`, `PathType` 0x20/0x40/0x80 meaning) or the swim/steep rules are not ported yet |
| `WorldPosition.cpp:1361` | 1 | `ExcludeSteepSlopes(PathFinder&)`: a Turtle patch of the path filter; F's `PathGenerator` has a slope penalty inside its filter and no switch | the same decision |
| `WorldPosition.cpp:1639,1643,1657` | 3 | `ClosestCorrectPoint`: the navmesh query through `MMapManager::GetNavMeshQuery(map)` (F takes mesh map, instance map and instance id) and the include/exclude nav flags | navmesh query helper once the flag decision above is made; travel nodes only |
| `WorldPosition.cpp:106,128` | 2 | `set(guid)` looks a spawn record up by `guid.GetCounter()`; on F the counter of a creature is not its database spawn id (the brief: do not invent one) | the caller needs the spawn record of a guid: for a live object `Creature::GetSpawnId()` after `GetAnyTypeCreatureCompat`; for a bare guid nothing exists |
| `WorldPosition.cpp:136` | 1 | `HIGHGUID_TRANSPORT` (T's game object based transport) has no F guid tag (`HIGHGUID_MO_TRANSPORT` is defined: `HighGuid::Transport`) | none: the case label can go when the file is ported by hand |
| `WorldPosition.cpp:677` | 1 | `info->level_max` of the point-danger index: F creature templates carry no level (it comes from content tuning) | a source of a template's level; travel safety only |
| `GuidPosition.cpp:163-191` | 13 | `IsPartOfAPool`, `GetGameEventId`, `IsEventUnspawned`: `sPoolMgr.IsPartOfAPool<Creature>(GetCounter())` and `sGameEventMgr.GetGameEventId<Creature>(GetCounter())` start from a spawn id taken from a guid, and F's pool and game event APIs differ | a guid-to-spawn-id answer (above) and the F pool/game-event calls; travel and RPG only |
| `GuidPosition.h:17,18` | 2 | the constructors from a `CreatureDataPair`/`GameObjectDataPair` build a guid from entry and spawn id (`creature_id` array, 3-number guid) | a guid-keyed spawn record: F has none; callers are `TravelMgr` quest and fish tables |
| `GuidPosition.h:86` | 1 | `b << (WorldPosition)guidP` binds a temporary to `operator<<(ByteBuffer&, WorldPosition&)`; on T the byte buffer's raw template took it, on F that template is deleted | decide the wire form of a `GuidPosition`; no caller in the slice |
| `ServerFacade.h:136,142` | 2 | `HostileRefManager`/`Unit::GetHostileRefManager` | an adaptor type with `getFirst()`/`next()`/`getSource()` over `ThreatManager::GetThreatenedByMeList()`, in step with V's and A's `HostileReference` use (`AttackerCountValues.cpp:21`, `TellTargetAction.cpp:42,76`, `Commander.cpp`) |
| `ServerFacade.h:268,274,278` | 3 | `BattleGroundQueueTypeId`, `BattlegroundMgr::BGTemplateId` | battlegrounds are out of the slice |
| `RandomBotFacade.h:78-91` | 4 | `AuctionEntry` (F: `AuctionPosting`) | auction is out of the slice |
| `Talentspec.h:5,28` | 4 | `Player::GetBonusTalentCount` (no counterpart), `CONFIG_FLOAT_RATE_TALENT`, `TalentTabEntry` incomplete | out of the slice |
| `ChatHelper.cpp:126` | 1 | `SKILL_FIRST_AID`: F has no such name (First Aid was removed from the DB2 skill list; a number copied from T is not proven on F) | the skill line id on F |
| `ChatHelper.cpp:539` | 1 | `ItemRandomPropertiesEntry`/`sItemRandomPropertiesStore` (the "of the Bear" suffix): no store on F | where F keeps item random suffix names |
| `CustomStrategy.cpp:119,122` | 2 | `CharacterDatabase.escape_string` and `PQuery` with `%s`/`%u` + `QueryResult*` ownership | custom strategies are out of the slice; see "Traps" |
| `BotDiagnostics.cpp:223` (also `ReachTargetActions.h:195`, A's) | 1 | `Creature::GetCombatStartPosition`: F records no combat start point | a core hook, or dropping the input of the evade probe |

In other packages' headers (not counted above): `ItemUsageValue.h:14-16` (V; item random property, loot item, auction entry), `GenericActions.h:102,104` (A; the packet call),
`ReachTargetActions.h:195` (A; `GetCombatStartPosition`), `PlayerbotAI.h:73` (P; `ExtractSpellIdFromLink`).

## NEEDS FROM

* **CMake owner (lead / R5)**: add `${CMAKE_CURRENT_SOURCE_DIR}/forever/conf/aiplayerbot.conf.dist` to the `CONFIGS` list of `trinity_add_module` (`CMakeLists.txt:123-124`), and make the deployment copy the
  installed file to `<directory of worldserver.conf>/aiplayerbot.conf` (`~/forever-run/etc/` on the VM; `setup.sh`/`world.sh` is where the runner's other files are placed).
* **R5 (`forever/conf/tortoisebots.conf.dist`)**: optional, one line `AiPlayerbot.ConfigFile =` with a comment (silences the one WARN per `Initialize`, and lets the owner point at another path).
* **R4A**: `ReachTargetActions.h:195` is the same `GetCombatStartPosition` item as `BotDiagnostics.cpp:223`; A's `GenericActions.h:102,104` is the `CMSG_GAMEOBJ_USE` packet call.
* **R4V**: `AttackerCountValues.cpp:21` and `TankTargetValue.cpp:281` use `HostileReference`; if V defines the adaptor, `ServerFacade.h:136,142` can name it (and then closes), else R4C
  closes both with an adaptor of its own and V uses that one.
* **Linking round / host**: `PlayerbotAIConfig::Initialize` also reads the object manager (`:1233`) and, for the random-bot keys, `LoginDatabase`/`CharacterDatabase`; call it after the world is loaded (R5 does).

## OFFERED (defined in `TbCompat_C.h`, usable by every package; the lead dedupes at the merge)

`GuidKeyCompat`, `GuidFromRawCompat`, `GuidFromNumbersCompat`, `GuidWriteCompat`/`GuidReadCompat`, `CStrOrCompat`, `IsNumericCompat`, `ZoneIdCompat` (a package that spells
`getZoneId()` can use it instead of a macro), `AccountIdOfCharacterCompat`, `WorldBoolConfigCompat` with `CONFIG_UINT32_MAX_PLAYER_LEVEL` and
`CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP`, `MaxLevelCompat`, `TEAM_BOTH_ALLOWED`, the reputation helpers (`ReputationStateOfCompat`, `ReputationRankCompat`, `ReputationValueCompat`,
`ReputationAtWarCompat`, `ForcedReputationRankCompat`), `FactionOfTemplateCompat`, `FactionEntryOfCompat`, `FactionNameCompat`, `FactionTemplateHostileToCompat`, `IsPlayerControlledCompat`,
`IsContestedPvpCompat`, `GetAnyTypeCreatureCompat`, `MapIsLoadedCompat`, `RandNormCompat`, `HasSpellCategoryCooldownCompat`, `InBattleGroundQueueCompat`, `IsPlayerInLfgQueueCompat`, the
creature template helpers (`CreatureIsCivilianCompat`, `CreatureIsGuardFlagCompat`, `CreatureNeverAggrosCompat`, `CreatureIsAboveNormalCompat`), the area helpers (`AreaEntryOfKeyCompat`,
`ZoneAndAreaIdOfKeyCompat`, `AreaIdOfCompat`, `AreaZoneIdCompat`, `AreaTeamCompat`, `AreaNameOrEmptyCompat`, `MapNameOrEmptyCompat`), the transport helpers, `LiquidAtCompat` and the vmap/mmap helpers.

## Traps found on the way (not errors, nothing here is changed)

* **`CharacterDatabase.PQuery("... '%s' ... %u", ...)` compiles on ForeverCore and does the wrong thing.** The core's `PQuery` is a `{}` format (`DatabaseWorkerPool.h:120-131`); a legacy `%s` string is taken as the
  query text, the arguments are dropped, and no error is raised. The census cannot see it. Every legacy `PQuery`/`PExecute` with `%` placeholders (a grep finds them in `runtime/`, `commands/` and the strategies)
  needs its format changed when that file is ported. `CustomStrategy.cpp:119-122` is the one in R4C's list.
* **`sConfig.GetStringDefault` on ForeverCore logs a WARN for every missing key** (see the config section); R3's `Config` class (the bot's own `aiplayerbot.conf`) does not.
* **Numeric creature flags**: `WorldPosition.cpp` carried T's `flags_extra` bits as literals (0x400, 0x80, 0x20000); on F those bits mean other flags. They are now named by the helpers. Any other
  literal `flags_extra` mask in the tree has the same problem.

## Gates

(see the end of this file after the final run)
