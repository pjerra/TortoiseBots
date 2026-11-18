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
| FIX1 | full census, this branch at b7d20355a (code), review fix round 1 | 11,938 in 531 files, 11 clean files |

The item-data families are gone from the name table: `SubClass` 268 -> 0, `Class` 191 -> 0, `ItemId` 170 -> 0,
`Spells` 108 -> 0, `Name1` 97 -> 0, `Quality` 69 -> 0, `InventoryType` 58 -> 0, `ItemLevel` 53 -> 0, `GetProto` 326 -> 2.
The fix round also took `GetItemPrototype` 180 -> 0 and `IsMoving` 59 -> 0 out of the name table; only
`GetItemPrototypeMap` (3 places) remains of that family, in the map-iteration open point below.
Item-heavy files: `ItemUsageValue.cpp` 306 -> 75, `RandomItemMgr.cpp` 556 -> 325, `PlayerbotFactory.cpp` 490 -> 280,
`PlayerbotAIItems.cpp` 86 -> 34, `EquipAction.cpp` 91 -> 20.

What the mirror surfaces in exchange (each is a group of its own, not a wrong value):

| name now in the census | places | why |
|---|---|---|
| `GetItemPrototype` | 180 | `sObjectMgr.GetItemPrototype(id)` - resolved by fix round B1 (`GetItemProtoByIdCompat`) |
| `IsMoving` | 59 | D6 removed the rename; the bot's calls - resolved by fix round B2 (`IsMovingCompat`) |
| `strstr` / `strstri` with a `std::string` Name1 | 17 + 5 | the Turtle shim overloads them for `std::string` (ai/cmangos-compat-shim.h:357-364); the ForeverCore shim does not, and `Name1` now resolves so the call typechecks that far |
| `const std::string` in a printf sink | +12 | the round-1 group 21 ("%s receives std::string"); more sites now get that far |
| `DisplayInfoID` | 6 | left out by D3 (below) |

## Review fix round 1 (FOREVER-SHIM-R2-FIX1)

Two reviews read the round-2 code; these are the corrections, each with the proof on both cores.

### A1 (HIGH) Block was the stat, not the shield's own column

`Block` is now `sObjectMgr->GetItemClassicBlock(entry)` alone (`forever/src/compat/TbItemPrototype.cpp`, `Fill`).

- ForeverCore keeps the classic column in its own store: `ObjectMgr.h:1106` (`GetItemClassicBlock`), loaded from
  `item_classic_block` at `ObjectMgr.cpp:808-833`, counted into the shield's block at `Player.cpp:8975-8984`
  (`Player::GetClassicShieldBlockValue`; the call is `:8980`).
- The `ITEM_MOD_BLOCK_VALUE` stat is added on top by a separate case, `Player.cpp:8871-8873`, into
  `m_classicBlockValueBonus` (`:8872`) - it is an extra bonus, not the base column.
- Turtle: the same value is `item_template.block`, loaded at `ObjectMgr.cpp:2301` and applied as a base mod at
  `Player.cpp:8478-8479`. Turtle's stat space has no block-value mod at all: `ItemPrototype.h:27-36` lists
  mana/health/agility/strength/intellect/spirit/stamina and `MAX_ITEM_MOD` is 8. A block bonus on a Turtle item could
  therefore only be an equip spell, which the old `Block` field never contained.
- Decision: the stat is **not** added. `BlockBase` (the classic column) and `BlockStat` (the stat, for the live check)
  are mirror-only fields; `.tb item` prints both.

On this server's data, 29 loaded items carry `ITEM_MOD_BLOCK_VALUE` in `hotfixes.item_sparse` (any stat slot 1..10), and
`item_classic_block` has 368 rows.

### A2 (MED) all-resistances does not include holy

`HolyRes` is the `ITEM_MOD_HOLY_RESISTANCE` stat alone; `ITEM_MOD_CLASSIC_RESISTANCE_ALL_SCHOOLS` is added only to fire,
nature, frost, shadow and arcane. The core's own application loops over exactly those five unit mods
(`F Player.cpp:8930-8932`); holy is a separate case (`F Player.cpp:8812-8814`). On the data, 3 items carry the
all-schools stat.

