# ForeverCore port: shim round 1

One header (`forever/src/compat/forever-compat-shim.h`) filled in; no bot code changed. Counts are
`forever/tools/port_census.py` error places at the pinned core (`forever/PINNED_CORE`).

## Numbers

| Run | What | Error places |
|---|---|---|
| BEFORE | full census, the shim as committed at 7808b02ae | (pending) |

## Entries

`kind`: 1 = type name, 2 = singleton, 3 = member name, 4 = free function / proxy.
"Meaning checked" is the line that shows the same thing on both cores (T = Tortoise core, F = ForeverCore).

| old name | ForeverCore | kind | meaning checked |
|---|---|---|---|
| `UnitAI` alias (removed) | core has its own `UnitAI`, the AI base class | — | T `ai/cmangos-compat-shim.h:96` alias only because Tortoise has no `UnitAI`; F `AI/CoreAI/UnitAI.h:50` |
| `BattleGroundTypeId` | `BattlegroundTypeId` | 1 | T `SharedDefines.h:1742`, F `Miscellaneous/SharedDefines.h:6944` (AV/WS/AB = 1/2/3 both) |
| `Config` | `ConfigMgr` | 1 | T `shared/Config/Config.h:34`, F `common/Configuration/Config.h:26` |
| `CreatureDataPair` | `std::pair<uint32 const, CreatureData>` | 1 | T `ObjectMgr.h` creature data map value type; F `ObjectMgr.h:1502` `GetAllCreatureData()` (unordered_map keyed by `ObjectGuid::LowType`) |
| `GameObjectDataPair` | `std::pair<uint32 const, GameObjectData>` | 1 | F `ObjectMgr.h:503,1523` same shape |
| `GridPair` | `GridCoord` | 1 | T `Maps/GridDefines.h:148` `CoordPair<MAX_NUMBER_OF_GRIDS>`; F `Grids/GridDefines.h:154` |
| `CellPair` | `CellCoord` | 1 | T `Maps/GridDefines.h:149`; F `Grids/GridDefines.h:155` |
| `ItemPrototype` | `ItemTemplate` | 1 (macro) | bot headers forward-declare `struct ItemPrototype;`, an alias cannot be redeclared as a class |
| `SpellEntry` | `SpellInfo` | 1 (macro) | same reason (`GuildOrderCraft.h:8`) |
| `Vector3` | `Movement::Vector3` | 1 | F `Movement/Spline/MovementTypedefs.h:33` `using G3D::Vector3` |
| `PointsArray` | `Movement::PointsArray` | 1 | F `Movement/Spline/MoveSplineInitArgs.h:32` |
| `sConfig` | `*ConfigMgr::instance()` | 2 | T `#define sConfig (MaNGOS::Singleton<Config,...>)`; F `#define sConfigMgr ConfigMgr::instance()` |
| `sBattleGroundMgr` | `*BattlegroundMgr::instance()` | 2 | F `Battlegrounds/BattlegroundMgr.h:200` |
| `sObjectAccessor` | proxy | 2/4 | T `ObjectAccessor.h:104-124`; F `Globals/ObjectAccessor.h:61` namespace |
| `sSpellTemplate` | proxy `LookupEntry` -> `sSpellMgr.GetSpellInfo(id, DIFFICULTY_NONE)` | 4 | T `SpellMgr.h:721` single entry per id; F `Spells/SpellMgr.h:753` needs the difficulty |
| `sLog.outX` | proxy -> `Log::instance()->OutMessage(logger, level, "{}", text)` | 4 | T `shared/Log.h:232-250` printf + levels; F `common/Logging/Log.h:120`; loggers from `worldserver.conf.dist:4726-4730` |
| `GetDeathState` | `getDeathState` | 3 | F `Entities/Unit/Unit.h:1210` |
| `GetGameObjectInfo` | `GetGameObjectTemplate` | 3 | F `Globals/ObjectMgr.h` |
| `GetGOData` | `GetGameObjectData` | 3 | F `Globals/ObjectMgr.h:1524` (same spawn-id key) |
| `GetItemPrototype` | `GetItemTemplate` | 3 | F `Globals/ObjectMgr.h` |
| `IsDead` | `isDead` | 3 | F `Entities/Unit/Unit.h:1209` |
| `GetSpellEntry(id)` | `GetSpellInfo(id, DIFFICULTY_NONE)` | 3 (function-like) | args move; F `Spells/SpellMgr.h:753` |
| `AREA_FLAG_CAPITAL` | `AreaFlags::LinkedChat` (0x100) | 4 | T `Database/DBCEnums.h:58`; F `DataStores/DBCEnums.h:112` |
| `AREA_FLAG_CITY` | `AreaFlags::LinkedChatSpecialArea` (0x200) | 4 | T `DBCEnums.h:59`; F `DBCEnums.h:113` |
| `AREA_FLAG_SLAVE_CAPITAL` | `AreaFlags::AllowTradeChannel` (0x8) | 4 | T `DBCEnums.h:53`; F `DBCEnums.h:106` |
| `MaNGOS::ComputeGridPair/CellPair` | `Trinity::ComputeGridCoord/CellCoord` | 4 | F `Grids/GridDefines.h:174,186` (same formula) |
| `WorldTimer` | inline struct over `getMSTime` | 4 | F `Timer.h` |

## CANNOT SHIM

Grouped; count = error places in the BEFORE census. Fill after the census.

