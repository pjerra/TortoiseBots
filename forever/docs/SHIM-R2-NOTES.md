# ForeverCore port: shim round 2 (item data)

The bot brain reads item data as public fields of cmangos's `ItemPrototype` (`proto->Class`, `proto->Quality`,
`proto->ItemStat[i].ItemStatType`, 1,168 error places in the round-1 census). `ItemTemplate` keeps the same facts behind
accessors, and a macro cannot turn a field into a call, so on ForeverCore `ItemPrototype` is now our own struct with the old
field names, filled once per item from the core's `ItemTemplate` (`forever/src/compat/TbItemPrototype.h` / `.cpp`). The
field reads compile unchanged; only the places that FETCH the pointer changed, to `GetItemProtoCompat(X)`, which exists on
both cores.

## Numbers

| Run | What | Error places |
|---|---|---|
| BEFORE | full census, this branch at 233e35a0c | 13,496 in 538 files, 11 clean files |
| AFTER | full census, this branch at 570d22dd4 | 12,182 in 533 files, 11 clean files |

The item-data families are gone from the name table: `SubClass` 268 -> 0, `Class` 191 -> 0, `ItemId` 170 -> 0,
`Spells` 108 -> 0, `Name1` 97 -> 0, `Quality` 69 -> 0, `InventoryType` 58 -> 0, `ItemLevel` 53 -> 0, `GetProto` 326 -> 2.
Item-heavy files: `ItemUsageValue.cpp` 306 -> 75, `RandomItemMgr.cpp` 556 -> 325, `PlayerbotFactory.cpp` 490 -> 280,
`PlayerbotAIItems.cpp` 86 -> 34, `EquipAction.cpp` 91 -> 20.

What the mirror surfaces in exchange (each is a group of its own, not a wrong value):

| name now in the census | places | why |
|---|---|---|
| `GetItemPrototype` | 180 | `sObjectMgr.GetItemPrototype(id)` - the escalated point below |
| `IsMoving` | 59 | D6 removed the rename; the bot's calls need the core's `isMoving()` spelling |
| `strstr` / `strstri` with a `std::string` Name1 | 17 + 5 | the Turtle shim overloads them for `std::string` (ai/cmangos-compat-shim.h:357-364); the ForeverCore shim does not, and `Name1` now resolves so the call typechecks that far |
| `const std::string` in a printf sink | +12 | the round-1 group 21 ("%s receives std::string"); more sites now get that far |
| `DisplayInfoID` | 6 | left out by D3 (below) |

## The mirror: field -> core source

T = `~/playerbots-references/penqle-tortoise-wow-modsys/src/game/Objects/ItemPrototype.h` (Tortoise core);
F = `~/ForeverCore/src/server/game/Entities/Item/ItemTemplate.h` (+ `Item.cpp`, `DB2Structure.h`) at the pinned core.