### A3 (MED) BagFamily: mask -> the old core's number

Turtle's field is one number per family (`T ItemPrototype.h:97-108`: NONE 0, ARROWS 1, BULLETS 2, SOUL_SHARDS 3,
UNKNOWN1 4, UNKNOWN2 5, HERBS 6, ENCHANTING_SUPP 7, ENGINEERING_SUPP 8, KEYS 9). ForeverCore's is a mask
(`F ItemTemplate.h:356-375`). `BagFamilyNumber` converts a mask that is exactly one old family bit. Three masks the old
core cannot name are the AhBot trade-good groups' material categories, and the only reader of this field
(`runtime/AhBotPricing.cpp:23` -> `runtime/policy/ah/AhBotPricingPolicy.h:97-101`, switch at `:415-421`) uses those
numbers for trade goods only, so they are converted only for `ITEM_CLASS_TRADE_GOODS`: 0x8 (leatherworking) -> 12,
0x10000 (cooking) -> 10, 0x400 (mining) -> 13. A trade-good mask that names exactly one of the five pricing categories
gets that number even if unrelated later-expansion bits are set; a mask with several categories, or a family the old core
does not have at all, is 0 (NONE): no single old family is true for it, and the switch reads NONE as "no group".

Mask distribution on the items the store loads (7,298 rows: every `hotfixes.item` row has an `item_sparse` row, and the
197 sparse rows without an `item` row are not loaded) and what the conversion does:

| mask | items | converted | what the items are |
|---|---|---|---|
| 0x1 arrows | 7 | 1 | ammo (Precision Arrow 9399, Swiftfeather Arrow 274387, ...) |
| 0x2 bullets | 4 | 2 | ammo (Swiftstrike Shot 274388, ...) |
| 0x40 enchanting | 12 | 7 | enchanting materials/supplies (6217, 287979, ...) |
| 0x100 keys | 6 | 9 | key items (221324, 281350, ...) |
| 0x400 mining | 2 | 13 | both are trade goods (1529 class-7 "other", 3859 metal/stone) |
| 0x10000 cooking | 6 | 10 for 242639 (trade good), 0 for the other five | 242639 is a cooking trade good; the rest are consumables |
| 0x800 soulbound equipment | 1 | 0 | 280363 Nightclaw Mantle |
| 0x1000 vanity pets | 15 | 0 | pets (11474, 19054, ...) |
| 0x8000 fishing | 2 | 0 | 275012, 277820 |
| 0x18000 fishing+cooking | 2 | 10 | 279094, 279106: both cooking trade goods, one pricing category in the mask |
| 0x80000 | 1 | 0 | 284164 |
| 0x100000 | 57 | 0 | trinkets and quest items (1490 Guardian Talisman, 13965 Blackhand's Breadth, ...) |
| 0x2000000 | 2 | 0 | 12709, 269716 |
| 0x56004C8 | 1 | 0 | 232875: leatherworking, enchanting, engineering and mining bits together - three categories, no single pick |
| 0 (other) | 7,180 | 0 | ordinary items |

So of 118 loaded items with a non-zero mask, 34 convert to an old number (7+4+12+6 single old families, plus 2 mining,
1+2 cooking trade goods) and 84 become 0; only 232875 has several pricing categories and therefore no single answer.
`.tb item <entry>` prints both the mask and the converted number.

### A4 (MED) no mutex on the steady-state lookup

`tbcompat::FillAll()` runs from the module's `WorldScript::OnWorldInitialized` (after `Main.cpp:372`, i.e. after
`World::SetInitialWorldSettings` loaded the item store at `World.cpp:1608` and `item_classic_block` at `:1631`) and fills
every mirror from `sObjectMgr->GetItemTemplateStore()`. The deque and the entry map are never written again, so
`ItemProto` reads them without the lock; a `std::atomic<bool>` with release/acquire publishes the fill. A lookup that
arrives before the fill (not expected: the fill runs before the world is online) takes the old locked lazy path. The
fill logs one line:

