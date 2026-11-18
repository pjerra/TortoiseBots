# ForeverCore port, slice 1, round R3: header error places

Goal of the round: remove the error places that sit in HEADERS of the slice, because a header error is repeated by every
file that includes it. Measure: `FOREVER_SLOT=5 FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`.

## Numbers

| Run | Error places | Files holding them | Places in headers | Header files |
|---|---|---|---|---|
| BEFORE (baseline, `ac7f77bda`) | 2,504 | 140 | 649 | 71 |
| AFTER (this branch), census as it is | 1,730 | 97 | 224 | 37 |
| AFTER, with the registry move (R1/R2) applied by hand in the VM copy | 1,570 | 73 | 64 | 13 |

Why two AFTER rows. `AiObjectContext.cpp` is in the slice list and its constructor includes the nine registry headers, so the
census sees 24 headers that only the registry pulls in (guild, quest, loot, invitation, duel, AH, BG, taxi-release, area-trigger and
stuck-trigger headers: 158 places). R1 moves that constructor out and R2 replaces it by `SliceAiObjectContext.cpp`, which includes only
the registered classes' headers, so those headers leave the slice's include closure. They are listed as (c), not fixed. The second
row was measured by deleting the nine includes and the constructor from the VM copy of `AiObjectContext.cpp` only (nothing in the
repository), which is what R1 does. 64 places in 13 header files remain (the list at the end).

Top 15 of `where.tsv` after (census as it is; the full table is `~/forever-build/census-5/where.tsv`): PlayerbotAI.cpp 314,
MovementActions.cpp 284, WorldPosition.cpp 130, PlayerbotAISpells.cpp 106, PlayerbotAIPackets.cpp 80, GenericTriggers.cpp 40,
ChatHelper.cpp 39, ReleaseSpiritAction.h 39 (registry only), PlayerbotAIChat.cpp 34, GuidPosition.cpp 32, GenericSpellActions.cpp 32,
PartyMemberToHeal.cpp 29, GuildManagementActions.h 27 (registry only), PossibleTargetsValue.cpp 24, PlayerbotAIItems.cpp 23.
The remaining places are now almost all in `.cpp` files (R4V/R4A/R4P/R4C).

## What each header was and how it was closed (D4: a name entry, b both-core helper, c left and listed)

### WorldPosition.h (225 places) - closed, (a)/(b)

Root cause: `WorldPosition` derives from `WorldLocation` and reads `mapId, x, y, z, o` as public members; ForeverCore's
`WorldLocation` has `m_mapId` and a `Position` base with private orientation (Position.h:29-221).

**WorldLocation design (D2): the token trick worked, the fallback (an edit of the base class) was not needed.**
`forever/src/compat/TbCompat_Position.h` defines `TbLegacyWorldLocation` (public `mapId, x, y, z, o`, all default 0, `explicit`
5-argument constructor with every argument defaulted, copy, `CopyFrom`, `IsEmpty`, `Clear`: the Turtle struct,
SharedDefines.h:1901-1939), an implicit conversion FROM the core's `WorldLocation` (so a `WorldObject` converts too) and an
implicit conversion TO it, and ends with `#define WorldLocation TbLegacyWorldLocation`. The shim includes it last, after every core
header (botpch_forever.h includes them all before the shim), so no core header is ever read under the macro.
`forever/tools/check_worldlocation_scope.py` proves it: it preprocesses each source with the census flags and reports any file
outside the repository whose text holds `TbLegacyWorldLocation`. Result on the 127 slice files: 0 core headers under the macro
(one file, `AiObjectContext.cpp`, cannot be preprocessed without the census stand-ins for the registry headers; it is registry-only).

Deliberate differences: the default `TbLegacyWorldLocation` has map 0 (the core's default is `MAPID_INVALID`; the legacy code
compares with `WorldLocation()` as the all-zero value, so the Turtle default keeps its meaning); the orientation is stored as given
(Turtle does not normalise either), the core normalises it when a value is converted to the core's class.