| old field (T line) | ForeverCore source | same meaning because |
|---|---|---|
| `ItemId` (:426) | `GetId()` (F:870), `BasicData->ID` | the same item_template entry id / DB2 item id |
| `Class` (:427) | `GetClass()` (F:871), `BasicData->ClassID` | same client ItemClass enum (`ITEM_CLASS_WEAPON` 2, ... in both, ItemTemplate.h:489) |
| `SubClass` (:428) | `GetSubClass()` (F:872) | same client subclass values for the classes the bot tests (weapon/armor/consumable) |
| `Name1` (:429) | `GetName(defaultLocale)` (F:905, ItemTemplate.cpp:58) | the item's name in one locale; a `std::string` in the old struct, and the bot code passes it where a string is wanted (`strstri`/`logEvent`) |
| `Quality` (:432) | `GetQuality()` (F:874) | 0..6 `ITEM_QUALITY_*` in both |
| `Flags` (:433) | only the `ItemFlags` bits the bot tests, by name | the two cores number the middle of the word differently (see below) |
| `BuyCount` (:434) | `GetBuyCount()` (F:878) | items per vendor purchase, `max(VendorStackCount, 1)` |
| `BuyPrice` (:435) | `GetBuyPrice()` (F:879) | copper |
| `SellPrice` (:436) | `GetSellPrice()` (F:880) | copper |
| `InventoryType` (:437) | `GetInventoryType()` (F:881) | same `INVTYPE_*` values 0..28 (ItemTemplate.h:427) |
| `AllowableClass` (:438) | `GetAllowableClass()` (F:882) | class mask, `-1` = all classes = `0xFFFFFFFF` as a `uint32` |
| `AllowableRace` (:439) | `GetAllowableRace().RawValue[0]` (F:883) | race mask word 0; the classic races are bits 0..9, the same bits the old 32-bit mask used |
| `ItemLevel` (:440) | `GetBaseItemLevel()` (F:884) | the template's base item level (no per-instance bonuses here) |
| `RequiredLevel` (:441) | `GetBaseRequiredLevel()` (F:885) | level needed to use it, clamped at 0 |
| `RequiredSkill` (:442) | `GetRequiredSkill()` (F:886) | skill line id |
| `RequiredSkillRank` (:443) | `GetRequiredSkillRank()` (F:887) | rank in that skill |
| `RequiredSpell` (:444) | `GetRequiredSpell()` (F:888), `RequiredAbility` | spell that must be known |
| `RequiredReputationFaction` (:447) | `GetRequiredReputationFaction()` (F:889), `MinFactionID` | faction id |
| `RequiredReputationRank` (:448) | `GetRequiredReputationRank()` (F:890), `MinReputation` | rank in it |
| `MaxCount` (:449) | `GetMaxCount()` (F:891) | unique limit, 0 = none |
| `Stackable` (:450) | the raw `ExtendedData->Stackable` column (F:2620) | the same column as `item_template.stackable`: 1 = not stackable. Not `GetMaxStackSize()`, which turns 0/negative into 0x7FFFFFFE and would be multiplied into a price (`ItemUsageValue.cpp:563`) |
| `ContainerSlots` (:451) | `GetContainerSlots()` (F:892) | bag slots |
| `ItemStat[i].ItemStatType` (:452) | `GetStatModifierBonusStat(i)` (F:893) | `ItemModType`; the values the bot reads (mana 0 .. stamina 7) are identical in both enums (ItemTemplate.h:31-38) |
| `ItemStat[i].ItemStatValue` (:452) | the core's own stat value at base item level: `GetItemStatValue` (Item.cpp:2432) = `StatPercentEditor * GetRandomPropertyPoints(itemLevel, quality, inventoryType, subClass) * 0.0001` | the number the item grants. Per-instance scaling and the socket-cost subtraction need an `Item` and are left out (classic items have no sockets). The bot only asks whether a stat is there and which one |
| `Delay` (:453) | `GetDelay()` (F:902), `ItemDelay` | weapon swing time in milliseconds |
| `RangedModRange` (:454) | `GetRangedModRange()` (F:903), `ExtendedData->ItemRange` | the ranged attack range modifier, a percentage in both (`PlayerbotAISpells.cpp:369` multiplies by 0.01) |
| `Damage[0]` (:456) | `GetDamage(baseItemLevel, min, max)` (F:994) + `GetDamageType()` (F:901) | the same two numbers the client shows for a weapon: the core derives them from item level, quality and slot, which is where the classic client gets them too. Only slot 0 is filled: classic items have one damage line |
| `Block` (:457) | `ITEM_MOD_BLOCK_VALUE` stat value (`Player.cpp:8871` "Classic 1.60: added to the shield block value") | the block value the item grants; an item stat instead of a column |
| `Armor` (:458) | `GetArmor(baseItemLevel)` (F:992, ItemTemplate.cpp:147) | the armor the client shows; the core computes it from item level, quality, subclass and slot. Shields use `ItemArmorShield` there |
| `HolyRes` .. `ArcaneRes` (:459-464) | `ITEM_MOD_HOLY/FIRE/NATURE/FROST/SHADOW/ARCANE_RESISTANCE` stat values, plus `ITEM_MOD_CLASSIC_RESISTANCE_ALL_SCHOOLS` (124) added to each | resistances are stat mods on ForeverCore where cmangos had a column each; "+X to all resistances" is one stat mod and means each school |
| `Spells[0..4]` (:465) | `Effects` (`std::vector<ItemEffectEntry const*>`, F:926; `ItemEffectEntry` DB2Structure.h:2435) placed by `LegacySlotIndex` | the five `item_template.spells[0..4]` slots are the item's effects, and the core sorts them by `LegacySlotIndex` (ObjectMgr.cpp:3741). `SpellId` <- `SpellID`, `SpellTrigger` <- `TriggerType` (0/1/2 = on use / on equip / chance on hit in both), `SpellCharges` <- `Charges`, `SpellCooldown` <- `CoolDownMSec`, `SpellCategory` <- `SpellCategoryID`, `SpellCategoryCooldown` <- `CategoryCoolDownMSec` |
| `Spells[i].SpellPPMRate` (:414) | `ItemTemplate::SpellPPMRate` (F:929) | one rate per item on ForeverCore; filled into every slot |
| `Bonding` (:466) | `GetBonding()` (F:904) | `ItemBondingType` 0..4 mean the same (`BIND_NONE/ON_ACQUIRE/ON_EQUIP/ON_USE/QUEST` at 0/1/2/3/4; ItemTemplate.h:170-176) |
| `StartQuest` (:470) | `GetStartQuest()` (F:907) | quest started by using the item |
| `LockID` (:471) | `GetLockID()` (F:908) | lock id |
| `ItemSet` (:475) | `GetItemSet()` (F:909) | item set id |
| `MaxDurability` (:476) | `ItemTemplate::MaxDurability` (F:925) | durability at full repair |
| `Area` (:477) | `GetArea(0)` (F:910) | the zone bound; the core keeps two (`ZoneBound`), the bot only asks whether there is one |
| `Map` (:478) | `GetMap()` (F:911), `InstanceBound` | instance bound |
| `Duration` (:479) | `GetDuration()` (F:918), `DurationInInventory` | seconds the item lasts in the inventory |
| `BagFamily` (:480) | `GetBagFamily()` (F:912) | `BAG_FAMILY_*` mask |

