# Slice 1, package R5: host wiring of the engine brain

Spec: `FOREVER-SLICE1-SPEC.md` section 2. Files: `forever/src/host/TbEngineBrain.{h,cpp}` (new), `forever/src/TbBotMgr.{h,cpp}`,
`TbBotSession.h`, `TbScripts.cpp`, `forever/conf/tortoisebots.conf.dist`, `forever/SLICE1-R5.txt` (census list),
`forever/tools/port_census.py` (define support).

## What it is

A bot marked "engine" gets a `PlayerbotAI` (the legacy engine) instead of the spike's hand-written follow/assist.
Everything that names a legacy type is in ONE file, `TbEngineBrain.cpp`, behind `#if TORTOISEBOTS_SLICE1`. Its header
(`TbEngineBrain.h`) has no legacy include: `Built()`, `EnsureConfig()`, `Create()`, `Tick()`, `Destroy()`, `Describe()`
and an opaque `Brain`. With the macro off the same file defines the "not built with the slice" versions (Create refuses,
Built() is false). `BotMgr::Bot` holds `unique_ptr<Brain, BrainDeleter>`, so a brain cannot outlive its entry.

## Lifecycle (Turtle file:line each step mirrors)

| Step | Where (ForeverCore host) | Mirrors |
|---|---|---|
| Mark | `Login` takes `TortoiseBots.Slice.EngineByDefault`; `.tb engine <c> on|off` -> `BotMgr::SetEngine` | new (Turtle has no per-bot switch) |
| Place | `BotMgr::Think` keeps the spike's placement and teleport (`TbBotMgr.cpp`, the block before the engine) | none: the Turtle bot is placed by the core's own login (the spike's `Think` placement stays here) |
| Config, once | `engine::EnsureConfig()` -> `sPlayerbotAIConfig.Initialize()`, lazily on the first brain (the world is loaded by then) | `host/BotHostAdapter.cpp:116` (done at startup there) |
| Create | `engine::Create`: `new PlayerbotAI(bot)`, `SetMaster`, `ResetStrategies`, `EnsureDefaultMovementStrategy`, context check, `BotActionLog::NoteLogin`, `PlayerbotAIStorage::SetAI`, usable check | `runtime/PlayerbotAIAdapter.cpp:30-62` (`Initialize`), `:79-85` (`IsUsable`), `BotManager.cpp:1251-1285` |
| Tick | `BotMgr::Update` calls `engine::Tick(brain, diff)` every world tick after `Think`, not while teleporting. `UpdateAI(diff)` inside `try`/`catch (ByteBufferException)` | `PlayerbotAIAdapter.cpp:64-77`, `BotManager.cpp:2431`; `PlayerbotAI.cpp:377`; `PlayerbotAIBase.cpp:16-31` keeps its own delay |
| Destroy | `PlayerScript::OnLogout` -> `BotMgr::OnPlayerLogout`; also before `Tick` when a logout is pending (`BotSession::LogoutRequested`), on `.tb engine off`, and as a net when the session entry is erased. `engine::Destroy`: `RemoveAI`, `BotActionLog::Close(guidLow)`, `delete ai` | `PlayerbotAIAdapter.cpp:149-158` (`Shutdown`), called from `BotManager.cpp:1588` (OnBotLogout) |

`OnLogout` fires in `WorldSession::LogoutPlayer` (`WorldSession.cpp:742`) before `CleanupsBeforeDelete` and the delete
(`:749-757`), so the order "AI before Player" holds for the logouts the core starts by itself too (the account owner
logging in, a kick), which the pending-logout check alone would miss.

Left out on purpose (not in the slice): `NormalizeHeadlessGmPresentation` (`BotManager.cpp:1145`, a Turtle Headless
session fix), `CinematicEnd` (`:1238`; the spike session already handles the client-less state), `AbandonCamp("logout")`
and `SavePlayerSnapshot` (`BotManager.cpp:1572-1578`: DB store and camp, out of slice; `~PlayerbotAI` still calls
`AbandonCamp` when a camp is active, `PlayerbotAI.cpp:272`), `RebindMaster`/`DetachMaster` (the master is bound once at
create; `PlayerbotAI::GetLiveMaster` re-validates the pointer by guid each tick, `PlayerbotAI.cpp:285`).

## `.tb where`

`brain: engine` or `brain: spike` (with a reason in brackets when the engine was asked for but is not running). With the
engine on, `Describe` prints: state (`PlayerbotAI::BotStateToString(GetState())`), combat and non-combat strategies
(`PlayerbotAI::GetStrategies(BotState)`, `PlayerbotAI.cpp:4147`, the same list `PrintStrategies` walks, `:4109`), and the
target from the engine's own value `"current target"` (`AI_VALUE(Unit*, "current target")`, as `AttackAction.cpp:313`; it
resolves by guid, so a despawned target reads as none). No API was invented.