**Every place legacy code hands a WorldLocation to the core** (grep `WorldLocation` over `ai/ runtime/ host/ commands/`, 106 lines):
 * `runtime/BotManager.cpp:611` `WorldLocation const& dest = p->GetTeleportDest()` - core to legacy, through the implicit constructor
   (a bound temporary). Not in the slice (replaced by the spike's manager).
 * `runtime/BotManager.cpp:1344` `player->SetHomebindToLocation(WorldLocation(...), area)` - legacy to core, through the conversion
   operator. Not in the slice, and that core call does not exist on ForeverCore (R-host work).
 * `forever/src/TbBotMgr.cpp:254` `me->TeleportTo(master->GetWorldLocation())` - the spike's own file; it does not include the shim,
   so the macro is not in effect there.
 * `GetNamedLocation` (`runtime/PlayerbotRuntimeFacade.cpp:305`) builds a legacy location from the bot's own table: no core call.
 * No other legacy file passes a `WorldLocation` to a core function: the formation/stance/aoe/AV code only builds and compares them.
The ten spawn/record constructors in `WorldPosition.h` that read core structs by field are the b-helpers below.

Other WorldPosition.h places, each one a both-core helper (Turtle side: `ai/cmangos-compat-r3.h`, ForeverCore side: the
`forever/src/compat` file named), the header call site changed to the helper:

| header line | was | helper | ForeverCore answer (file in F = pinned core) |
|---|---|---|---|
| ctor from `Position` | `pos.x/y/z/o` | `LocationOfCompat(mapId, pos)` (Position.h) | `GetPositionX/Y/Z`, `GetOrientation` |
| ctors from creature/object spawn pair | `data.position` | `LocationOfCompat(CreatureData/GameObjectData)` | `SpawnData::mapId` + `spawnPoint` (SpawnData.h:136) |
| ctors from SpellTargetPosition, taxi node, safe loc, PlayerInfo | field reads | `LocationOfCompat(...)` | `SpellTargetPosition = WorldLocation` (SpellMgr.h:370), `TaxiNodesEntry::ContinentID/Pos`, `WorldSafeLocsEntry::Loc`, `PlayerInfo::createPosition.Loc` |
| `getMapEntry` | `sMapStorage.LookupEntry` | `MapEntryCompat` | `sMapStore.LookupEntry` |
| `getFirstInstanceId` | `sMapMgr.Maps()` walk | `FirstInstanceIdCompat` | `DoForAllMapsWithMapId` |
| `getTerrain`, `isInWater`, `isUnderWater`, `getWaterLevel`, `getGroundLevel` | `TerrainInfo` calls | `TerrainOfCompat`, `TerrainIsInWaterCompat`, `TerrainIsUnderWaterCompat`, `TerrainWaterLevelCompat` | `TerrainInfo` takes map id and phase shift (TerrainMgr.h:100-104); the helper keeps the manager's `shared_ptr` so the raw pointer the legacy code holds stays valid; empty phase shift |
| `isCity` | `GetArea()->Flags & mask` | `AreaHasFlagsCompat` | `AreaTableEntry::Flags[0]` |
| `getVisibilityDistance` | `Map::GetVisibilityDistance`, `World::GetMaxVisible*` | `VisibilityDistanceCompat` | `Map::GetVisibilityRange`, `CONFIG_MAX_VISIBILITY_DISTANCE_CONTINENT/INSTANCE` |
| `IsInLineOfSight`, `GetHitPosition` | `Map::isInLineOfSight`, `GetLosHitPosition` | `IsInLineOfSightCompat`, `GetLosHitPositionCompat` | `isInLineOfSight` / `getObjectHitPos` with an empty phase shift |
| `getHeight`, `GetHeightInRange` | `Map::GetHeight(x,y,z,vmap[,dist])` | `GetHeightCompat` | `GetHeight(phaseShift, ...)`, same default search 10 yd |
| `getAreaFlag` | `sTerrainMgr.GetAreaFlag` | `AreaKeyAtCompat` | `TerrainMgr::GetAreaId`. **Not the same key**: T's is a per-map area flag, F's is the area id. Whoever turns it into an `AreaTableEntry` (WorldPosition.cpp, R4C) must read it the way ForeverCore does. |
| `PathFinder` type | core class | `using PathFinder = PathGenerator` | the same job; the API behind it is R4C's (WorldPosition.cpp) |

### PlayerbotAIConfig.h (6 places) - closed, (a)

`Config` was `using Config = ConfigMgr`; ForeverCore's `ConfigMgr` is a singleton with a private destructor, so it cannot be a
member, and it has no `SetSource`. `forever/src/compat/TbCompat_Config.h/.cpp` is a `Config` class of its own (the shim alias is
gone), with the Turtle class's interface: `SetSource`, `Reload`, `GetFilename`, `Get{String,Int,Float,Bool}Default`,
`GetFloatDefault(name, section, def)`, `GetStringDefaultInSection`, `GetRootSections`, `GetSections`, `GetKeys`, `GetValueHelper`
(with `ACE_TString` as `std::string`), plus the forwarding header `forever/compat/Config/Config.h` for the old include path.
`MAX_RACES` and `SLOT_EMPTY` (the two other places) are names in `TbCompat_Names.h` (T SharedDefines.h:55 and :200 against F
Player.h:758: equipment slots 0..18, end marker 19).

**What it matches.** The Turtle class wraps ACE's `ACE_Configuration_Heap` + `ACE_Ini_ImpExp`; ForeverCore has no ACE, so the
ini dialect is re-implemented. The rules were taken by running ACE 7.1.2's importer on crafted files, and the new parser was
compared with it on 44 hand-made cases, 400 random files and the two `.conf.dist` files of the repository, section by section
and key by key: 442 of 444 equal; the two differences are the ACE defect below. The repository has no place or pattern for C++
unit tests of `forever/` code (the `tools/test_*.cpp` files are core-free tests of the shared tree), so no test is shipped; the
harness was a scratch pair of programs (one linked to libACE dumping what `ACE_Ini_ImpExp` built, one including
`TbCompat_Config.cpp` dumping the same text) and a Python diff, run on the laptop once (two small single-file compiles, flock+nice).
The rules are written at the top of `TbCompat_Config.cpp`; the ones that surprised: a value starts and ends with `"` both
quotes are stripped (a lone `"` gives an empty value); `;` and `#` start a comment only at the start of a line; a line with no `=`
stops the import with code -3, a key before any section or a key holding `\ [ ]` with -4, a section line is cut at its LAST `]`
and a name holding `[ ]` is -3; section and key names are case-insensitive and a repeated key keeps its first spelling and takes
the new value; a long line is read in 4095-character chunks; **a failed import drops everything** (`Reload` deletes the store), so
after a failed `SetSource` every lookup returns its default.
`GetBoolDefault` is exact (`true TRUE yes YES 1`), `GetIntDefault` is `atoi`, `GetFloatDefault` is `atof`, as in Turtle.

**Differences from ACE, on purpose.** Enumeration order: ACE returns sections and keys in hash order, this class in file order
(a key present in several sections is found in the first section that introduced it); `aiplayerbot.conf` has one section. ACE
7.1.2 refuses `[CONF]` after `[Conf]` (code -3) while accepting `[COnf]` as the same section: here both are the same section.
The list-returning calls on an empty store return an empty list (Turtle dereferences null).
**The brief said SetSource should return true also for a missing file. It does not here, on purpose**: ACE's `import_config`
returns -1 when the file cannot be opened and the Turtle `SetSource` returns that as false, and `PlayerbotAIConfig::Initialize`
(`PlayerbotAIConfig.cpp:660-668`) depends on it: it tries the next path and prints "AI Playerbot is Disabled. No configuration file".
An empty file or a file of only comments returns true, as in Turtle. If a different behaviour was meant, it is one line in `Reload`.

**`sConfig` is not this class.** On Turtle it is the main server config (mangosd.conf); the legacy reads server keys from it
(`TortoiseBots.*`, `LogsDir`, `GetFilename()`). The shim keeps `sConfig` as the core's `ConfigMgr` (worldserver.conf + module
config), which has `GetString/Bool/Int/FloatDefault` and `GetFilename`.

