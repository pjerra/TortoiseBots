# Slice 2, package R3: merge, register, link, boot, spikes

Branch `slice2/integ` (worktree `~/wt-slice2-integ`), build slot 1 (`FOREVER_SLOT=1`). Inputs: `slice2/v` (V2: loot values, loot object stack, corpse money), `slice2/i` (I2: loot and
item-use actions), the first package's notes (`SLICE2-R0-NOTES.md`, `SLICE2-KEYS.md`). Core read only at the pin 6517af05. Not pushed.

## 1. Merges

`slice2/v` merged without a conflict; `slice2/i` conflicted in one file, `ai/playerbot/UnportedFeatures.h` (both packages appended a block after the last X-row). Resolved by
keeping both blocks and then merging rows that name one feature (section 2). `TbCompat_Unported.*` and `cmangos-compat-unported.h` were not touched by either package, so the
blocks the brief expected there did not collide. The re-pinned wiring tests (`test_loot_slot_right_wiring.py`, `test_loot_order_behaviour.py`, and the three others both
packages changed) merged by git with both packages' spellings; no assertion was deleted (the text-only ones were run here: `test_loot_slot_right_wiring`,
`test_corpse_refusal_reason_wiring`, `test_loot_view_rule_wiring`, `test_quest_source_item_wiring`, `test_no_cached_spell_target`: OK; the whole set runs once in `vm-test tb`).

## 2. Dedupe table (helpers and rows both packages defined)