## Config: what the engine needs and who provides it

`PlayerbotAIConfig::Initialize()` (`PlayerbotAIConfig.cpp:641`) does three things the ForeverCore host cannot:

1. reads `sConfig.GetStringDefault("AiPlayerbot.ConfigFile")`, `sConfig.GetFilename()` and `sConfig.GetStringDefault("LogsDir")`
   (`:650-654`, `:1446`, `:1942`): the legacy global `sConfig`;
2. calls `config.SetSource(file)` on its own `Config config` member (`:660-663`) and returns false, leaving `enabled` false, when
   it fails ("AI Playerbot is Disabled. No configuration file at ...");
3. reads about 720 keys with `config.Get{Bool,Int,Float,String}Default(key, default)`; every call site carries its own
   default, so an EMPTY source gives the engine's defaults. It also enumerates keys (`ConfigAccess::GetValues`, `:58-84`,
   `GetRootSections`, `GetKeys`, `GetValueHelper`).

What R4C must provide (shared-file work, not done here): a `Config` type with `SetSource(path)` (true also when the file
is absent or empty, or the module's own conf serves as the source), the four `Get*Default`, `GetFilename`,
`GetRootSections`/`GetKeys`/`GetValueHelper`, and a global `sConfig` with `GetStringDefault`/`GetFilename`. Without it
`EnsureConfig()` returns "PlayerbotAIConfig::Initialize failed ..." and every bot marked engine logs it once and stays on
the spike (`brain: spike (engine requested, not running: ...)`). The HOST side needed nothing more: no function under
`forever/src` could load the keys, because the engine reads them itself through `Config`, so a host-side key loader would
duplicate 720 reads. The one host decision is WHEN to call `Initialize()`: once, lazily, at the first brain.
Also needs `AiPlayerbot.Enabled` not 0 (default 1), and `Initialize()` reads the object manager (`:1233`) and, in random-bot
keys, `LoginDatabase`/`CharacterDatabase` (`:1752`, `:1776`); both exist by the time a bot is placed.

## Threading

`BotMgr::Update` runs from `WorldScript::OnBeforeMapsUpdate` (`ScriptMgr.cpp:1546`), called from `World::Update`
(`World.cpp:2462`) immediately BEFORE `sMapMgr->Update(diff)` (`:2475`). Commands run on the world thread too, and
`PlayerScript::OnLogout` runs inside `LogoutPlayer` on whichever thread logs the session out: the bot session is updated
only from `BotMgr::Update` (`BotSession::Tick`), so that is the world thread as well. The engine is therefore ticked on the
world thread while the maps are not updating. The Turtle build does the same (`BotHostAdapter::OnWorldUpdate`
before the map update).
Findings in the legacy code: `PlayerbotAI.cpp`/`PlayerbotAIBase.cpp`/`Engine.cpp` take no map lock and assume none. The only
locks are `PlayerbotAIStorage`'s own mutex (also taken by `GetAI`) and `PacketHandlingHelper::m_botPacketMutex`
(`PlayerbotAI.cpp:152`, a `try_lock`; the packet queue is fed from `SendPacket`, which `BotSession` drops, so it stays empty).
Comments name a "map-thread player hook" (`PlayerbotAI.h:726`, `PlayerbotAIStorage.h` "Any thread"): those are Turtle hooks
on the packet path, not engine ticks. Not checked: whether ForeverCore's map threads can touch the same bot Player at the
same moment from the packet or `SendPacket` side; BotSession drops packets, and the map update does not run during the tick.

## Census of the engine-facing file

`FOREVER_SLOT=4 FOREVER_CENSUS_LIST=forever/SLICE1-R5.txt forever/tools/vm-census.sh "" 14`. The list file starts with a
`-DTORTOISEBOTS_SLICE1=1` line: `port_census.py` now takes `-D` lines of the list file, and `FOREVER_CENSUS_DEFINES`
(space-separated flags) as well. The list-file form is the one that works today, because the VM's `census.sh` (outside the
repository) forwards only `FOREVER_CENSUS_LIST` into the container. Both are no-ops when unset. The census also writes
`errors.tsv` (every error place once: where, message) next to the other tables.