`TortoiseBots: item mirrors filled: N items in M ms, K with a Stackable column <= 0 or INT_MAX.`

`forever/docs/PORT-PLAN.md` (pin rule) now records: the core has no item template reload at the pin (`LoadItemTemplates`
is called once, `World.cpp:1608`); re-check when the pin moves, because the mirror is filled once.

### A5 (MED) Stackable

The raw `ExtendedData->Stackable` column stays, and the fill counts rows whose raw column is `<= 0` or `INT_MAX` and logs
that count. On this server's data the count is **0** (7,298 loaded item rows), so the raw column is the column
the old core had, and no special value is invented. The bot reads `Stackable` through `GetMaxStackSize()` (the mirror's
inline) for stack limits and directly as a price factor in `ItemUsageValue.cpp:563`; a raw 0 or INT_MAX would have made
the second meaningless, but neither occurs.

### A6 (LOW) stat multiplier and known gaps

- The stamina multiplier is **1.0** for every item level this server has, so the mirror's stat values need no
  correction. Proof: `F Player.cpp:8619-8624` multiplies only `ITEM_MOD_STAMINA` by `GtStaminaMultByILvl`;
  `Player.cpp:8606-8607` does the combat-rating multiplier, which none of the stats the bot reads (mana/health/agility/
  strength/intellect/spirit/stamina) use. The data file on the server, `data/gt/StaminaMultByILvl.txt`, has 1.0 in every
  one of its four columns for item levels 1..340 (the first row above 1 is item level 341, file line 342), and the item
  store's largest item level is 298. So base item level is also the item level the core would use for these rows.
- KNOWN GAP (no code change): classic "+spell damage/healing" are item stats on ForeverCore
  (`ITEM_MOD_SPELL_HEALING_DONE`/`_DAMAGE_DONE`, `F Player.cpp:8874-8876`) and equip spells on Turtle; the mirror does not
  translate one into the other. The bot reads the spell list, so an item whose bonus is a stat here shows no spell.
- KNOWN GAP (no code change): thrown weapons (subclass 16) get no proficiency skill on ForeverCore
  (`F ItemTemplate.cpp:97-106`, index 16 is `0`), where Turtle returns `SKILL_THROWN` (`T Item.cpp:654-666`, index 16).
  The bot's check at `ai/playerbot/strategy/values/ItemUsageValue.cpp:2380` skips the skill test when the value is 0, so a
  thrown weapon is only less restricted, never wrongly rejected.
- KNOWN GAP (no code change): `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE` must never be given the value 5 on ForeverCore - 5 is
  `ITEM_SPELLTRIGGER_ON_PICKUP` there (`F ItemTemplate.h:124-137`). The mirror copies the core's `TriggerType`, which is
  right for the values the bot tests (0 on use, 1 on equip, 2 proc); a shim constant for the old no-delay-use name must
  not be added as 5.

### B1, B2 (mechanical)

- B1: `GetItemProtoByIdCompat(uint32)` on both cores (Turtle: `sObjectMgr.GetItemPrototype(id)`; ForeverCore:
  `tbcompat::ItemProto(id)`). `forever/tools/rewrite_compat_calls.py` rewrote **180** call sites in 61 files; the two
  spellings it looks for are `sObjectMgr.GetItemPrototype(` and `ObjectMgr::GetItemPrototype(` (no site used the second).
  `sItemStorage.LookupEntry<ItemPrototype>(id)` still works through the existing proxies on both cores and was not
  touched.
- B2: `IsMovingCompat(Unit const*)` on both cores (Turtle: `IsMoving()` on `WorldObject`, `T Object.h:976`; ForeverCore:
  `Unit::isMoving()`, `F Unit.h:1829`). The same script rewrote **59** `->IsMoving()` sites in 30 files; every receiver is
  a core `Unit*`/`Player*`/`Creature*` (checked site by site: `bot`, `master`, `unit`, `member`, `target`, `attacker`,
  `lead`, `commander`, `caller`, and `sServerFacade.GetChaseTarget(bot)`), and no bot class has an `IsMoving` member.
