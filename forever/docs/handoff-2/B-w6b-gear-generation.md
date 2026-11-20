# B. W6b: gear generation

**Can start now.** It builds on your W6a in PR #93 (the item value from `RandomItemWeights.cpp` and the weight-scale
SQL, `ClassicItemStatPolicy.h`, `.tb gear <bot|party> upgrade`, the H3 test kit's `.tb equip`/`.tb itemscan` in
`forever/src/host/TbTestKit.cpp`).

## Done looks like

`.tb gear <who> set [spec] [level] [quality]` dresses a bot (or the GM sender's own character) in a full set of
classic items that fit its class, spec and level, scored by your W6a value, through the core's own item creation and
equip calls.

**Live acceptance** (world-life plan, W6b row): `.tb gear` set on the 9 classes at levels 10, 30 and 60: every slot
legal (the core's equip check said OK), item level inside the bracket for that level, no non-classic item. The reply
lists slot, item id, item level and score per slot.
**From a client**: the client test bench skips 29 of its 51 trials for want of items on the tester's own character. A
`set` on the tester's own character (allowed like other "own character" commands) is the item source it lacks; the
bench's inventory snapshot then confirms what is worn.

## Scope and legacy files

Plan scope: "level- and spec-fit gear sets from F item templates (test setup, optional pool seeding)", 5 worker-days,
biggest risk "DB2 item set is the modern Classic one; random suffixes". Legacy: `RandomItemMgr.cpp` (3,320 lines after
your split), `RandomItemMgr.h`, `PlayerbotFactory.cpp` 4,550 / 177 places (`InitEquipment`, `CanEquipItem`,
`InitAmmo`, `InitLevelBags`). The whole gear-and-usage area is 29 files, 16,089 lines, 387 places.

## ForeverCore APIs (25fc7ca)

- Templates: `sObjectMgr->GetItemTemplateStore()` (`ObjectMgr.h:1122`), `GetItemTemplate(id)` (`:1121`).
- `ItemTemplate` (`ItemTemplate.h`): `GetClass`/`GetSubClass` (`:871-872`), `GetQuality` (`:874`),
  `GetInventoryType` (`:881`), `GetAllowableClass`/`GetAllowableRace` (`:882-883`), `GetBaseItemLevel` (`:884`),
  `GetBaseRequiredLevel` (`:885`), `GetRequiredSkill` (`:886`), `GetRequiredExpansion` (`:923`),
  `RandomBonusListTemplateId` (`:935`). Classic "Equip:" stats are `ITEM_MOD_CLASSIC_*` 83-124 (`:109-124`).
- Equip: `Player::CanEquipNewItem(slot, dest, item, swap)` (`Player.h:1603`), `EquipNewItem(pos, item, context,
  update)` (`:1621`), `AutoUnequipOffhandIfNeed` (`:1623`); bags: `CanStoreNewItem` (`:1600`),
  `StoreNewItemInBestSlots` (`:1626`).
- Random suffix: `GenerateItemRandomBonusListId(itemId)` (`ItemEnchantmentMgr.h:26`).
- Cross-check only: the core AH bot's classic filter `AuctionBotVanilla::IsAllowed(proto)`
  (`AuctionHouseBot/AuctionHouseBotVanilla.h:36`) answers "true" while its filter is off, so it cannot be the rule.

## Files to touch (suggested)

- Pure `ai/playerbot/policy/item/GearSetPolicy.h` + `forever/tools/test_gear_set_policy.cpp`: slot list per class,
  level bracket (required level and item level window), quality by level, one-hand/two-hand/shield/ranged rules,
  best item per slot by your W6a score, ties by lowest id.
- Host `forever/src/host/gear/TbGearGen.{h,cpp}`: builds a per-(slot, class) index once from the template store at
  startup (logged count and ms), runs `GearSetPolicy`, equips through the core.
- Command: a `set` verb on your existing `.tb gear` (`forever/src/host/TbPartyState.cpp:699`), same reply grammar;
  target check like `.tb tele`'s: a host bot, or the GM sender's own character.
- Compat pair `TbCompat_WL6b.h` + `ai/cmangos-compat-wl6b.h` only if a legacy file is touched; the census list
  `forever/SLICE-W6B.txt`.

## UNPORTED rows removed (or shrunk)

`random item tables` (the gear part of `RandomItems`), `equip slot of an item prototype` (a template-based slot lookup),
the gear half of `bot factory (ammo, pets)` if you do ammo, and `item random properties` if you take decision 2's
second option. `talent spec tables` stays: the spec comes from your W6a spec detection or the command argument.

## Rules that bite here

- No SQL for items: the template store is loaded. If you read the data folder's lists at runtime, ship them as module
  SQL and read through `WorldQueryCompat`.
- "Fit" is the core's answer: an item is only "set" if `CanEquipNewItem` said OK and the slot holds it after the call.
  A slot with no fitting item stays empty and is listed as `code=no-item` (less, not a substitute from another class).
- Never destroy a worn item silently: old items go to the bags; bags full = that slot refused.
- Test helper: `set` is not called by the autonomous bot (no free items in the real path); pool seeding stays off.

## Tests (failing case first)

- **Failing case:** the pool contains an item with `RequiredExpansion > 0` (or outside the classic id band) whose
  stats score highest for a level-60 warrior; `GearSetPolicy` must not pick it.
- Two-hand weapon plus off-hand never both; a class that cannot wear plate at level 30 gets no plate; a caster's
  weapon carries spell power, a warrior's does not (your W6a scales).
- Level bracket: a level-30 set never has required level above 30 or item level below the bracket floor.
- Wiring: `set` refuses a character that is not a host bot and not the sender's own; `--mutate` drops the expansion
  filter and must FAIL.

## Decisions already made

- Scoring is your W6a value (Turtle weight scales on classic stats); no retail stat weights. The team's
  `BotGearGenerator` level-to-item-level ramp is an idea to read, not code to copy.
- Pre-levelled pool seeding with gear stays a test-only switch, off by default.

## Open for you to settle

1. **What "classic item" means in the DB2 set.** Recommendation: `GetRequiredExpansion() == 0`, quality at most epic,
   not in `forever-item-traps.csv` or `items-missing-classified.csv` as missing, and an id band from `id-ranges.md`
   plus Forever's custom re-IDs. Measure first: a `.tb gear audit` count of candidates per slot and level band, then fix
   the band.
2. **Random suffixes.** Recommendation for round 1: skip items with `RandomBonusListTemplateId != 0` (less, plain
   names). Round 2 may create them with `GenerateItemRandomBonusListId` and score the rolled suffix.
3. **`gear-replacements.csv` (hire tiers 1-7).** Recommendation: use it as the preferred pick per (class, spec, slot)
   at level 60 when its item fits, and the generated pick otherwise; it is checked data.
4. **Quality ramp.** Recommendation: uncommon below 30, rare from 30, epic only when asked (`quality=` argument).
