# ForeverCore port, slice 2, package V2: corpse money loot, loot values, the loot object stack, quest relation names

Branch `slice2/v` (worktree `~/wt-slice2-v`), build slot 1. T = Turtle core (`~/playerbots-references/penqle-tortoise-wow-modsys/src/game`), F = pinned
ForeverCore (`~/ForeverCore/src/server/game`). Lead's decision D2: the bot loots MONEY from the corpses of creatures its group killed; item decisions,
game object loot, gathering and skinning are named unported features.

## 1. Numbers

`FOREVER_SLOT=1 FOREVER_CENSUS_LIST=forever/SLICE2-V2.txt forever/tools/vm-census.sh "" 14`

| | places |
|---|---|
| BEFORE (`d97cc42b9`) | 101 in 6 files: LootValues.cpp 48, LootObjectStack.cpp 22, QuestValues.h 16, CorpseLootPolicy.h 13 (I2's), NearestCorpsesValue.cpp 1, PartyMemberToDispel.cpp 1 |
| AFTER | **13**, all in `policy/loot/CorpseLootPolicy.h` (owned by I2); every file V2 owns is clean (`compile clean 5`) |

Of the 88 places V2 owns, by method: (a) 39 (a name or type that means the same: `go->GetEntry()` for the template id, `std::max<uint32>`, `IsEmpty()` for the bool tests,
`LootStoreItemCompat` rows, `GuidFromNumbersCompat`, `GuidTextCompat` / `GuidKeyCompat`, `CStrCompat`, the `QuestRelationsMap` alias, the DIALOG_STATUS names); (b) 16 (a both-core helper, call site
rewritten, same answer: corpse rights, the loot of an object, the qualifier text, the grid search, mini pets); (c) 1 (the corpse approach distance, section 4); counted unported helpers 32
(loot items 14, loot tables 7, game objects and skinning 3, quest relation maps 8); left open in V2's files 0. (Tally by hand from the baseline error list; one line can hold two places.)

AlwaysLootListValue.cpp and LootStrategyValue.cpp compiled clean at the baseline and needed no change. LootStrategyValue.cpp:25 reads `ItemUsage` (the `item usage` value is not
registered in this slice): the missing-key stand-in answers ITEM_USAGE_NONE, and the default loot strategy (`equip,quest,skill,disenchant,use,vendor`) has no `trash`, so nothing
is "looted" by that route (the R2 rule: fail toward less). Nothing to port there.

## 2. What was ported properly (F has the thing; no counter)

| Question | T | F (file:line in `~/ForeverCore/src/server/game`) | helper |
|---|---|---|---|
| May this bot open this corpse | `Player::IsAllowedToLoot(creature)` | `Player::isAllowedToLoot` Player.h:2962, body Player.cpp:20246 (dead, no pending bind, a Loot for this player not looted, player in the allowed looters, gold or items for all / for the player, then the loot method's turn rule). It is the test the core masks the lootable flag with per viewer (Updates/ViewerDependentValues.h:76) and that CMSG_LOOT applies (Handlers/LootHandler.cpp:61, :214) | `CorpseAllowedToLootCompat` |
| Which Loot is the bot's | `creature->loot` (one per creature) | `Creature::GetLootForPlayer` Creature.cpp:1438: the personal loot of this player, else the shared `m_loot`, nullptr when the player has none. Open world: every tapper has a personal Loot (Entities/Unit/Unit.cpp:11965-11983); a grouped kill in a dungeon is one shared Loot under the group's loot method (Unit.cpp:11896-11922) | `CreatureLootForCompat`, `LootOfObjectCompat`, `LootOfGuidCompat` |
| Whose turn | `loot.roundRobinPlayer` (uint64) | `Loot::roundRobinPlayer` Loot.h:299 (ObjectGuid, set to the loot owner for group loot, Loot.cpp:959; empty for personal loot) | `CorpseRoundRobinGuidCompat` |
| Master looter | `Group::GetLooterGuid()` (T Group.h:223) | `Group::GetMasterLooterGuid()` Group.h:316 (F's `GetLooterGuid` is the round-robin pointer, Group.cpp:1675) | `GroupMasterLooterGuidCompat` |
| Money in the corpse | `loot.gold` | `Loot::gold` Loot.h:297; gold counts as "something for all" (Loot.cpp:1184-1190), so a money-only corpse passes `isAllowedToLoot`. Taking it is HandleLootMoneyOpcode (LootHandler.cpp:142-190: split with allowed group members near the looter, then `Loot::LootMoney`); that is I2's action | read directly (`objLoot->gold`) |
| Corpse distance | `IsWithinDistInMap(bot, GetMaxLootDistance(creature), true, SizeFactor::None)` | the server accepts `IsWithinDist(creature, 30 yd)` plus both combat reaches (AELootCreatureCheck, LootHandler.cpp:41, :50, :214). The bot keeps T's approach formula (`max(INTERACTION_DISTANCE 5.0, max(1.5, own reach) + 4/3 + max(1.5, corpse reach))`, ObjectDefines.h:24), centre to centre, 3D (`IsWithinDistInMap(.., true, false, false)`, Object.h:362): always inside what F accepts, so "in range" never earns a "too far" refusal and the bot walks up to the corpse as on T | `CorpseInLootRangeCompat` |
| Guid in a value qualifier | `std::to_string(guid.GetRawValue())` / `stoull` | a 128-bit guid has no 64-bit raw value; the hash of `GuidKeyCompat` cannot be turned back. The lossless `high/low` text of `GuidWriteCompat` / `GuidReadCompat` (TbCompat_C.h) | `GuidQualifierCompat`, `GuidFromQualifierCompat` |
| Grid search for dead units | `UnitListSearcher(list, check)` + `Cell::VisitAllObjects` | `VisitUnitsCompat` (R4V) | reused |
| Mini pets | `getPetType() == MINI_PET` | `PetIsMiniPetCompat` (R4V; F has no such pet type, false) | reused |

**Threading, per loot call (the brief asks).** Every call in this package is a READ: `isAllowedToLoot`, `GetLootForPlayer`, `Loot::gold` / `roundRobinPlayer` / `unlootedCount` /
`hasItemFor` / `HasAllowedLooter`, `Map::GetCreature(guid)`, `Player::GetLootGUID`. None takes a map context or a lock and none writes. The loot handlers that DO write (open: `Player::SendLoot` ->
`Loot::OnLootOpened`; take: `HandleLootMoneyOpcode`; release: `DoLootRelease`) are PROCESS_THREADUNSAFE (Opcodes.cpp:689-693), i.e. world-thread calls with no map update context: that is the
engine's own position (it ticks on the world thread right before the maps update, World.cpp:2462-2475), and they are I2's. Still unproven, as in findings item 2: that no map thread touches the same
corpse's Loot while a tick runs; the reads above would then see a torn value for one tick (a refused open, never a crash through them).

## 3. Unported rows added (`ai/playerbot/UnportedFeatures.h`, block `slice 2, package V2` at the end; `forever/docs/UNPORTED.md`)

Rule: a fallback makes the bot do LESS (false / null / empty). Each call site's handling of the value:

| Row | Helper (F) | Call site | Value on F | Handling at the call site | Polarity |
|---|---|---|---|---|---|
| loot: items | `CorpseHasVisiblePersonalItemForCompat` | LootObjectStack.cpp MayLootCorpse (last line) | false | MayLootCorpse answers false for another member's turn: `LootObject::Refresh` does not set the guid, the bot does not walk to it | LESS |
| loot: items | `LootContentForCompat` | LootValues.cpp `LootAccess::GetLootContentFor`, read by ShouldLootObject's item loop | empty | the loop does not run; ShouldLootObject answers true only through `gold > 0` (checked before the loop) | LESS |
| loot: items | `LootStatusForCompat` | `LootAccess::GetLootStatusFor` -> `IsLootedFor` (no caller in the tree) | gold bit only | none reads it | n/a |
| loot tables | `LootTemplateOfGuidCompat` | `DropMapValue::GetLootTemplate` (DropMap, ItemDropMap, LootChance values; RandomItemMgr and GuildOrderSources, outside the slice) | nullptr | drop maps stay empty, `entry loot list` empty, `loot chance` 0, `HasUpgradeValue` false: no upgrade is chased, no quest-drop is known | LESS |
| loot tables | `GameObjectLootTemplateCompat`, `ReferenceLootTemplateCompat` | `IsCommonWorldProp`, `CollectCommonChestLoot` | nullptr | `IsCommonWorldProp` false ("not a junk chest"); reachable only for a game object guid, which `LootObject::Refresh` never keeps on F (next row) | LESS |
| game object loot, gathering and skinning | `GameObjectLootAllowedCompat` | `LootObject::Refresh` game object branch | false | the branch is skipped: no game object is ever a loot target (`Add`, `IsLootPossible`, `GetWorldObject` all see an empty guid) | LESS |
| game object loot, gathering and skinning | `CorpseSkinnableByCompat` | `LootObject::Refresh` | false | no skinning target; counted only for a corpse that carries the skinnable flag | LESS |
| game object loot, gathering and skinning | `AbsentLootMeansNothingToTakeCompat` | `ShouldLootObject::Calculate`, `!objLoot` branch | true -> the value answers false | creature: this bot has no loot there (T never reaches the branch: it always has `creature->loot`; without this helper the branch's `return true` would have sent the bot to a corpse the core refuses); game object: not ported | LESS |
| quest internals (existing row) | `QuestRelationsMapCompat` behind four macros | QuestValues.h `QuestObjectMgr::Get*QuestRelationsMap()` (reader: QuestValues.cpp, not in the slice) | empty map | no giver and no turn-in is known | LESS |

## 4. Different answers (both definitions)

1. **Guid qualifier text** of `should loot object`: T a decimal number, F `high/low`. Every producer of that qualifier must call `GuidQualifierCompat` (LootAction.cpp:795 still has `std::to_string(guid.GetRawValue())`: I2).
2. **Master looter**: T `Group::GetLooterGuid()` under MASTER_LOOT; F `GetMasterLooterGuid()` (F's `GetLooterGuid` is the round-robin pointer). Same intent, different accessor.
3. **Loot per player**: T one `Loot` per creature, so every bot of the group sees the same `gold`; F in the open world gives each tapper its OWN Loot with its own gold (Unit.cpp:11965-11983), so each bot opens and takes its own money; in a dungeon the shared Loot is split by the core
   (LootHandler.cpp:149-176). A bot that is not a tapper has no Loot (`GetLootForPlayer` nullptr) and `ShouldLootObject` answers false.
4. **Turn rule**: `isAllowedToLoot` is F's own (Player.cpp:20246-20290) and already contains the round-robin / master-loot / threshold rules; the module's trimmed rule runs on top of it exactly as on T (MayLootCorpse), minus the personal-item clause (F: none).
5. **Approach distance**: T's formula over F's combat reaches and INTERACTION_DISTANCE (both 5.0); the server's own limit is 30 yd plus reaches on F, so F never refuses what the bot calls in range.
6. **Skinning**: a skinnable corpse is never a target on F (T: after the body loot is gone, bots with the skill skin it).
7. **Game objects**: never a loot target on F (T: chests, herbs, ore, goobers).
8. **Quest relation maps**: empty on F (see section 3).
9. **DIALOG_STATUS_* numbers** are defined on F with T's values (QuestDef.h:119-131); F's own status is a bit mask (QuestDef.h:154). The bot's quest value computes the number itself, so nothing converts between them; `AcceptQuestAction.cpp:146` compares a menu icon with `DIALOG_STATUS_AVAILABLE` (outside V2: if that icon comes from the core it carries F's value there).

## 5. Reused, new

Reused (no second definition): `VisitUnitsCompat`, `PetIsMiniPetCompat`, `GuidFromNumbersCompat`, `GuidWriteCompat`, `GuidReadCompat`, `GuidTextCompat`, `GuidKeyCompat`, `CStrCompat`, `tbcompat::Unported`.
New (V2 pair): `GuidQualifierCompat`, `GuidFromQualifierCompat`, `CorpseAllowedToLootCompat`, `CreatureLootForCompat`, `CorpseRoundRobinGuidCompat`, `GroupMasterLooterGuidCompat`,
`CorpseHasVisiblePersonalItemForCompat`, `CorpseRefusalFactsOfCompat` (+ struct), `CorpseInLootRangeCompat`, `CorpseSkinnableByCompat`, `GameObjectLootAllowedCompat`, `LootOfObjectCompat`,
`AbsentLootMeansNothingToTakeCompat`, `LootOfGuidCompat`, `LootContentForCompat`, `LootStatusForCompat`, `LootTemplateOfGuidCompat`, `GameObjectLootTemplateCompat`, `ReferenceLootTemplateCompat`,
`LootStoreItemCompat` / `LootStoreItemListCompat` (F: a row type with T's field names that is never filled), `LootStatusFlags` (F: T's numbers), `QuestRelationsMap` + the four member-name macros, `__QuestGiverStatus`.

The policy calls inside T's moved blocks (`ai::PersonalCopyVisible`, `ai::HasVisiblePersonalItemFor`) are handed to the helper as lambdas by the call site, because the policy headers are included after the compat headers.
The lambdas on F are never called (generic lambdas for the one that names `Loot`).

## 6. NEEDS FROM / OFFERED / left open

**NEEDS FROM I2** (CorpseLootPolicy.h, the 13 places left in my census; and LootAction.cpp):
- keep `ai::HasVisiblePersonalItemFor(Loot const&, Player*)` and `ai::CorpseHasBodyLootFor(Player*, Creature*)` declared with those names: `LootObjectStack.cpp` calls the second directly and names the first inside a lambda (T runs it, F never does). If I2 changes either signature, tell V2 or adapt the two call sites at the merge.
- LootAction.cpp:795 `RESET_AI_VALUE2(bool, "should loot object", std::to_string(guid.GetRawValue()))` and any other producer of that qualifier: use `GuidQualifierCompat(guid)`, or `ShouldLootObject` reads the wrong guid on F.
- `sLootMgr.GetLoot(...)` (LootAction.cpp:595): `LootOfGuidCompat(player, guid)` is offered (F: a creature's loot for the player; empty guid = the open window).
**NEEDS FROM the merge**: UnportedFeatures.h, TbCompat_Unported.* and cmangos-compat-unported.h are shared: V2 added only the three table rows (a block after the last X-row, in UnportedFeatures.h) and kept all helpers in its own pair; the existing `LootOfCreatureCompat`
(TbCompat_Unported.h:89, "loot objects", PlayerbotAIItems.cpp:273) still answers nullptr: `CreatureLootForCompat(creature, player)` is the proper one if that row is retired.
**OFFERED** (names a second package may want): `CorpseAllowedToLootCompat`, `CreatureLootForCompat`, `CorpseInLootRangeCompat`, `LootOfGuidCompat`, `GuidQualifierCompat` / `GuidFromQualifierCompat`.
**Left open**: RandomItemMgr.cpp and GuildOrderSources.cpp (outside the slice) read `LootTemplateAccess` rows typed `LootStoreItem`: when they join, they use `LootStoreItemCompat` and get nullptr templates on F. QuestValues.cpp (outside the slice) reads the four relation maps through the macros (empty on F).

## 7. Unsure

- `LootStatusForCompat` on F reports a corpse with items only as "looted" (no bit): fine for D2 and nobody reads it, but a later slice that reads `IsLootedFor` must port the item side first.
- The F drop-map code (`DropMapValue::Calculate` loops over all item and creature templates) is compiled and, when its values are asked, runs the full scan to find nullptr templates; the values are not registered in the slice. If a later slice registers them it should short-circuit.
- Not live-tested (no boots in this round): that the money of a group kill really arrives in the bot's personal Loot as read here is read from Unit.cpp, not observed (live test k).

## 8. Gates

(filled at the end)