- Both helpers add a null check where the old call would have crashed.
- The mechanical rename broke four tests that pinned the old spelling: `tools/test_loot_order_behaviour.py` (its standalone
  C++ harness now defines `GetItemProtoByIdCompat` exactly as the Turtle shim does) and the exact-source assertions in
  `tools/test_player_led_wiring.py`, `tools/test_run_leader_wiring.py`, `tools/test_travel_leg_chain_wiring.py` (same
  wiring, new call spelling). No test was removed.


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
| `Stackable` (:450) | the raw `ExtendedData->Stackable` column (F:2620) | the same column as `item_template.stackable`: 1 = not stackable. Not `GetMaxStackSize()`, which turns 0/negative into 0x7FFFFFFE and would be multiplied into a price (`ItemUsageValue.cpp:563`). The fill counts rows whose raw column is `<= 0` or `INT_MAX` and logs the number; it is **0** on this server's item data (fix round A5), so the raw column stays |
| `ContainerSlots` (:451) | `GetContainerSlots()` (F:892) | bag slots |
| `ItemStat[i].ItemStatType` (:452) | `GetStatModifierBonusStat(i)` (F:893) | `ItemModType`; the values the bot reads (mana 0 .. stamina 7) are identical in both enums (ItemTemplate.h:31-38) |
| `ItemStat[i].ItemStatValue` (:452) | the core's own stat value at base item level: `GetItemStatValue` (Item.cpp:2432) = `StatPercentEditor * GetRandomPropertyPoints(itemLevel, quality, inventoryType, subClass) * 0.0001` | the number the item grants. Per-instance scaling and the socket-cost subtraction need an `Item` and are left out (classic items have no sockets). The bot only asks whether a stat is there and which one. No stamina multiplier is applied because it is 1.0 at every item level this server has: `Player.cpp:8619-8624` applies `GtStaminaMultByILvl` only to stamina, and `data/gt/StaminaMultByILvl.txt` rows 1..340 are all 1.0 (first row above 1 at item level 341), while the item store's largest item level is 298 (fix round A6) |
| `Delay` (:453) | `GetDelay()` (F:902), `ItemDelay` | weapon swing time in milliseconds |
| `RangedModRange` (:454) | `GetRangedModRange()` (F:903), `ExtendedData->ItemRange` | the ranged attack range modifier, a percentage in both (`PlayerbotAISpells.cpp:369` multiplies by 0.01) |
| `Damage[0]` (:456) | `GetDamage(baseItemLevel, min, max)` (F:994) + `GetDamageType()` (F:901) | the same two numbers the client shows for a weapon: the core derives them from item level, quality and slot, which is where the classic client gets them too. Only slot 0 is filled: classic items have one damage line |
| `Block` (:457) | `sObjectMgr->GetItemClassicBlock(entry)` (ObjectMgr.h:1106, ObjectMgr.cpp:831; the classic column is loaded at ObjectMgr.cpp:808-833) | the shield's own block value, which is what the old core's `item_template.block` was (`T ObjectMgr.cpp:2301`, applied as a base mod at `T Player.cpp:8478-8479`). ForeverCore adds that column to the shield's block in `Player::GetClassicShieldBlockValue` (`F Player.cpp:8975-8984`). The item's `ITEM_MOD_BLOCK_VALUE` stat is a different bonus on this core (`F Player.cpp:8871-8873`): on Turtle the stat space has no block value at all (`T ItemPrototype.h:27-36`), so such a bonus could only have been an equip spell, never part of the Block column; it is **not** added (fix round A1) |
| `Armor` (:458) | `GetArmor(baseItemLevel)` (F:992, ItemTemplate.cpp:147) | the armor the client shows; the core computes it from item level, quality, subclass and slot. Shields use `ItemArmorShield` there |
| `HolyRes` .. `ArcaneRes` (:459-464) | `ITEM_MOD_HOLY/FIRE/NATURE/FROST/SHADOW/ARCANE_RESISTANCE` stat values, plus `ITEM_MOD_CLASSIC_RESISTANCE_ALL_SCHOOLS` (124) added to fire/nature/frost/shadow/arcane | resistances are stat mods on ForeverCore where cmangos had a column each; "+X to all resistances" is one stat mod and the core applies it to those five schools only, not holy (`F Player.cpp:8930-8932`), so holy gets its own stat alone (fix round A2) |
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
| `BagFamily` (:480) | `GetBagFamily()` (F:912), converted from the core's mask to the old core's number by `BagFamilyNumber` (TbItemPrototype.cpp) | Turtle's `BagFamily` is a number (`T ItemPrototype.h:97-108`), ForeverCore's is a bit mask (`F ItemTemplate.h:356-375`). Single bits become the old number (0x1->1, 0x2->2, 0x4->3, 0x20->6, 0x40->7, 0x80->8, 0x100->9); the three masks with no 1.12 family that the AhBot trade-good groups read become those groups' numbers on trade goods (0x8->12, 0x10000->10, 0x400->13); every other mask is 0 (fix round A3, counts and items in that section) |

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
`GetSkill()`, ItemTemplate.cpp:97, which uses the same skill table). `_Spell` stays an alias of `_ItemSpell`, and the
old header's free `ItemSubClassToDurabilityMultiplierId` (a row index into the client's durability cost table) comes
along: weapons are rows 0..20, armor 21..30, in both cores.

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
  toolchain, the module's own flags): **no error names the mirror or a helper any more**.