### ServerFacade.h (11 places) - 6 closed, 5 left (HostileRefManager 2, battleground types 3)

Closed: `IsInRoots` (now `unit->HasAuraType(SPELL_AURA_MOD_ROOT)`, which is what the Turtle member is, Unit.h:930), `isInFront`
(`HasInArcCompat`: both cores test +-arc/2 of the facing, the arguments come in the other order), `IsWithinLOSInMap`
(`IsWithinLOSInMapCompat`), `SetFacingTo` (`GetAngleCompat`), `GetAreaId` (`GetAreaIdCompat`), the trap spell
(`GameObjectTrapSpellIdCompat`, `trap.spell`). Left: see the list.

### Combat-critical inline code (ReachTargetActions.h, RangeTriggers.h, GenericSpellActions.h, WarriorActions/Triggers.h, AttackAction.h, EnemyAreaSpell.h, UseItemAction.h states) - closed

Name entries in `TbCompat_Names.h` (no shared file changed): `UNIT_STAT_*` as the ForeverCore `UNIT_STATE_*` bit of the same fact and
every Turtle mask as the same union (T UnitDefines.h:308-379, F Unit.h:277-323; `TAXI_FLIGHT = IN_FLIGHT`, `FEIGN_DEATH = DIED`);
`MOVEFLAG_MASK_MOVING`; the `TARGET_*` names (the same numbers, T SpellDefines.h:31-61 against F SharedDefines.h:2973-3000);
`TEAM_NONE`; the `SLOT_*` names; `MAX_RACES`, `MAX_SKILL_TYPE` (counts copied from T); `IsRooted()` (the root movement flag,
as T Unit.h:701), `IsTaxiFlying()` (`IsInFlight()`), `GetAttackers` (`getAttackers`), `IsWorldBoss` (`isWorldBoss`),
`IsEvadeBecauseTargetNotReachable` (`CanNotReachTarget`), `CanReachWithMeleeAutoAttack` (`IsWithinMeleeRange`), `GetAuraDuration`
(`GetDuration`), `GetDispellMask`, `npc_flags`, `GetRequiredClasses`, `MANGOS_ASSERT`, `sTerrainMgr`. Each macro is only used as
`x->Name(...)` and no bot class defines a member of that name (grep); the names a bot class does define were not touched
(`ServerFacade::IsInRoots`, `ServerFacade::IsWithinLOSInMap`, `Formation::GetAngle`, `Stance::GetAngle`).
**Same question, each core's own answer, numbers differ:** `CanReachWithMeleeAutoAttack` (T combat reaches + leeway + z reach,
F `IsWithinMeleeRange`: 3-D distance against combat reaches + 4/3, at least the nominal range), `IsWorldBoss` (rank against boss
type flag), `IsEvadeBecauseTargetNotReachable` (3 s timer against a flag), `MOVEFLAG_MASK_MOVING` (T has pitch/jump/spline
elevation, F falling/ascending/descending).