`Flags` is built by name, one bit per flag the bot code tests, because the two cores put different meanings on the middle
of the word (cmangos 0x400 = ignore bag space, ForeverCore 0x400 = uses resources):

| cmangos bit | ForeverCore flag |
|---|---|
| `ITEM_FLAG_CONJURED` 0x2 | `ITEM_FLAG_CONJURED` |
| `ITEM_FLAG_LOOTABLE` 0x4 | `ITEM_FLAG_HAS_LOOT` |
| `ITEM_FLAG_DEPRECATED` 0x10 | `ITEM_FLAG_DEPRECATED` |
| `ITEM_FLAG_WRAPPER` 0x200 | `ITEM_FLAG_IS_WRAPPER` |
| `ITEM_FLAG_NO_DISENCHANT` 0x8000 | `ITEM_FLAG_NO_DISENCHANT` |
| `ITEM_FLAG_UNIQUE_EQUIPPED`/`_EQUIPPABLE` 0x80000 | `ITEM_FLAG_UNIQUE_EQUIPPABLE` |

The old struct's helpers the bot calls are on the struct, with the old answers: `GetMaxStackSize`, `IsWeapon`,
`IsRangedWeapon`, `IsConjuredConsumable` (from the fields) and `GetProficiencySkill` (forwarded to the core's
`GetSkill()`, ItemTemplate.cpp:97, which uses the same skill table). `_Spell` stays an alias of `_ItemSpell`.

## Fields left out (D3: no true ForeverCore counterpart, so no guess)