- `GetItemProtoCompat(X)` (both cores): **364** item `GetProto()` sites in 71 files, rewritten by
  `forever/tools/rewrite_item_proto.py` (kept in the tree, `--check` prints without writing). The diff was reviewed: the
  rewrite is text-preserving (every changed line differs only by the call spelling).
- `GetItemProtoByIdCompat(id)` (both cores): **180** `sObjectMgr.GetItemPrototype(id)` sites in 61 files (no site used
  the `ObjectMgr::GetItemPrototype` spelling), rewritten by `forever/tools/rewrite_compat_calls.py` (fix round B1).
- `IsMovingCompat(unit)` (both cores): **59** `->IsMoving()` sites in 30 files, same script (fix round B2).
- Deliberately not changed: the **26** `.GetProto()` sites on the bot's own `ItemQualifier`
  (`ItemUsageValue.h:30` defines it, via `sItemStorage` -> the mirror) and every `GetProto()` on a non-item core class
  (none are called in the bot brain: `Creature`/`GameObject`/`Pet` go through other helpers).

The mirror table is filled once, from the module's `WorldScript::OnWorldInitialized` (`tbcompat::FillAll`), into a
`std::deque` (nodes never move, so the pointers the bot code holds stay valid). After the fill it is only read, so the
lookup takes no lock; a lookup before the fill takes the old locked lazy path. Like the store pointers the old code
held, a mirror is only as fresh as the item store it was filled from.

## What the shim does now

- No `#define ItemPrototype ItemTemplate` and no `#define GetItemPrototype GetItemTemplate` (removed).
- `sItemStorage.LookupEntry<T>(id)` returns the mirror (`tbcompat::ItemProto(id)`); `GetMaxEntry()` unchanged.
- `GetItemProtoCompat(Item const*)` and `GetItemTemplateCompat(...)` are in both shims (`ai/cmangos-compat-shim.h` for
  the Turtle core: `item->GetProto()` / identity).
- Core functions that want an `ItemTemplate const*` and are handed a mirror (`Player::CanUseItem` and friends) take
  `GetItemTemplateCompat(proto)`; on the Turtle core that is the same pointer. 15 sites in 9 files, plus the
  `CanUseItem(GetItemProtoCompat(item))` ones turned into `CanUseItem(GetItemTemplateCompat(item))`.