Both-core helpers (`TbCompat_Unit.h` / `ai/cmangos-compat-r3.h`): `IsWithinLOSInMapCompat` (T `bool checkDynLos`; F
`LineOfSightChecks`: true is VMAP|GOBJECT, false VMAP, SharedDefines.h:7475-7481), `IsInLineOfSightCompat`, `GetHeightCompat`,
`GetAngleCompat` (`GetAbsoluteAngle`, null target 0 as T), `HasInArcCompat`, `GetDistanceCompat` + `SizeFactor` (F declares the
enum; T `GetDistance(obj, CombatReach)` is exactly F `GetDistance(obj)`, Object.cpp:1383-1426 against Object.cpp:431-435, the other
three factors are computed from the same members), `GetCombatReachCompat` (`GetMeleeRange`), `MeleeSpellReachCompat` (T: the block
that was in the header, moved word for word; F: `Spell::CalcMinMaxRange`, the numbers the cast tests), `GetWeaponForAttackCompat`,
`CreatureHasWeaponCompat`, `GetRespawnCoordCompat`, `HasNetworkTransportCompat` (T: a socket is attached; F: the session is not a
bot session, `WorldSession::IsBot`, set by `BotSession`), `isRealPlayer_Helper`/`IsRealPlayer_Helper`/`IsInGroup_Helper` (the Turtle shim's
three, now on ForeverCore too), `CStrCompat`, `PlayerXPCompat`, `PlayerLifetimeHonorableKillsCompat`, `UnitMin/MaxDamageCompat`,
`WorldIntConfigCompat`, `GetFactionTemplateEntryCompat`, `AreaNameCompat`, `AreaLevelCompat`, `GetWorldObjectCompat`,
`CreatureEntryOfCompat`, `CreatureNameCompat`, `UnitListSearcherCompat` (F's searcher takes the searching object for its phase),
`SpellRangeMin/Max` (the Turtle shim has them, the ForeverCore side was missing), `StrSplit`, the `strstri` declaration, `BarGoLink`
(the same do-nothing class the Turtle shim has), `LootItemList`, `GetQuestSlotIdCompat`.
Header call sites changed to the helpers (text-preserving): ServerFacade.h, PlayerbotAI.h, ExternalEventHelper.h,
GenericSpellActions.h, ReachTargetActions.h, RangeTriggers.h, WarriorActions.h, WarriorTriggers.h, WorldPosition.h, GuidPosition.h,
TravelMgr.h, Value.h, NearestUnitsValue.h, GenericTriggers.h, LeastHpTargetValue.h, LastSpellCastValue.h, StatsValues.h,
AttackerCountValues.h, GuildValues.h. The `.cpp` call sites of the same helpers are left to R4x (the helpers are there for them).

One real bug found and fixed on the way: the shim's `#define HasSpellCooldown(spellId)` (a core member rename) also rewrote the bot's
own member `CastItemTargetAction::HasSpellCooldown` (GenericSpellActions.h:309, .cpp:1353 and :1465). The member is renamed
`HasItemSpellCooldown` (3 lines, no behaviour change) so the macro stays valid for the unit calls.

### Other
 * `GuidPosition.h` (12 -> 5, census as it is also 5): `ObjectGuid::Set` becomes `Clear()` / `operator=`; the three comparison operators compare through
   `ObjectGuid`'s own operators (both cores have them); `GetWorldObject` through `GetWorldObjectCompat`; `npc_flags`.
 * `HasTotemValue.h`, `SubStrategyValue.h`, `WorldSquare.h`: name entries (`strstri`, `StrSplit`, `MANGOS_ASSERT`).
 * `NearestUnitsValue.h`, `Value.h`, `StatsValues.h`, `AttackerCountValues.h`, `LeastHpTargetValue.h`, `LastSpellCastValue.h`:
   one helper or one `IsEmpty()`/`Clear()` each.
 * `NonCombatStrategy.h`, `CombatStrategy.h`, `PullStrategy.h` are included without their folder (`generic/`). The census reported
   them as headers ForeverCore does not have and stubbed them, which broke the warrior headers (`expected class name`). Three
   forwarders in `forever/compat/` name the folder. (How the Turtle build resolves them was not looked into.)
 * `botpch_forever.h` gained three core includes in the core-header block (`PhasingHandler.h`, `TerrainMgr.h`, `Loot.h`), the only edit
   there; the shim lost the `using Config = ConfigMgr` line and gained one include block at its end, as the brief allowed.

## Shared files changed, and the gates

Files outside `forever/`: the headers listed above, `ai/cmangos-compat-shim.h` (one include at its end), the new
`ai/cmangos-compat-r3.h` (the Turtle side of the helpers: each forwards to the call the header used to make) and
`ai/playerbot/strategy/actions/GenericSpellActions.cpp` (the rename). Gate lines: see "Gates" at the end of this file.

## Left (c): 64 places in 13 header files, with what each needs

Measured with the registry move applied by hand in the VM copy (see Numbers).

| file:line | what | needed |
|---|---|---|
| `strategy/triggers/GenericTriggers.h:1385,1407,1497` | `Player::GetSpellAuraHolderMap()` (3) | an aura-holder adaptor over `Unit::GetAppliedAuras()`; bodies use the holder's `GetSpellProto`, durations, stacks |
| `GenericTriggers.h:1744-1785` | quest triage: `Quest::IsActive`, `ReqItemId/ReqItemCount/ReqSourceId/ReqSourceCount` arrays, `QUEST_ITEM_OBJECTIVES_COUNT`, `GetQuestLevel`, `GetType`, `GetRewOrReqMoney`, `QUEST_SPECIAL_FLAG_DELIVER`, `GetQuestLevelForPlayer` (17) | a quest-requirements view over `Quest::GetObjectives()`; quests are out of the slice, the trigger is never registered |
| `TaxiRouteGuard.h:37-46` | `Player::IsTaxiFlying/GetTaxi/CleanupFlagsOnTaxiPathFinished`, `MotionMaster::MovementExpired`, `ObjectMgr::GetTaxiNodeEntry` (8) | taxi is out of the slice; helpers for the five calls |
| `ServerFacade.h:136,142` (2) | `HostileRefManager` / `Unit::GetHostileRefManager` | an adaptor type with `getFirst()/next()/getSource()` over `ThreatManager::GetThreatenedByMeList()` (3 users: `AttackerCountValues.cpp:21`, `TellTargetAction.cpp:42,76`) |
| `ServerFacade.h:268,274,278` | `BattleGroundQueueTypeId`, `BattlegroundMgr::BGTemplateId` | battlegrounds are out of the slice; a type alias and one helper |
| `GuidPosition.h:13,17,18,20` | `ObjectGuid(uint64)`, `ObjectGuid(HighGuid, entry, counter)`, `CreatureData::creature_id` | a guid factory: ForeverCore's creature/object guid carries the map id and the spawn id, which these constructors do not have; design with `GuidPosition.cpp` (R4C) |
| `GuidPosition.h:86` | `ByteBuffer << WorldPosition` is deleted (the position's `ObjectGuid` base) | decide the wire form of a `GuidPosition`; only used by the chat/serialisation helpers |
| `UseItemAction.h:18,144,153,255,264` | `Spell` constructor shape, `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE`, `AddSpellAndCategoryCooldowns` | item use is out of the slice; the spell constructor is the spec's `tbhost::NewSpell` |
| `ItemUsageValue.h:14-16`, `RandomBotFacade.h:78-91` | `Item::GetItemRandomPropertyId`, `LootItem` members, `AuctionEntry` | loot and auction records; out of the slice |
| `ItemVisitors.h:294,457,593` | `FindEquipSlot` takes an `Item`, `SPELL_EFFECT_SUMMON_CRITTER` (ForeverCore's effect numbering differs: 97 is CAST_BUTTON), `SKILL_FIRST_AID` | item/skill visitors; out of the slice |
| `Talentspec.h:5,28` | `Player::GetBonusTalentCount` (no counterpart), `getConfig(CONFIG_FLOAT_RATE_TALENT)`, `TalentTabEntry` incomplete | the Turtle bonus-talent count has no ForeverCore source; `getRate(RATE_TALENT)` and the tab struct are mechanical |
| `GenericActions.h:102,104` | `CMSG_GAMEOBJ_USE` packet and `HandleGameObjectUseOpcode` | packet-handler call; out of the slice |
| `WarriorActions.h:595` (2) | `Group::GetFirstMember()` / `GroupReference` iteration | a group-member iterator helper |
| `ReachTargetActions.h:195` | `Creature::GetCombatStartPosition` | ForeverCore does not record where a creature began its fight; needs a core hook or a decision to drop the input of the evade rule (`ai::EvadeChasePolicy`) |
| `PlayerbotAI.h:73` | `ChatHandler::ExtractSpellIdFromLink` | a spell-link parser (raw id, `|Hspell:|`, `|Henchant:|`, `|Htalent:|`); chat is out of the slice |

Out of the slice by the registry move (R1/R2), measured but not fixed: `ReleaseSpiritAction.h` 39, `GuildManagementActions.h` 27,
`WithinAreaTrigger.h` 18, `QuestValues.h` 12, `StuckTriggers.h` 11, `AcceptInvitationAction.h` 10, `ChatActionContext.h` 6,
`AcceptDuelAction.h` 4, `MaintenanceValues.h` 4, `RpgSubActions.h` 3, `BlackwingLairDungeonActions.h` 3, `BlackwingLairDungeonTriggers.h` 2,
`LootRollAction.h` 2, `BattleGroundTactics.h` 2, `LeaveGroupAction.h` 2, `AhAction.h` 2, `PassLeadershipToMasterAction.h` 2 and seven
one-place headers.

## Things the next rounds must know

 * `GetDistance(x)` without a `SizeFactor` means different things: T's default is the bounding-radius form, F's `GetDistance(obj)` is the
   combat-reach form (309 uses in the tree). No place was changed; this is a silent difference, not an error. `GetDistanceCompat`
   with an explicit factor is the way to be exact.
 * `MAX_SKILL_TYPE` (763) and `MAX_RACES` (11) are Turtle's counts. Arrays sized by them are safe for the vanilla ids the bot uses and
   not for ForeverCore's skill and race ids above them (TravelMgr.h:111-112, PlayerbotAIConfig.h:761).
 * `AreaKeyAtCompat`: the key differs (see the table).
 * Both `Config` and `sConfig`: see above; `PlayerbotAIConfig::Initialize` still has to be called from `BotMgr::LoadConfig` (R5).