| old field | uses | why not filled |
|---|---|---|
| `RandomProperty` (:474) | `PlayerbotFactory.cpp:2091`, `:2211`, `:2272`, `:2418`; `RandomItemMgr.cpp:1383`, `:1940`, `:1942`, `:1967`, `:4022` | no template-level random property id on ForeverCore: random properties/bonuses are item (bonus data) facts, not template ones. `Item::GetItemRandomPropertyId()` is per item instance |
| `DisenchantID` (:481) | `CastCustomSpellAction.cpp:792`, `:1111`; `ItemUsageValue.cpp:430`; `LootValues.cpp:202-203` | the core's disenchant loot is `ItemDisenchantLootEntry` (DB2) via `Item::GetDisenchantLootId()`, and `LootTemplates_Disenchant` is keyed by item entry; the cmangos id belonged to a shared world-DB template row, so putting a core id here would be a different key |
| `RequiredHonorRank` (:445) | `RandomItemMgr.cpp:33`, `:1285-1287`, `:1494`, `:2627`, `:2983`, `:4264`, `:4344`; `ItemUsageValue.cpp:2177` | no `ItemTemplate` accessor. The DB2 column is `ItemSparseEntry::RequiredPVPRank` (DB2Structure.h:2666) and its population in this fork's classic data is unverified |
| `RequiredCityRank` (:446) | `RandomItemMgr.cpp:33`, `:4264`, `:4344` | no counterpart at all |
| `DisplayInfoID` (:431) | `ImbueAction.cpp:55`, `:72`, `:114`, `:131`; `PlayerbotAIItems.cpp:825`, `:844` | the display model lives in the appearance stores (`ItemModifiedAppearance` -> `ItemAppearance` -> `ItemDisplayInfoID`), not on `ItemTemplate`; deriving it would add a second store walk per item for two constant comparisons |
| `DestItemId` (:489) | `RandomItemMgr.cpp:116` | Turtle's transmogrification clone marker; ForeverCore transmog is item modifiers, not a template pair |
| `Description`, `AmmoType`, `PageText`, `LanguageID`, `PageMaterial`, `Material`, `Sheath`, `ExtraFlags`, `OtherTeamEntry`, `ScriptId`, `SourceItemId`, `WrappedGift`, `FoodType`, `MinMoneyLoot`, `MaxMoneyLoot`, `IsQuestItem`, `Discovered` | `ExtraFlags` and `MinMoneyLoot` only in `runtime/AhBotItemPool.cpp:652,660`, which iterates the whole item map by value | not mirrored: no bot code reads them off a single item, and the map-iteration paths cannot use a `const ItemPrototype*` anyway (see open points) |

## Call sites

- Verified by compiling every bot .cpp that touches an item prototype against the core (105 files, ForeverCore
  toolchain, the module's own flags): **no error names the mirror or a helper any more**, only the
  `sObjectMgr.GetItemPrototype` group below.
- `GetItemProtoCompat(X)` (both cores): **364** item `GetProto()` sites in 71 files, rewritten by
  `forever/tools/rewrite_item_proto.py` (kept in the tree, `--check` prints without writing). The diff was reviewed: the
  rewrite is text-preserving (every changed line differs only by the call spelling).
- Deliberately not changed: the **26** `.GetProto()` sites on the bot's own `ItemQualifier`
  (`ItemUsageValue.h:30` defines it, via `sItemStorage` -> the mirror) and every `GetProto()` on a non-item core class
  (none are called in the bot brain: `Creature`/`GameObject`/`Pet` go through other helpers).

A mirror is built on first use, kept in a `std::deque` (nodes never move, so the pointers the bot code holds stay
valid) and looked up under one mutex: the bot brain runs on the map threads. Building it is a map lookup plus the
core's own accessors; it happens once per item per process. Like the store pointers the old code held, a mirror is
only as fresh as the item store it was filled from.

## What the shim does now

- No `#define ItemPrototype ItemTemplate` and no `#define GetItemPrototype GetItemTemplate` (removed).
- `sItemStorage.LookupEntry<T>(id)` returns the mirror (`tbcompat::ItemProto(id)`); `GetMaxEntry()` unchanged.
- `GetItemProtoCompat(Item const*)` and `GetItemTemplateCompat(...)` are in both shims (`ai/cmangos-compat-shim.h` for
  the Turtle core: `item->GetProto()` / identity).
- Core functions that want an `ItemTemplate const*` and are handed a mirror (`Player::CanUseItem` and friends) take
  `GetItemTemplateCompat(proto)`; on the Turtle core that is the same pointer. 15 sites in 9 files, plus the
  `CanUseItem(GetItemProtoCompat(item))` ones turned into `CanUseItem(GetItemTemplateCompat(item))`.
- `#define IsMoving isMoving` removed (round-1 review, D6): the core uses `IsMoving` itself, so the rename is not ours
  to make. The bot brain's 71 `IsMoving()` calls are an ordinary rename left for a later batch (`isMoving()`, the core's
  name); a null check or behaviour change is not involved.