- `#define IsMoving isMoving` removed (round-1 review, D6): the core uses `IsMoving` itself, so the rename is not ours
  to make. The bot brain's 59 `IsMoving()` calls now go through `IsMovingCompat(unit)` (fix round B2).
- `GetItemProtoByIdCompat(id)` is the entry lookup of both cores: the core's own `sObjectMgr.GetItemPrototype(id)` on
  Turtle, `tbcompat::ItemProto(id)` on ForeverCore (`GetItemProtoCompat(item)` is the item-instance lookup).
- `sItemStorage.LookupEntry<ItemPrototype>(id)` keeps working through the existing proxies on both cores; no call site
  changed.

## Open points / escalation

- **`sObjectMgr.GetItemPrototype(id)` is resolved mechanically** (fix round B1): the 180 call sites now read
  `GetItemProtoByIdCompat(id)`. A macro could not have done it - a macro cannot rewrite a member access - so this is the
  same kind of call-site change as `GetProto`.
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
quality, inventory type, item level, required level, buy/sell price, bag family (converted number and the core mask),
armor, block as `Block (base + stat, not added)`, damage min-max and delay, the stat mods, the spell slots, bonding, max
stack and durability; an item whose damage or armor row this client's data does not have gets a fourth line,
`INCOMPLETE: <table and key>`. Read-only, no config change.

## Live boot: a damage table row the fill must not ask for (FOREVER-SHIM-R2-FIX2)

The round-2 eager fill aborted the first live boot: `DB2Store.h:85 AssertEntry ... LookupEntry(298)` after "World
initialized in 0 minutes 41 seconds". `ItemTemplate::GetDamage` (ItemTemplate.cpp:260) goes through `GetDPS`
(:207-257), which indexes the client's damage table for the item level with `AssertEntry`; this client's
`ItemDamageOneHand`/`OneHandCaster`/`TwoHand`/`TwoHandCaster` rows stop at ID 100 (`ItemDamageAmmo` at 1000) while the
item store goes to item level 298. The lazy path only ever met levels that had rows; the one-time fill meets every
item. An assertion cannot be caught, so every accessor the fill calls was read for asserts, aborts, unchecked pointers
and unbounded indexes, and the ones that depend on item data are pre-checked with the same lookup the core does:

| core call the fill makes | what it can do | pre-check in `Fill` |
|---|---|---|
| `sItemDamageAmmoStore.AssertEntry(itemLevel)` (ItemTemplate.cpp:217) | abort when the row is missing | all five stores (`Ammo` :217, `TwoHandCaster` :221/:237, `TwoHand` :223/:239, `OneHandCaster` :231/:249, `OneHand` :251) are checked with `LookupEntry(itemLevel)` before `GetDamage`, and only for an item `GetDPS` would index (weapon, quality not past artifact with heirloom read as rare, handled inventory type). Checking all five instead of copying `GetDPS`'s choice of store is stricter, never looser |
| `GetArmor` (`ItemArmorQuality`/`ItemArmorTotal` :156-157, `ArmorLocation` :165, `ItemArmorShield` :200) | no abort (`LookupEntry`), but a missing row is a silent 0 that reads as the item's armor | the same rows are looked up first: shield subclass -> `ItemArmorShield`; cloth..plate -> `ItemArmorQuality`, `ItemArmorTotal`, `ArmorLocation` (robe maps to chest as the core does). A missing row leaves `Armor` 0 and marks the mirror incomplete |
| `GetStatModifierBonusStat` / `GetStatPercentEditor`, `ASSERT(index < MAX_ITEM_PROTO_STATS)` (ItemTemplate.h:893-894) | abort on an index >= 10 | the loop bound is `MAX_ITEM_PROTO_STATS` (10, DBCEnums.h:1215) |
| `GetArea(0)`, `ASSERT(index < MAX_ITEM_PROTO_ZONES)` (ItemTemplate.h:910) | abort on an index >= 2 | the literal 0 |
| `GetRandomPropertyPoints` (ItemEnchantmentMgr.cpp:107-180) | none: `sRandPropPointsStore.LookupEntry` is null-checked, the quality and slot switches are enumerated, the `[5]` arrays are indexed by an enumerated 0..4 | none needed; a missing row returns 0, exactly what `Item::GetItemStatValue` returns |
| `BasicData` / `ExtendedData` (ItemTemplate.h:870-913) | null pointer if an item row had no sparse row | the store is built only from `ItemSparse` rows whose `Item` row exists (ObjectMgr.cpp:3650-3663) |
| `Effects` + `LegacySlotIndex` | null pointer, slot out of range | the existing `!effect` and `< MAX_ITEM_PROTO_SPELLS` checks |

