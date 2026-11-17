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

| `GridPair` / `CellPair` | `GridCoord` / `CellCoord`; `MaNGOS::ComputeGridPair`/`ComputeCellPair` -> `Trinity::ComputeGridCoord`/`ComputeCellCoord` | 1/4 | as above |
| `MANGOS_OBJECT_GUID_H`, `MANGOS_OBJECTACCESSOR_H` | set, so the bot's Lens stand-in classes stay out | 4 | `runtime/BotManager.h:14`, `commands/BotCommands.cpp:140`; real headers from botpch |
| `HIGHGUID_PLAYER/UNIT/GAMEOBJECT/ITEM/PET/CORPSE/DYNAMICOBJECT` | `HighGuid::Player/Creature/GameObject/...` | 4 | T `Objects/ObjectGuid.h`; F `Entities/Object/ObjectGuid.h:108-129` (tag, not number: F numbers differ; every use compares with `GetHigh()`) |
| `ITEM_CLASS_MISC` | `ITEM_CLASS_MISCELLANEOUS` (15) | 4 | F `Entities/Item/ItemTemplate.h:489` |
| `ITEM_SUBCLASS_ARMOR_MISC` | `ITEM_SUBCLASS_ARMOR_MISCELLANEOUS` (0) | 4 | F `ItemTemplate.h:586` |
| `ITEM_SUBCLASS_FOOD` | `ITEM_SUBCLASS_FOOD_DRINK` (5) | 4 | T `ItemPrototype.h:173`; F `ItemTemplate.h:506` |
| `ITEM_SUBCLASS_WEAPON_FIST` | `ITEM_SUBCLASS_WEAPON_FIST_WEAPON` (13) | 4 | F `ItemTemplate.h:550` |
| `ITEM_SUBCLASS_WEAPON_MISC` | `ITEM_SUBCLASS_WEAPON_MISCELLANEOUS` (14) | 4 | F `ItemTemplate.h:551` |
| `UNIT_FLAG_NOT_SELECTABLE` | `UNIT_FLAG_UNINTERACTIBLE` (both 0x02000000) | 4 | F `Entities/Unit/UnitDefines.h:192` |
| `RACE_UNDEAD` | `RACE_UNDEAD_PLAYER` (5) | 4 | F `Miscellaneous/RaceMask.h:34` |
| `MAX_ITEM_PROTO_SPELLS` | `MAX_ITEM_SPELLS` (5) | 4 | T `ItemPrototype.h:421`; F `Entities/Item/Item.h:52` |
| `MAX_LOCALE` | `OLD_TOTAL_LOCALES` (9) | 4 | T `shared/Common.h:238`; F `common/Common.h:68` |
| `CONFIG_*` (18 keys) | `CONFIG_*` without the type token | 4 | F `World/World.h` enum, e.g. `CONFIG_MAX_PLAYER_LEVEL:286` |
| `sItemStorage` / `sCreatureStorage` / `sGOStorage` (LookupEntry, GetMaxEntry) | proxies over `ObjectMgr::GetItemTemplate` / `GetCreatureTemplate` / `GetGameObjectTemplate` + cached max id | 4 | T `ObjectMgr.h`: SQL stores; F `Globals/ObjectMgr.h:1086-1122` |
| `GetCharmerGuid` | `GetCharmerGUID` | 3 | F `Unit.h:1232` |
| `GetCreatureInfo` | `GetCreatureTemplate` | 3 | F `Globals/ObjectMgr.h` |
| `GetLeaderGuid` | `GetLeaderGUID` | 3 | F `Groups/Group.h:311` |
| `GetObjectBoundingRadius` | `GetBoundingRadius` | 3 | F `Unit.h:724` (same client field; units only) |
| `GetOwnerGuid` | `GetOwnerGUID` | 3 | F `Unit.h:1213` |
| `GetSpellProto` | `GetSpellInfo` (aura/spell's own spell) | 3 | F `User.h`/`SpellAuras.h:217` area |
| `SetSelectionGuid` | `SetSelection` | 3 | F `Player.h:1979` writes `UF::UnitData::Target`; `SetTarget` is a no-op for players |
| `IsMoving` | `isMoving` | 3 | F `Unit.h:1829` |
| `getThreat` | `GetThreat` | 3 | F `Combat/ThreatManager.h:131` |
| `getOpcode` | `GetOpcode` | 3 | F `Server/WorldPacket.h:62` |
| `HasSpellCooldown(id)` | `GetSpellHistory()->HasCooldown(id)` | 3 (function-like) | F `Spells/SpellHistory.h:172`, `Unit.h:1528` |
| `GetAreaEntryByAreaID` | `sAreaTableStore.LookupEntry` | 4 | F `DataStores/DB2Stores.h:40` |

## CANNOT SHIM

Grouped; count = error places in the BEFORE census. Fill after the census.


## Subset runs

| after | subset | places |
|---|---|---|
| BEFORE | `ai/playerbot/strategy/values` | 2,717 (CENSUS.md row 2) |
| batch 1 | `ai/playerbot/strategy/values` | 1,748 |
| batch 3 | `ai/playerbot/strategy/values` | 1,682 |
| first run | `ai/playerbot/strategy/triggers` | 688 |
| after the spell helpers | `ai/playerbot/strategy/triggers` | 687 |