## Open points / escalation

- **`sObjectMgr.GetItemPrototype(id)` cannot return the mirror without changing its 186 call sites.** `sObjectMgr` is a
  macro for `(*ObjectMgr::instance())`, so `sObjectMgr.GetItemPrototype(id)` is member access on the core's `ObjectMgr`;
  a macro cannot rewrite a member access, `ObjectMgr`'s constructor is private (no proxy can derive from it), and a
  `GetItemPrototype` member does not exist any more. Options: (a) rewrite the 186 sites to `ItemProtoCompat(id)` (a
  both-core helper), the same mechanical change as `GetProto`, or (b) leave them as census errors this round. Nothing was
  changed here; the sites now read `no member named 'GetItemPrototype' in 'ObjectMgr'`.
- **Iterating every item by value is not covered.** `sObjectMgr.GetItemPrototypeMap()` (`runtime/AhBotItemPool.cpp:417,638`,
  `commands/OrderSearch.cpp:252`, plus the Turtle shim's `GetMaxEntry`) hands over a `map<uint32, ItemPrototype>`; ForeverCore's
  store holds `ItemTemplate` values, and the mirror is a `const ItemPrototype*` built on demand, so those loops need their own
  shape (iterate ids, call `tbcompat::ItemProto(id)`). Nothing changed there this round; the reads that only exist inside those
  loops (`ExtraFlags`, `MinMoneyLoot`) are the reason those two fields are not in the struct.
- Enum names the bot compares item data against that ForeverCore does not have (`ITEM_SPELLTRIGGER_CHANCE_ON_HIT`,
  `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE`, `ITEM_FLAG_LOOTABLE`, `ITEM_FLAG_UNIQUE_EQUIPPED`, `BIND_WHEN_*`,
  `NO_BIND`) are their own shim group and are not part of this round; the mirror fills the right numbers, so adding
  those constants later is enough.

## Live check

`.tb item <entry>` (console) prints the mirror's filled fields for one item in three lines: name, class/subclass,
quality, inventory type, item level, required level, buy/sell price, armor, damage min-max and delay, the stat mods, the
spell slots, bonding, max stack and durability. Read-only, no config change.

## Gates

| Gate | Command | Result |
|---|---|---|
| G1 module compiles on ForeverCore | `forever-build/build.sh 12 worldserver` | `MAKE_RC=0`: TbItemPrototype.cpp and TbScripts.cpp compiled, `libtortoisebots.a` and `bin/Release/bin/worldserver` linked (24 s, warm ccache; the script then exits 1 because its closing `grep for error:` finds nothing) |
| G2 Turtle build | `vm-build ~/wt-forever-port --wait` | `MAKE_RC=0`, `CU=0` at 570d22dd4 (7 module files rebuilt, 186 s; the 364-site rewrite alone was the 337-file run before it, also `MAKE_RC=0`) |
| G3 Turtle unit tests | `vm-test tb ~/wt-forever-port -- python3 -m unittest discover -s tools -p 'test_*.py'` | `RESULT: PASS` at 7b07b8e52 (dirty=0). The full `verify_all.sh` wrapper was also run: step 4 (these unit tests) passed, step 5 was still compiling after 10 minutes on the shared VM and was stopped - its subject is G4, which passes standalone |
| subset census `ai/playerbot/strategy` | `vm-census.sh ai/playerbot/strategy 14` | 6,778 places in 398 files, 2 clean; item field names (Class/SubClass/ItemId/Spells/Name1) no longer in `names.tsv`; `GetItemPrototype` is now 101 of them |
| G4 core-free tests, ForeverCore toolchain | `policy-tests.sh` | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL - byte-identical to the same run on the BEFORE commit 233e35a0c (checked out to `tb2` and run the same way). `test_decision_capture_codec` fails its `perTick < 2000 us` budget on this VM in both runs (the suite's own wrapper sets `TB_RELAX_TIMING=1` for that reason); `test_log_file_rotation` fails for lack of an argument. Nothing got worse |