An item that fails a pre-check is not skipped and no value is invented: every field that can be read is filled (item
284846 keeps its item level, delay, durability and stat mods), `Damage`/`Armor` stay 0, and `Incomplete` +
`IncompleteReason` on the mirror say which table and key are missing. `FillAll` counts them and logs one line with the
first ten; `.tb item <entry>` prints `INCOMPLETE: <reason>`. The lazy pre-fill path calls the same `Fill`, so the checks
cover both.

Live evidence (gate G5 below): `26054 items in 35 ms, 0 with a Stackable column <= 0 or INT_MAX, 379 incomplete`, the
first ten being weapons at item level 298 (and 277951 at 259) with no `ItemDamage*` row for the level. The armor side
fires too: a level-298 plate pauldron (270816) is incomplete with no `ItemArmorQuality`/`ItemArmorTotal` row and keeps
its stats, while a level-90 shield (23043) is complete and prints `block 66 (base 66 + stat 21, not added)`. The five
command outputs and the fill line are quoted below.

### Live output (G5)

```
TortoiseBots: item mirrors filled: 26054 items in 35 ms, 0 with a Stackable column <= 0 or INT_MAX, 379 incomplete: 284846 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 277951 (no row for item level 259 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270839 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270838 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270837 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270836 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270835 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270834 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270833 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); 270832 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster).
TortoiseBots (ForeverCore port, spike): enabled
TrinityCore rev. unknown 1970-01-01 00:00:00 +0000 (Archived branch) (Linux, x86_64, Release, Static) (worldserver-daemon) ready...
```