| Name(s) | Packages | Decision |
|---|---|---|
| `Feature::LootItems` | V2 row `loot: items`, I2 row `loot: items, quest items and rolls`: the same enum id, two texts (would not compile) | one row, I2's text (it is the wider one); V2's and I2's call sites both count under it |
| `Feature::GatheringLoot` (V2) / `Feature::LootGather` (I2) | the same feature under two ids | one row `loot: gathering, skinning and game objects`, id `GatheringLoot`; I2's `LootGatherUnportedCompat` counts under it |
| `CorpseAllowedToLootCompat` (V2) / `CorpseHasBodyLootForCompat` (I2) | both call `Player::isAllowedToLoot` on F; on T they differ: V2 `IsAllowedToLoot(creature)`, I2 `lootable flag || IsAllowedToLoot` (each keeps the line it replaced) | F: I2's helper now calls V2's (one definition of the F rule); T: both stay, each matches its original line exactly |
| `GuidQualifierCompat` producers of the `should loot object` qualifier | V2 defines it, I2's `LootAction.cpp:782` wrote `std::to_string(GuidKeyCompat(guid))` (a hash on F: the value would have read a different guid than the stack asked about) | `LootAction.cpp:782` uses `GuidQualifierCompat(guid)`; the stack (`LootObjectStack.cpp:478`) already did; `GuidFromQualifierCompat` is the one reader (`LootValues.cpp:323`). On T the text is the same decimal number as before |
| `LootOfGuidCompat` (V2: creature loot for the player, or the open window's creature) / `LootOfWindowCompat` (I2: `GetLootByWorldObjectGUID(guid)`) | different F answers for different T calls (`GetLoot(player, guid)` of the roll code vs `GetLoot(bot)` of the open window) | both kept; no merge |
| `CorpseInLootRangeCompat` (V2: approach distance) / `LootDistanceCompat` (I2: 30 yd, debug text) | different questions | both kept |
| `PersonalItemVisibleCompat` (I2) / `CorpseHasVisiblePersonalItemForCompat` (V2) | different layers (V2's is the MayLootCorpse answer, false and counted on F; I2's sits inside `ai::HasVisiblePersonalItemFor`) | both kept; on F V2's answer decides and I2's is not reached |
| `GuidReadCompat`, `GuidWriteCompat`, `GuidKeyCompat`, `CStrCompat` | V2's notes say reused | verified: V2.h does not define them (TbCompat_C.h / TbCompat_Unit.h / TbCompat_Names.h do) |

Names checked by a script over the four compat headers (F and T pairs): no other identifier defined twice. Table size: 8 rows.

## 3. NEEDS FROM / OFFERED

| From | Need | Satisfied by |
|---|---|---|
| V2 -> I2 | `ai::HasVisiblePersonalItemFor(Loot const&, Player*)` and `ai::CorpseHasBodyLootFor(Player*, Creature*)` keep their names | kept (CorpseLootPolicy.h:11-23) |
| V2 -> I2 | every producer of the `should loot object` qualifier uses `GuidQualifierCompat` | `LootAction.cpp:782` fixed (section 2); grep of `"should loot object"` over `ai/`: two producers (the stack :478, LootAction :782) |
| V2 -> I2 | `LootOfGuidCompat` offered for `sLootMgr.GetLoot` (LootAction.cpp:595) | I2 used `LootOfWindowCompat` (reads the open window by owner guid, correct for the window); `LootOfGuidCompat` stays for the roll code in `LootValues.cpp:422,454` |
| I2 -> V2 | `LootAccess(loot).lootType()` returns the core's loot type on F | `LootValues.cpp:16-19`: `loot ? loot->loot_type : LOOT_CORPSE`, `Loot::loot_type` is the core's field (Loot.h:301) |
| I2 -> V2 | game objects and skinnable corpses are not queued | `GameObjectLootAllowedCompat` false and `CorpseSkinnableByCompat` false on F: `LootObject::Refresh` never keeps either |
| I2 -> merge | "loot response" leaves the IncomingLoot unported group | removed from `TbCompat_Unported.cpp` group; row renamed `incoming: loot roll` |
| I2 -> A2 | `default` registers trigger `loot response` and action `store loot` | both are in the generated registry (section 5) |
| I2 -> merge | UNPORTED.md rows of the three notes | `forever/docs/UNPORTED.md`: V2 block (names updated), I2 block added, three renames |
| first package | rename `incoming: cast failed` -> `incoming: cast failed trigger`, `incoming: spell failure and delay` -> `incoming: spell delay` | done in `UnportedFeatures.h` and `UNPORTED.md`; group entries unchanged |
| V2 | `LootOfCreatureCompat` (`loot objects` row, PlayerbotAIItems.cpp:273) could be retired with `CreatureLootForCompat` | NOT retired: that code reads the item list of a loot (item taking is out of this slice) and has no player to ask the core with; the row stays, its other call (`ReleaseLootIfFarCompat`, a bot walking away from an open window) is unchanged |
| spec section 4 | rows `incoming: forced speed change` / `incoming: knock back` "not needed on F" | left in the table (their counters are one registration per engine); listed under open items |

## 4. A2: the priest and generic-strategy files

Census after the merge: 8 places in 5 files (`PriestActions.cpp` 3, `LootTriggers.cpp` 2, `GenericPriestStrategy.h` 1, `.cpp` 1, `PriestMultipliers.cpp` 1); after the fixes: 0 in all 160 files.

| File:line | Was | Now | Class of fix |
|---|---|---|---|
| `GenericPriestStrategy.h:12` | bare `#include "RangedCombatStrategy.h"` (resolves on Turtle only; the second place is the base class that include supplies) | `#include "playerbot/strategy/generic/RangedCombatStrategy.h"` (resolves on both: `ai/` is an include root on both builds) | include path |
| `PriestActions.cpp:30` | `(bot->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_SILENCED) != 0` | `UnitSilencedCompat(bot)` (existing pair: T the same expression, F the silence aura type, TbCompat_Unported.h:289) | both-core helper |
| `PriestMultipliers.cpp:33` | `Spell::CalculatePowerCost(spellInfo, bot, nullptr, nullptr)` | `SpellPowerCostCompat(spellInfo, bot)` (existing: T the same call, F `SpellInfo::CalcPowerCost` for the spell's own power type) | both-core helper |
| `LootTriggers.cpp:26` | `unit->HasFlag(UNIT_FIELD_FLAGS, A | B)` | `HasUnitFlagCompat(unit, A | B)` (existing) | both-core helper |

No behaviour change on Turtle (every T side is the original expression).

## 5. Registry (decision D6)

`forever/tools/slice_gen_context.py` gains `SLICE2_KEYS` (the 69 keys of `SLICE2-KEYS.md`: 5 strategies, 12 actions, 26 triggers, 26 values) on top of slice 1's lists; three more full-build context
classes (`ChatActionContext` for `use`, `WorldPacketActionContext` for `store loot`, `WorldPacketTriggerContext` for `loot response`) have a local twin. Result: `slice: registered 8/23/56/166`
(slice 1 was 3/10/27/139: the new keys add 5/12/26/26 and one key had been counted twice nowhere: 3+5, 10+12+1, 27+26+3, 139+26+1 are the generator's own counts; the generator prints them). `--check`: up to date.
`slice_registry_check.py`: (a) = 0; it now reads `SLICE2.txt` and counts the priest context as registered; (b) lists 234 unregistered keys, of which no value is read as a type the stand-in lacks.

`default` (WorldPacketHandlerStrategy) is registered; of its triggers only `often` (slice 1 had it), `timer`... and the two this slice needs, `loot response` -> `store loot`, resolve. Every other trigger/action it
names (`group invite`, `accept invitation`, `trade status`, `taxi`, `levelup`, `xpgain`, `summon request`, ..., 59 names, the first package's list) is left unregistered: the engine skips a missing trigger or action and
the guard logs `slice: missing ...` once per name. Reason for all of them: a packet-driven feature that is not in this slice (party invitations, trade, taxi, ready check, petitions, area triggers, XP and honor, resurrection, mount state, mail, guild).
Not registered either, by reason: loot rolls (`loot roll`, `auto loot roll`, `log roll won`, `loot roll won`: rolls time out on F), gathering and destroying (`add gathering loot`, `reveal gathering item`, `smart destroy item`, `destroy all gray`),
item usage (`query item usage`, `item usage` value: D2), pets (`set pet stance`, `new pet`), the feature rows of NonCombatStrategy (`faction`, `fish`, `world buff`, `need world buff`, `equipment audit`, `collision`, `move out of collision`, `at war`), `party member dead`, `val`, `skip go loot list`.
The full list printed by the checker is in section 9.

The missing-key stand-in (`SliceMissingKeyGuard.h`) gains the value types `ItemUsage` (answers `ITEM_USAGE_NONE` = "not looted": fails toward less, the R2 rule) and `std::set<uint32>&` (`skip go loot list`: an empty list).

Factory (`SliceAiFactory.cpp`), spec section 3: priest -> `GuardedContext<PriestAiObjectContext>`; combat `discipline, offdps, dps assist, ranged, buff, cast time`; non-combat `discipline, dps assist, follow, buff, food, loot, default`;
reaction `discipline`; `GetPlayerRoles`: healer for a priest (the slice reads no talents: tab 0). One addition outside the spec's priest list: the warrior's non-combat set gains `loot` and `default`
(live observation k has both bots open their corpse; its combat set is unchanged).

## 6. CMake

The option keeps its name `TORTOISEBOTS_SLICE1` (also the macro of the host code and the build scripts) and reads `forever/SLICE2.txt` (160 files: `SLICE1.txt` plus the 27 files plus the generated registry's slot).
