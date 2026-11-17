# ForeverCore port: shim round 1

One header (`forever/src/compat/forever-compat-shim.h`) filled in; no bot code changed. Counts are
`forever/tools/port_census.py` error places at the pinned core (`forever/PINNED_CORE`).

## Numbers

| Run | What | Error places |
|---|---|---|
| BEFORE | full census, the shim as committed at 7808b02ae | 17,530 in 570 files, 0 clean files |
| AFTER | full census, the shim as committed at a7a640591 | 13,496 in 538 files, 11 clean files |

## Entries

`kind`: 1 = type name, 2 = singleton, 3 = member name, 4 = free function / proxy.
"Meaning checked" is the line that shows the same thing on both cores (T = Tortoise core, F = ForeverCore).

| old name | ForeverCore | kind | meaning checked |
|---|---|---|---|
| `CreatureInfo` / `GameObjectInfo` / `GenericTransport` / `GuidVector` / `ObjectGuidSet` | `CreatureTemplate` / `GameObjectTemplate` / `Transport` / `std::vector<ObjectGuid>` / `GuidSet` | 1 | F `Entities/Creature/CreatureData.h`, `Entities/GameObject/GameObjectData.h`, `Entities/Transport/Transport.h`, `Entities/Object/ObjectGuid.h` |
| `UnitAI` alias (removed) | core has its own `UnitAI`, the AI base class | — | T `ai/cmangos-compat-shim.h:96` alias only because Tortoise has no `UnitAI`; F `AI/CoreAI/UnitAI.h:50` |
| `BattleGroundTypeId` | `BattlegroundTypeId` | 1 | T `SharedDefines.h:1742`, F `Miscellaneous/SharedDefines.h:6944` (AV/WS/AB = 1/2/3 both) |
| `Config` | `ConfigMgr` | 1 | T `shared/Config/Config.h:34`, F `common/Configuration/Config.h:26` |
| `CreatureDataPair` | `std::pair<ObjectGuid::LowType const, CreatureData>` | 1 | T `ObjectMgr.h` creature data map value type; F `ObjectMgr.h:490,1502` `GetAllCreatureData()` (keyed by `ObjectGuid::LowType`, 64 bit; the record is the same) |
| `GameObjectDataPair` | `std::pair<ObjectGuid::LowType const, GameObjectData>` | 1 | F `ObjectMgr.h:503,1523` same shape |
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
| `sLog.outX` (files that do not include `playerbot.h`, which points `sLog` at the bot's `BotLog`) | proxy -> `Log::instance()->OutMessage(logger, level, "{}", text)` | 4 | T `shared/Log.h:232-250` printf + levels; F `common/Logging/Log.h:120`; loggers from `worldserver.conf.dist:4726-4730` |
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
| `CONFIG_*` (18 keys, e.g. `CONFIG_UINT32_MAX_PLAYER_LEVEL`) | `CONFIG_*` without the type token | 4 | F `World/World.h` enum, e.g. `CONFIG_MAX_PLAYER_LEVEL:286` |
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

Counts are error places in the AFTER census (`names.tsv`/`where.tsv`); examples are `file:line` with the code as it
reads today. The last column is how ForeverCore gets the same fact.

### 1. Item template fields
count: 1,168
examples: `ai/playerbot/strategy/values/ItemUsageValue.cpp:184` `proto->Class == ITEM_CLASS_WEAPON`,
`ai/playerbot/strategy/values/ItemUsageValue.cpp:167` `proto->InventoryType`
why: `ItemTemplate` keeps the record private (`BasicData`/`ExtendedData`) and reads it through accessors; a field
macro cannot turn `proto->Class` into `proto->GetClass()`.
way: `GetId()`, `GetClass()`, `GetSubClass()`, `GetQuality()`, `GetInventoryType()`, `GetDelay()`, `GetBonding()`,
`GetBuyPrice()`, `GetSellPrice()`, `GetArmor(itemLevel)`, `GetDamage(itemLevel, min, max)`, `GetBaseRequiredLevel()`,
`GetAllowableClass()`, `GetRequiredSkill()`, `GetMaxStackSize()`, `GetName(locale)`, `HasFlag(...)` —
`Entities/Item/ItemTemplate.h:870-992`.

### 2. Spell effect and spell data arrays
count: 738
examples: `ai/playerbot/PlayerbotAISpells.cpp:88` `aura->GetSpellProto()->StackAmount`,
`ai/playerbot/strategy/values/ItemUsageValue.cpp:95` `proto->Spells[i].SpellId`
why: ForeverCore splits a spell into `SpellInfo` plus `SpellEffectInfo` objects and item spells into a separate
record; `Effect[i]`, `EffectApplyAuraName`, `EffectMiscValue`, `MAX_EFFECT_INDEX` (3) have no one-to-one names
(`MAX_SPELL_EFFECTS` is 32 there).
way: `SpellInfo::GetEffect(SpellEffIndex)` / `GetEffects()`, `SpellEffectInfo::{Effect, ApplyAuraName, MiscValue,
BasePoints, TriggerSpell, ItemType}` — `Spells/SpellInfo.h:263,521`; item spells through
`ItemTemplate::GetSpell(ItemSpelltriggerType)`.

### 3. Update fields
count: 357
examples: `ai/playerbot/LootObjectStack.cpp:232` `creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE)`,
`ai/playerbot/strategy/actions/...` `bot->GetUInt32Value(PLAYER_FLAGS)`
why: ForeverCore has no numbered update fields; the values live in typed `UF::` structures with change masks, and
`HasFlag(field, flag)` does not exist.
way: `Unit::HasUnitFlag(UnitFlags)` (`Entities/Unit/Unit.h:862`), `Player::HasPlayerFlag(PlayerFlags)`
(`Entities/Player/Player.h:1306`), values through the typed `UF::` structures — `Entities/Object/Updates/`.

### 4. ObjectGuid model
count: 596
examples: `ai/playerbot/PlayerbotAIChat.cpp:135` `ObjectGuid(HIGHGUID_PLAYER, player.first)`,
`ai/playerbot/GuidPosition.h:78` `getRawGuid() == other.getRawGuid()`
why: the guid is 128 bit in ForeverCore (`std::array<uint64,2>`, `GetRawValue()` returns a 16-byte span), no
`ObjectGuid(high, low)` constructor, no `Set(high, low)`.
way: `ObjectGuidFactory::CreatePlayer(realmId, subType, arg1, dbId)`, `CreateItem`, `CreateWorldObject`,
`ObjectGuid::FromString`, `GetCounter()`, `GetRawValue(i)` — `Entities/Object/ObjectGuid.h:287-289,320-400`.

### 5. Loot model
count: 146
examples: `ai/playerbot/strategy/values/LootValues.cpp:21` `std::vector<LootItem*> LootAccess::GetLootContentFor(Player* player)`,
`ai/playerbot/LootObjectStack.cpp:232` `creature->loot.isLooted()`
why: ForeverCore's `Loot` holds `LootItem` differently (`std::vector<LootItem>`, `LootStoreItem` in `LootStore`),
and rolls are `RollVote`/`LootRoll` driven by the loot object.
way: `Loot::items` (`std::vector<LootItem>`), `Loot::gold`, `Loot::isLooted()`, `Player::GetLootGuid()` —
`Loot/Loot.h:297-320`.

### 6. Packets
count: 76
examples: `ai/playerbot/PlayerbotAIPackets.cpp:21` `static_assert(... == SMSG_FORCE_WALK_SPEED_CHANGE, ...)`,
`ai/playerbot/...` `WorldPackets::Trade::CancelTrade`
why: ForeverCore builds packets as typed `WorldPackets::` structs and sends them through `SendDirectMessage`; the
modern client's opcodes and field layouts are different.
way: `WorldPackets::*` in `Server/Packets/`, `WorldSession::SendDirectMessage`.

### 7. Database / query API
count: 394
examples: `runtime/GuildOrderStore.cpp:291` `std::unique_ptr<QueryResult> result(CharacterDatabase.Query(select.c_str()));`,
`runtime/GuildOrderStore.cpp:296` `Field* fields = result->Fetch();`
why: ForeverCore returns `PreparedQueryResult` and reads `Field` via `GetString()`/`GetUInt32()` on the result row
set; `Fetch()`/`NextRow()` are gone.
way: `CharacterDatabase.Query(...)` returning `PreparedQueryResult`, `result->Fetch()`, `Field::GetString()` /
`GetInt32()` — `server/database/Database/Field.h:74-80`.

### 8. Battlegrounds
count: 163
examples: `ai/playerbot/ServerFacade.h:268` `BattleGroundTypeId`,
`ai/playerbot/...` `sBattleGroundMgr.GetBattleGround(bgId, type)`
why: `BattleGround*` per type is `Battleground*` per instance in ForeverCore, the queue id is a packed struct
(`BattlegroundQueueTypeId`, not an enum), and the template is `BattlegroundTemplate` settings, not a live object.
way: `sBattlegroundMgr->GetBattleground(instanceId, queueId)` (`Battlegrounds/BattlegroundMgr.h:110`),
`GetBattlegroundTemplateByTypeId` (`:151`), `Player::GetBattleground()` (`Player.h:2715`),
`BattlegroundQueueTypeId::FromPacked` (`Miscellaneous/SharedDefines.h:7200`).

### 9. Positions (WorldLocation / Position fields)
count: 795
examples: `ai/playerbot/WorldPosition.h:46` `WorldLocation(mapId, x, y, z, o)`,
`ai/playerbot/WorldPosition.cpp:493` `area->ZoneId`
why: ForeverCore keeps `Position` coordinates private behind `GetPositionX()`...; DBC position records use
`PosX`/`PosY`/`PosZ`/`MapID` (PascalCase) and `ParentAreaID` for the zone.
way: `Position::{GetPositionX, GetPositionY, GetPositionZ, GetOrientation, GetMapId}`, `AreaTableEntry::ParentAreaID`,
`TaxiNodesEntry::{MapID, PosX, PosY, PosZ}`, `WorldSafeLocsEntry::{MapID, LocX, LocY, LocZ}`.

### 10. Grids, terrain and pathfinding
count: 177
examples: `ai/playerbot/WorldPosition.h:360` `static void ExcludeSteepSlopes(PathFinder& pathfinder)`,
`ai/playerbot/WorldPosition.h:306` `sTerrainMgr.GetAreaId(x, y, z)`
why: there is no `PathFinder`/`TerrainInfo`/`GridMapLiquidData` and no `Map::GetHitPosition`; ForeverCore paths with
`PathGenerator` and reads terrain through `Map::GetTerrain` / `TerrainInfo`.
way: `PathGenerator::{CalculatePath, GetPath, GetPathType, GetEndPosition}` (`Movement/PathGenerator.h:76-102`),
`Map::GetTerrain()`,
`Map::GetHeight(phase, x, y, z)` (`Maps/Map.h:330`), `Map::GetWaterOrGroundLevel`, `Map::getObjectHitPos(...)` (the
line-of-sight hit point, `Maps/Map.h:519`).

### 11. Turtle-only host bits (LFT, MasterPlayer, Lua)
count: 77
examples: `runtime/LftBotFillService.cpp` `sLFTMgr`, `ai/playerbot/...` `MasterPlayer`
why: Turtle spells these; ForeverCore has no LFT (`DungeonFinding` is the modern LFG), no `MasterPlayer` (a Turtle
cache of offline character data) and no Eluna.
way: `sLFGMgr` for the dungeon finder (`DungeonFinding/LFGMgr.h:526`), `sCharacterCache` for offline character
facts (`Cache/CharacterCache.h:83`).

### 12. Talents (Turtle's custom talent tables)
count: 32
examples: `ai/playerbot/Talentspec.h:5` `bot->GetBonusTalentCount()`, `Talentspec.h:28` `TalentTabEntry`
why: Turtle has its own talent spec tables; ForeverCore keeps talents in `TalentTabEntry`/`TalentEntry` DB2 stores
with a different shape (`GetTalentSpellPos`, `GetTalentTabInfo`).
way: `Player::GetTalentMap(spec)`, `Player::LearnTalent`, `sTalentStore` — `Entities/Player/Player.h:2085-2105`,
`DataStores/DB2Stores.h:278`. There is no "bonus talent points" on ForeverCore; that number is Turtle's own system.

### 13. Guild ranks, reputation and trainers
count: 177
examples: `ai/playerbot/PlayerbotAIChat.cpp:159` `guild->HasRankRight(bot->GetRank(), GR_RIGHT_GCHATSPEAK)`,
`ai/playerbot/GuildOrderEnchants.cpp:120` `co->trainer_type != TRAINER_TYPE_TRADESKILLS`
why: guild ranks are `GuildRankId` on `Guild::m_ranks` and the right test is private
(`Guild::_HasRankRight(Player const*, uint32)`, `Guilds/Guild.h:917`); reputation reads need a `FactionEntry const*`
(`ReputationMgr::GetRank/GetReputation`, `Reputation/ReputationMgr.h:103-110`); creature trainer data is
`Trainer::Trainer` behind `sObjectMgr->GetTrainer(trainerId)` (`Globals/ObjectMgr.h:1627`), not `trainer_type` on
the template. `HasRankRight` and `TrainerSpellData` have no name counterpart at all.
way: guild rights need either a public accessor (a core request) or the bot-side rank model; reputation via
`Player::GetReputationMgr()` with a `FactionEntry`; trainers via `sObjectMgr->GetTrainer` / `Creature::GetTrainerId()`.

### 14. Aura holders (Turtle's AuraHolder layer)
count: 5
examples: `ai/playerbot/PlayerbotAISpells.cpp:180` `aura->GetHolder()->GetAuraDuration()`
why: ForeverCore has no `AuraHolder`/`SpellAuraHolder`; one aura application per caster and one effect per aura.
way: `Unit::GetAura(spellId, casterGuid)`, `Aura::GetDuration()`, `Aura::GetMaxDuration()` —
`Spells/Auras/SpellAuras.h:217`.

### 15. Spell helpers that need the caster
count: 40
examples: `ai/playerbot/PlayerbotAISpells.cpp:1328` `GetSpellCastTime(pSpellInfo, bot, spell)`,
`ai/playerbot/PlayerbotAISpells.cpp:91` `IsPositiveSpell(aura->GetSpellProto())` (done) and
`IsPositiveSpell(info, caster, victim)`
why: ForeverCore's `SpellInfo::CalcCastTime(Spell*)` applies the caster's modifiers only through a live `Spell`;
there is no caster-aware positive check.
way: `Spell::GetCastTime()`, `SpellInfo::CalcCastTime(spell)`, `SpellInfo::IsPositive()`.

### 16. Core objects' lower-case getters (getName and friends)
count: -- (spread over `override`/`unknown type` messages)
examples: `ai/playerbot/Strategy.h:64` `std::string GetName() { return getName(); }` — the bot's own classes define
both spellings, so a global `#define getName GetName` redeclares those members.
why: not shimmable by macro; the bot-side headers keep both spellings.
way: `Object::GetName()` (ForeverCore); the bot classes' `GetName()` forwarders already exist.

### 17. Per-class template getters (`GetProto`)
count: 326
examples: `ai/playerbot/strategy/values/ItemUsageValue.cpp:190` `item->GetProto()->ItemLevel`
why: cmangos had `GetProto()` on Item, Creature, GameObject and Pet returning the template; ForeverCore names each
one after the template.
way: `Item::GetTemplate()`, `Creature::GetCreatureTemplate()` (pets are creatures too), `GameObject::GetGOInfo()`
(`Entities/GameObject/GameObject.h:217`).

### 18. Server configuration reads
count: 85
examples: `ai/playerbot/Talentspec.h:5` `sWorld.getConfig(CONFIG_FLOAT_RATE_TALENT)`
why: ForeverCore has no `World::getConfig`; rates are read through `getRate(Rate)` and typed getters.
way: `World::getRate(RATE_TALENT)`, `getFloatConfig`, `getIntConfig`, `getBoolConfig` — `World/World.h:684`.

### 19. Melee reach and movement flags
count: 44 + 30
examples: `ai/playerbot/strategy/actions/BossTacticsActions.cpp:11810` `tank->CanReachWithMeleeAutoAttack(unit)`,
`ai/playerbot/...` `MOVEFLAG_ONTRANSPORT`
why: cmangos's melee reach has a separate Z limit and a flat modifier; ForeverCore uses `IsWithinMeleeRange` /
`GetMeleeRange`. `MOVEFLAG_ONTRANSPORT`/`MOVEFLAG_JUMPING` have no `MOVEMENTFLAG_` counterpart (the flag set was
renumbered), and `MOVEFLAG_MASK_MOVING` covers different bits.
way: `Unit::IsWithinMeleeRange(obj)`, `Unit::GetMeleeRange(target)`, `MovementInfo::HasMovementFlag(...)` —
`Entities/Unit/Unit.h:727-729`, `Entities/Object/MovementInfo.h:30-100`.

### 20. Taxi flight
count: 114
examples: `ai/playerbot/PlayerbotAI.cpp:644` `!bot->IsTaxiFlying()`
why: ForeverCore has no `IsTaxiFlying`; the player's route is `Player::GetTaxi()` and the movement state is
`UNIT_STATE_IN_FLIGHT` (`IsInFlight()`), which is a superset (any flight, not only a taxi).
way: `Player::GetTaxi().empty()` (`Entities/Player/PlayerTaxi.h:92`), `Unit::IsInFlight()` (`Unit.h:1044`).


## Subset runs

| after | subset | places |
|---|---|---|
| BEFORE | `ai/playerbot/strategy/values` | 2,717 (CENSUS.md row 2) |
| batch 1 | `ai/playerbot/strategy/values` | 1,748 |
| batch 3 | `ai/playerbot/strategy/values` | 1,682 |
| first run | `ai/playerbot/strategy/triggers` | 688 |
| after the spell helpers | `ai/playerbot/strategy/triggers` | 687 |
| batch 4 | `ai/playerbot/strategy/generic` | 442 |

### 21. printf sinks that now receive `std::string`
count: 290 (`const std::string`), plus the same cause inside other messages
examples: `ai/playerbot/PlayerbotAI.cpp:674` `sLog.outDetail("%s: Jump: Landed, landTime: %u", bot->GetName(), curTime)`,
`ai/playerbot/PlayerbotAI.cpp:1600` `sLog.outBasic("[BOT] fight-start-hold open %s", bot->GetName())`
why: `playerbot.h:37` points `sLog` at the bot's own `BotLog` (`ai/playerbot/BotLog.h:34`, printf-attributed), and
`Player`/`Object::GetName()` returns `std::string` on ForeverCore where cmangos returned `char const*`. Passing the
string to `%s` is a compile error (`-Wnon-pod-varargs`); the same holds for `ChatHandler::PSendSysMessage`.
way: pass `.c_str()` (a bot-code change), or keep a local `char const* name = bot->GetName().c_str();`.

### 22. Headless session tests
count: 152 (`HasNetworkTransport` 48, `IsHeadless` 66, `HeadlessSessionState` 38)
examples: `ai/playerbot/...` `session->HasNetworkTransport()`, `runtime/BotManager.cpp` `IsHeadless()`
why: Tortoise marks a bot session as "no socket"; ForeverCore has one flag for that on the session.
way: `WorldSession::IsBot()` — `Server/WorldSession.h:1078` (this core is the bot-capable fork).

### 23. Item random properties / suffixes
count: 6
examples: `ai/playerbot/ChatHelper.cpp:547` `sItemRandomPropertiesStore.LookupEntry(abs(itemPropertyId))`
why: there is no `ItemRandomPropertiesEntry`/`sItemRandomPropertiesStore` (the id → five enchant ids + name table);
ForeverCore resolves the same ids through the spell item enchantment store and the item enchantment manager.
way: `sSpellItemEnchantmentStore` (`DataStores/DB2Stores.h:253`), `ItemEnchantmentMgr.h:27`
`GetRandomPropertyPoints`, `Item::SetItemRandomProperties`/`Item::GetItemRandomPropertyId`.

### 24. Turtle-only headers the bot includes
count: 37 includes (census `fatal.tsv`)
examples: `ScriptObjects.h`, `Group/CreatureLinkingMgr.h`, `LFT/LFTMgr.h`, `MapNodes/MasterPlayer.h`,
`Battlegrounds/BattleGroundAB.h`, `Config/Config.h`
why: these are Tortoise headers or paths; ForeverCore names the include folders differently or has no equivalent
(`Config/Config.h` on Tortoise is reachable as `Config.h` here — `common/Configuration/Config.h`; LFT,
CreatureLinking, MasterPlayer and ScriptObjects do not exist).
way: include the ForeverCore header by its name (`Config.h`); the rest are part of the LFT / Turtle
work that stays off on ForeverCore (see `PORT-PLAN.md`).

### 25. Quest template fields
count: 162
examples: `ai/playerbot/strategy/values/QuestValues.cpp` `quest->RewChoiceItemId[i]`, `community` `quest->ReqItemId[i]`,
`quest->QuestFlags`
why: cmangos keeps request and reward items as flat arrays on the quest template. ForeverCore keeps the reward items
(`Quest::RewardChoiceItemId`/`RewardItemId`, `Quests/QuestDef.h:753-757` — a rename away, but a field, so a bot-code
edit and not a shim) while the request (turn-in) items moved into the objective model, flags are `QuestFlags`, and the
level is `GetClassicQuestLevel()`/`GetClassicMinLevel()` or `Player::GetQuestLevel(Quest const*)` (`Player.h:1758`).
way: `Quest::Objectives` / `QuestObjective` (`Quests/QuestDef.h:477`), `Quest::HasFlag(QuestFlags)`,
`Player::GetQuestLevel(Quest const*)`.

### 26. Trade and auction
count: 249
examples: `ai/playerbot/RandomBotFacade.h:79` `std::vector<AuctionEntry> GetAhPrices(uint32 itemId)`,
`ai/playerbot/strategy/actions/TradeStatusAction.cpp` `TradeData`/`TRADE_SLOT_*`
why: there is no `AuctionEntry` in ForeverCore at all (`grep AuctionEntry src` finds nothing): auctions are the
bucket/search model in `AuctionHouse/AuctionHouseMgr.h` (results as `AuctionListItemsResult` and `AuctionItem`
records), and trade state is the `TradeData` class with its own slot enum.
way: `AuctionHouseObject::AuctionItem` and the search results in `AuctionHouse/AuctionHouseMgr.h:42-48`,
`WorldPackets::AuctionHouse::*`; `TradeData` is `Entities/Player/TradeData.h:34`.

### 27. AreaTable flags are an array now
count: 21 (`AREA_FLAG_*` tests written as `area->Flags & AREA_FLAG_X`)
examples: `ai/playerbot/PlayerbotAI.cpp:4653` `return current_area->Flags & AREA_FLAG_CAPITAL;`,
`ai/playerbot/TravelMgr.cpp:611` `if (!destZone || !(destZone->Flags & AREA_FLAG_CAPITAL))`
why: `AreaTableEntry` keeps `std::array<int32, 2> Flags` (two flag words) and offers typed accessors; `Flags` is no
longer a `uint32` bitmask, so `&` cannot work whatever the constant is. The shim still provides `AREA_FLAG_*` as
`AreaFlags` values for the `HasAreaFlag(AreaFlags)` parameter the bot already declares.
way: `AreaTableEntry::HasFlag(AreaFlags::LinkedChat)`, `GetFlags()` / `GetFlags2()` —
`DataStores/DB2Structure.h:144-150`.

### 28. Summon duration is a chrono type
count: 45
examples: `ai/playerbot/strategy/actions/DebugPositionCommands.cpp:409`
`bot->SummonCreature(2334, x, y, z, 0, TEMPSPAWN_TIMED_DESPAWN, 20000.0f)`
why: same arguments in the same order, but ForeverCore's despawn time is `Milliseconds` (std::chrono) where cmangos
took a float/int; the enum already matches.
way: `SummonCreature(entry, x, y, z, o, TempSummonType, Milliseconds(20000))` —
`Entities/Object/Object.h:421-422`.