Result: `files 1  with errors 1  error places 267 (in 8 files)`. **Errors in `TbEngineBrain.cpp`: 0.** (The first run had 2,
both `TC_LOG_*` after the shim's `#define sLog`: fixed by logging with the legacy `sLog.outString/outError`.) All 267 are in
legacy headers that do not compile on ForeverCore yet, i.e. other packages' work:

| Header | Places | Owner (spec section 6) |
|---|---|---|
| `ai/playerbot/WorldPosition.h` | 225 | R4P/R4C (positions; `x y z mapId o` members, `sTerrainMgr`, `PathFinder`) |
| `ai/playerbot/GuidPosition.h` | 12 | R4C (`ObjectGuid::Set`, `creature_id`, `GetWorldObject`) |
| `ai/playerbot/ServerFacade.h` | 11 | R4C (`IsInRoots`, `HostileRefManager`, `GetAngle`, BG types) |
| `ai/playerbot/PlayerbotAI.h` | 10 | R4P (`GetFactionTemplateEntry`, `LootItemList`, `HasNetworkTransport`) |
| `ai/playerbot/Talentspec.h` | 4 | R4P/R3 (`GetBonusTalentCount`, `TalentTabEntry`) |
| `ai/playerbot/strategy/Value.h` | 2 | R4V (`creature_id`, `name`) |
| `ai/playerbot/PlayerbotAIConfig.h` | 2 | R4C (`MAX_RACES`, `SLOT_EMPTY`) |
| `ai/playerbot/strategy/ExternalEventHelper.h` | 1 | R4P (`HasNetworkTransport`) |
| `Config/Config.h` (not found, 1 include) | 1 | R4C: the `Config` adaptor |

Caution: a header error can hide a use in my file (an incomplete type makes the use below it unchecked), so 0 here is
"no error of its own so far"; run the census again after R4 lands. The names my file uses (`PlayerbotAI::SetMaster`,
`ResetStrategies`, `EnsureDefaultMovementStrategy`, `GetStrategies`, `GetState`, `BotStateToString`, `GetBot`,
`GetCurrentEngine`, `GetAiObjectContext`, `UpdateAI`, `PlayerbotAIStorage::{SetAI,RemoveAI,GetAI}`,
`BotActionLog::{NoteLogin,Close(uint32)}`, `ai::Value<Unit*>::Get`) are declared in `PlayerbotAI.h`, `BotActionLog.h`,
`PlayerbotAIStorage.h`, `strategy/Value.h` and resolved without error.

## Open questions

- `AiFactory::createAiObjectContext` and the four `create*Engine` are the slice factory (R2); `PlayerbotAI::PlayerbotAI`
  also calls `sObjectMgr.GetPlayerAccountIdByGUID` (`PlayerbotAI.cpp:230`) and `IsOwnedBot` uses
  `TortoiseBots::BotManager::Instance()` (`:264`): the former is a shim item, the latter a link stub (R6).
- `PlayerbotAI::UpdateAI` runs transport/loot/hire code (spec risk 4), reached on every tick once the brain exists.
- `BotActionLog::NoteLogin/Close` are mirrored because the Turtle adapter does; if the file's stubs make them noise, drop them.
- `ThinkIntervalMs` (500 ms) still gates placement only; the engine itself runs every tick.
- `.tb engine off` clears motion and attack state, it does not restore the spike's follow until the next Think (<= 500 ms).