```
tc> tb item 19019
TortoiseBots: item 19019 "Thunderfury, Blessed Blade of the Windseeker": class 2/7, quality 5, inventory 13, item level 80, required level 60, buy 1276777 sell 255355, bag family 0 (mask 0x0).
TortoiseBots: armor 0, block 0 (base 0 + stat 0, not added), damage 82.3-153.0 type 0, delay 1900, bonding 1, max stack 1, durability 125.
TortoiseBots: stats 3:5, 7:8, 51:8, 55:9; spells 21992 (trigger 2, charges 0); resistances 0/8/9/0/0/0.

tc> tb item 25
TortoiseBots: item 25 "Worn Shortsword": class 2/7, quality 1, inventory 21, item level 2, required level 1, buy 35 sell 7, bag family 0 (mask 0x0).
TortoiseBots: armor 0, block 0 (base 0 + stat 0, not added), damage 1.4-3.0 type 0, delay 1900, bonding 0, max stack 1, durability 20.
TortoiseBots: stats none; spells none; resistances 0/0/0/0/0/0.

tc> tb item 12940
TortoiseBots: item 12940 "Dal'Rend's Sacred Charge": class 2/7, quality 3, inventory 21, item level 63, required level 58, buy 274063 sell 54812, bag family 0 (mask 0x0).
TortoiseBots: armor 0, block 0 (base 0 + stat 0, not added), damage 81.3-151.0 type 0, delay 2800, bonding 1, max stack 1, durability 90.
TortoiseBots: stats 4:4, 32:14; spells none; resistances 0/0/0/0/0/0.

tc> tb item 284846
TortoiseBots: INCOMPLETE: no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster
TortoiseBots: item 284846 "Venomous Gladiator's Smasher": class 2/5, quality 4, inventory 17, item level 298, required level 90, buy 5466014 sell 1093202, bag family 0 (mask 0x0).
TortoiseBots: armor 0, block 0 (base 0 + stat 0, not added), damage 0.0-0.0 type 0, delay 3600, bonding 1, max stack 1, durability 120.
TortoiseBots: stats 4:217, 7:325, 40:187, 32:101; spells none; resistances 0/0/0/0/0/0.

tc> tb item 23043
TortoiseBots: item 23043 "The Face of Death": class 4/6, quality 4, inventory 14, item level 90, required level 60, buy 874054 sell 174810, bag family 0 (mask 0x0).
TortoiseBots: armor 3354, block 66 (base 66 + stat 21, not added), damage 0.0-0.0 type 0, delay 0, bonding 1, max stack 1, durability 120.
TortoiseBots: stats 7:21, 50:140, 15:15, 48:21; spells none; resistances 0/0/0/0/0/0.

tc> tb item 270816
TortoiseBots: INCOMPLETE: no ItemArmorQuality row for item level 298; no ItemArmorTotal row for item level 298
TortoiseBots: item 270816 "Venomous Gladiator's Plate Pauldrons": class 4/4, quality 4, inventory 3, item level 298, required level 90, buy 2295106 sell 459021, bag family 0 (mask 0x0).
TortoiseBots: armor 0, block 0 (base 0 + stat 0, not added), damage 0.0-0.0 type 0, delay 0, bonding 1, max stack 1, durability 100.
TortoiseBots: stats 74:166, 7:249, 40:147, 36:74; spells none; resistances 0/0/0/0/0/0.
```


## Gates

| Gate | Command | Result |
|---|---|---|
| G1 module compiles on ForeverCore | `forever-build/build.sh 12 worldserver` | `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=9` on the final tree: the module object for `TbItemPrototype.cpp` and the relinked `worldserver` were written (01:16 run: `worldserver` 55,377,688 bytes at 01:16:31); the `TbItemPrototype.cpp`/`TbScripts.cpp` changes are in it |
| G5 live boot | rsync core + module to `forever-build/src`, `build.sh 12 worldserver`, `forever-run/setup.sh && world.sh start`, poll `world.out` 15 s up to 3 min for `ready...`, `ASSERTION FAILED` = 0 and the mirrors line, then `tb item` on the console; `world.sh stop` | PASS: `ready...` at poll 5 (76 s), `grep -c "ASSERTION FAILED"` = 0, `grep -c "ready\.\.\."` = 1, `grep -c "mirrors filled"` = 1; the 379-incomplete line and the `tb item` outputs above; server stopped and `forever-world` removed |
| G2 Turtle build | `vm-build ~/wt-forever-port --wait` | `MAKE_RC=0`, `QUEUE_RC=0`, `COMPILE_SECONDS=11`, `TOTAL_SECONDS=13`, module files changed/added/removed: 5, job `1-1794961287780864682-1365186` rc=0 slot 0 |
| G3 Turtle unit tests | `vm-test tb ~/wt-forever-port` (the whole `tools/verify_all.sh`: OKF, surface, wiring, unit tests, standalone policy tests, decision trail) | `RESULT: PASS`, rc=0, `vm_duration=875s`, `sha=553e3be72 dirty=5 files=3529`; `Ran 2656 tests in 309.342s`, `OK (skipped=61)`, engine unit tests passed |
| G4 core-free tests, ForeverCore toolchain | `policy-tests.sh` on the tree under test (worktree synced to `~/forever-build/tb`) | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL in 113 s - the same set as the BEFORE commit and fix round 1: `test_combat_monitor_service`, `test_combat_monitor_service_race`, `test_decision_capture_files`, `test_run_trace_writer`; `test_decision_capture_codec` and `test_log_file_rotation`. Nothing got worse |
