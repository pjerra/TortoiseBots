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
(slice 1 was 3/10/27/139; the generator's own count: 8 strategies, 23 actions, 56 triggers, 166 values = the 69 new keys plus the slice-1 lists, slice 1's lists already holding `formation station`, `master target active`, `out of free move range` and `move style` from R8). `--check`: up to date.
`slice_registry_check.py`: (a) = 0; it now reads `SLICE2.txt` and counts the priest context as registered; (b) lists 234 unregistered keys, of which no value is read as a type the stand-in lacks.

`default` (WorldPacketHandlerStrategy) is registered; of what it names only `often` (slice 1 had it) and the pair this slice needs, trigger `loot response` -> action `store loot`, resolve. Every other trigger/action it
names (`group invite`, `accept invitation`, `trade status`, `taxi`, `levelup`, `xpgain`, `summon request`, ..., 59 names, the first package's list) is left unregistered: the engine skips a missing trigger or action and
the guard logs `slice: missing ...` once per name. Reason for all of them: a packet-driven feature that is not in this slice (party invitations, trade, taxi, ready check, petitions, area triggers, XP and honor, resurrection, mount state, mail, guild).
Not registered either, by reason: loot rolls (`loot roll`, `auto loot roll`, `log roll won`, `loot roll won`: rolls time out on F), gathering and destroying (`add gathering loot`, `reveal gathering item`, `smart destroy item`, `destroy all gray`),
item usage (`query item usage`, `item usage` value: D2), pets (`set pet stance`, `new pet`), the feature rows of NonCombatStrategy (`faction`, `fish`, `world buff`, `need world buff`, `equipment audit`, `collision`, `move out of collision`, `at war`), `party member dead`, `val`, `skip go loot list`.
The 234 unregistered keys with a reason each are in `forever/docs/SLICE2-R3-UNREGISTERED.md` (generated from the checker; reasons by name and by the naming file); the ones the engine actually asked for in the boots are explained in section 9.5.

The missing-key stand-in (`SliceMissingKeyGuard.h`) gains the value types `ItemUsage` (answers `ITEM_USAGE_NONE` = "not looted": fails toward less, the R2 rule) and `std::set<uint32>&` (`skip go loot list`: an empty list).

Factory (`SliceAiFactory.cpp`), spec section 3: priest -> `GuardedContext<PriestAiObjectContext>`; combat `discipline, offdps, dps assist, ranged, buff, cast time`; non-combat `discipline, dps assist, follow, buff, food, loot, default`;
reaction `discipline`; `GetPlayerRoles`: healer for a priest (the slice reads no talents: tab 0). One addition outside the spec's priest list: the warrior's non-combat set gains `loot` and `default`
(live observation k has both bots open their corpse; its combat set is unchanged).

## 6. CMake

The option keeps its name `TORTOISEBOTS_SLICE1` (also the macro of the host code and the build scripts) and reads `forever/SLICE2.txt` (160 files: `SLICE1.txt` plus the 27 files plus the generated registry's slot).

## 7. Link (option ON, slot 1)

Build: `FOREVER_SLOT=1 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 ~/forever-build/build.sh 12 worldserver`. One optional variable was added to the VM's `build.sh` (backup `build.sh.bak-r3-20261118`): `FOREVER_NINJA_EXTRA` (extra ninja arguments; `-k 0` keeps compiling after the first error, so one pass lists every failure).

| Pass | Result |
|---|---|
| 1 (after the merge, A2, registry, host commands) | compile: 1 file failed, the generated registry: `MaintenanceValues.h` (header-only value classes: nothing in the census includes a header that has no `.cpp`, so these four places were invisible to it): `SPELL_ID_PASSIVE_RESURRECTION_SICKNESS`, `Player::GetPowerPercent`, `Player::GetAngle` x2. Fixed: `GetPowerPercent` joins the name macros of the shim (`GetPowerPct`), `GetAngle` -> `GetAngleCompat(master, bot)` (existing pair), the spell id constant 15007 in `TbCompat_Names.h` (T: SharedDefines.h:1342; the number is read back by `.tb spell 15007` in the boot, section 10) |
| 2 | every object compiled; **link: 25 undefined functions and 10 vtables** |
| 3 | `worldserver` linked, `MAKE_RC=0` |

### 7.1 The 10 vtables: routed (rule a), 8 call sites in 3 files

`LootObjectStack.cpp:276,519`, `LootAction.cpp:312`, `LootValues.cpp:157,184,205,259,269` read the global value context (`GAI_VALUE`, `GAI_VALUE2`), whose singleton builds `SharedValueContext`, whose creators name the vendor, travel, trainer, quest,
mount and PvP value classes (not in the slice): ten vtables. The sites now use `GAI_VALUE_COMPAT` / `GAI_VALUE2_COMPAT` (`ai/cmangos-compat-s2v.h`: the same macros, expanded at the site; `TbCompat_V2.h`: `SharedValueUnportedCompat<T>` counted under `loot tables (drop maps)`).
Absent answer: the shared value does not exist. Value: `T{}` (nullptr for a `DropMap*`, an empty list). Effect: every caller already tests for null or iterates a list (V2's notes section 3): no drop is known, no upgrade chased, the game object paths are not reached (LESS).

### 7.2 The 15 stand-ins added (rule c, "answers the value the caller already treats as not there"; each is counted; polarity per row), and the 7 removed

Added in `forever/src/slice/SliceLinkStubs.cpp`, each with the three statements the review rule asks for in its comment:

| Symbol | Absent answer | Value | Effect at the callers | Polarity |
|---|---|---|---|---|
| `ScanBossTactics` | a fight with no boss row | empty fight | `PartyMemberToDispel`: `rows` empty, plain tier search | neutral (no boss rule) |
| `CollectDispelAuras` | no dispellable aura | `out` left empty | only reached with rows: never | LESS |
| `NearestGroupMemberYd` | "a large value when the unit has none" (header) | `float max` | only reached with rows: never; if reached a `clear` row would release the dispel | unreachable (the one value that would be MORE) |
| `LogDispelPick`, `LogDispelExcluded` | nothing to log | no-op | one events row not written | neutral |
| `BossRowProtectionPotion` | no spread row names a potion | `kNoProtectionPotion` (item 0) | UseItemAction.cpp:1171: no potion chosen | LESS |
| `WorldBuffAction::NeedWorldBuffs` | the unit needs no world buff | empty list | `need world buff` trigger never active (key not registered either) | LESS |
| `TellLosAction::ParseLosModifiers`, `GoGuidListToObjList`, `FilterGameObjects` | no game object matches | empty vector / lists | `use gos ...` finds no target; game object use is refused on F anyway | LESS |
| `ItemUsageValue::IsNeededForQuest` | no quest needs the item | false | corpse-refusal debug facts; game object branch unreachable | LESS |
| `ItemUsageValue::IsFutureUpgrade` | not an upgrade | false | `LootStrategyValue::CanLoot` does not wait for an upgrade | LESS |
| `ItemUsageValue::QueryItemUsageForEquip` | no equip use | `ITEM_USAGE_NONE` | `HasUpgradeValue` (not registered): no upgrade | LESS |
| `ItemUsageValue::GetRecipeSpell` | teaches no recipe | 0 | `UseRandomRecipe` finds nothing | LESS |
| `GuildOrderSkinMobFor` | no guild order wants a skin from this mob | false | a corpse with nothing else to take is not kept in the stack | LESS |

Host services (rule c, `forever/src/host/`): none needed. Files added to the slice (rule b): none. New unported row: `item usage` (the four `ItemUsageValue` stand-ins).
Removed: the seven `LootObject` / `LootObjectStack` stand-ins of slice 1 (`SliceLinkStubs.cpp`, former lines 310-317) and their row `loot object stack`, because `LootObjectStack.cpp` is real now.

## 8. Host test commands (`forever/src/TbScripts.cpp`, spec section 7)

Every new command refuses with "the engine is not built in (this build has no test commands for it)" when `TortoiseBots::engine::Built()` is false (checked in the option-OFF build: it compiles, the six objects are unchanged).

| Command | What it does | Core call |
|---|---|---|
| `.tb level <bot> <n>` (1-60) | the core's own level-up path of `.character level` (cs_character.cpp:914-917) | `Player::GiveLevel` (Player.h:1356), `InitTalentForLevel`, `SetXP(0)` |
| `.tb train <bot>` | free class training at the bot's level, no spell id in the module, two passes repeated while one teaches something. **Pass 1: the class trainers** (`ObjectMgr::GetTrainers`, ObjectMgr.h:1628, the world database's `trainer` / `trainer_spell` rows; type `Trainer::Type::None` = the class trainers, 254 of 716 rows, e.g. the priests' trainers 7 and 8; `Trainer::Type::Talent` is a different set of 8 and taught nothing in the first boot: the spec's "Talent" is a misreading; filter `Trainer::GetClassMask()` holds the class) through the core's `Trainer::TeachAvailableSpellsFree` (Trainer.h:94). **Pass 2: the class skill-line abilities** (`sSkillLineAbilityStore`, the DB2 data the core's own `.learn my trainer` reads, cs_learn.cpp:144-181; class spell family `ChrClasses.SpellClassSet`, `IsSpellFitByClassAndRace`, `SpellMgr::IsSpellValid`, and `SpellInfo::SpellLevel <= level`, which the core's command lacks): this is where a priest's Smite and Lesser Heal come from, they are in no trainer table | `Trainer::TeachAvailableSpellsFree`, `Player::LearnSpell` (Player.h:2028) |
| `.tb hurt <name> <percent>` | health to that percent of the maximum (1 at least), any logged-in character | `Unit::SetHealth` |
| `.tb power <bot> mana\|rage\|energy <percent>` | that power as a percent of its maximum (rage: percent of 100 = rage points, so `.tb rage` keeps working: it stays as the older spelling) | `Unit::SetPower` (Unit.h:839) |
| `.tb additem <bot> <item id> [count]` | a new item into the bags | `Player::StoreNewItemInBestSlots` (Player.h:1626) |
| `.tb use <bot> <item id> [self]` | the core's own item cast (spike U1); id 0 only reports mana, cast, movement and auras; prints the item's legacy flag, effect count and on-use spell first | `Player::CastItemUseSpell` (Player.h:2683) |
| `.tb castlog <bot> [on\|off]` | with a word: the cast-result sink of a bot without an engine; without: prints and drains the queue | `BotSession::SetCastResultSink`, `DrainCastResults` |
| `.tb where` | gained `money: N copper (Xg Ys Zc), mana a/b` | `Player::GetMoney` |
| `.tb castfail` (first package) | unchanged | |

## 9. Boots (build VM `~/forever-run`, binary from `~/forever-build/r3-on`, a copy of the slot-1 option-ON build, so the slot could go back to OFF while waiting)

The test server is shared. Boot 1 (21:04) was **taken over by the other worker's console driver within a minute**: their commands (`tb login Tbbeta`, `tb engine Tbalpha on`, `tb engine Tbgamma on`) arrived in my server before mine, so a slice-1 gate run was starting on a slice-2 binary. I stopped my server (I had started it), put their binary `worldserver.pre-r3` back, and waited. Boot 2 (21:07) the same driver sent `tb where Tbbeta` and `tele name Tbbeta RFC` after about five minutes; I took the U1 and castfail observations, stopped and restored again. Boot 3 (21:12) ran clean to the end (no foreign command), about 12 minutes. Boot 4 (21:22, the narrowed `.tb train`, 1 minute) saw the same driver again (`tb login Tbalpha Tbbeta`, `tb login Tbgamma Tbbeta`, `tb engine Tbalpha on`, `tb engine Tbgamma on`, `tb status`), so I stopped it at once; the fifth boot had to wait for the server. Boots used: 5 of 8 (the fifth is in 9.1). `ASSERT|Segmentation` lines in `world.out`: 0 in all three. The binary of the other worker was restored after each (`cmp` not needed: the copy is `worldserver.pre-r3`, taken before the first overwrite).

Characters: master `Tbbeta` (level 3 priest, slice 1's), warrior `Tbalpha` (level 2), new priest `Tbpriest` (`.tb create 1#1 Tbpriest 1 5`, then `.tb level Tbpriest 10`), all in one party by `.tb login <bot> Tbbeta`.

### 9.1 Registry and train

* `slice: registered 8/23/56/166 (strategies/actions/triggers/values)`.
* `.tb level Tbpriest 10` -> `Tbpriest is level 10: health 187/187, mana 487/487`.
* First `.tb train` (trainer type Talent): `learned 0 spell(s) from 8 class trainer(s)` (the type filter above). After the fix (types None + the skill-line pass): `learned 15 spell(s) from 45 class trainer(s) and 15 from the class skill lines, in 2 pass(es)`; `.tb spells Tbpriest` = 87 spells, with `2050:Lesser Heal`, `591:Smite` and `585` (Smite rank 1), `17:Power Word: Shield`, `139:Renew`, `8092:Mind Blast`, `1243` Power Word: Fortitude.
* The first `.tb train` also taught things that are not the priest's (riding passives, a tailoring recipe): 45 "class trainers" matched the priest mask, among them trainers whose spells name no class (the core's `GetClassMask` says "all classes when the data does not say", Trainer.h:96). The trainer pass now takes only trainers whose mask IS the class (`GetClassMask() == classMask`).
* **Boot 5 (21:34, a fresh priest `Tbhealer`, boot 5 of 8; the other worker's driver stayed away):** at level 1 the new priest already knows `2050:Lesser Heal` and `585:Smite` (42 spells); `.tb level Tbhealer 10`; `.tb train Tbhealer` -> `learned 11 spell(s) from 33 class trainer(s) and 15 from the class skill lines, in 2 pass(es)`; 68 spells, no riding and no recipe; it now holds `2052`/`2053` Lesser Heal ranks, `594`/`589` Shadow Word: Pain, `139`/`425268` Renew, `17` Power Word: Shield, `1243` Power Word: Fortitude, `591` Smite rank 2, `8092` Mind Blast, `2006` Resurrection, and also spells of the core's own trainer data whose names are retail-era (`431655` Mind Spike, `401977` Shadowfiend, `402004` Pain Suppression, `401946` Circle of Healing, `425204` Void Plague, `425294` Dispersion, `402668` Vampiric Touch, `401955` Shadow Word: Death): the core's trainer and skill-line rows give them at this level, the module does not choose. `ASSERT` lines 0, server stopped, binary restored.

### 9.2 Cast results (spike U3 and the cast-result path)

* Injected: `.tb castfail Tbpriest 585 75` and `.tb castfail Tbpriest 2050 63 interrupt` -> the action log: `[CAST_FAILED_ASYNC] spell=585 reason=75 injected=1`, `[SPELL_FAILURE] spell=2050 reason=63 injected=1`; `.tb where`: `cast results: queued 3, delivered 3, dropped 0 (queue holds 32), pending 0; refused during the engine's own tick (not queued) 0, unreadable 0`.
* **Real, async, from the core (boot 2):** the engine's `drink` while the console cast of the same drink was running: `[ACTION] A:drink - OK`, then 285 ms later `[CAST_FAILED_ASYNC] spell=430 reason=91` (`SPELL_FAILED_NOT_READY`): a refusal the core sent from the end of the cast, heard through the queue.
* **Real, async (boot 3):** Smite on a Kobold Vermin the party killed: `[CAST_START] spell=Smite(585) ... castTime=1500ms`, `[CAST_OK] result=0`, 1.5 s later `[SPELL_FAILURE] spell=585 reason=63` (the target died during the bar): `last spell cast` cleared by `SpellInterrupted`, and the priest's next action followed in the same second.
* **U3 (does the core cancel a spline-moved bot's cast): NO.** `.tb cast Tbpriest 2050` (Lesser Heal, 1.5 s, health 74 -> 125) then `.tb go Tbpriest -12 0` 0.3 s later: first `.tb castlog` 0.4 s after the walk was asked: `casting now yes, moving now no`; second, 1.5 s later: `casting now no, moving now yes`, 0 results, the heal landed. The core does not start the generator's movement while a movement-interruptible cast is preparing: `PointMovementGenerator.cpp:66` and `:153` (`owner->IsMovementPreventedByCasting()`, `Unit.cpp:3493-3508`); the Follow generator does the same (`FollowMovementGenerator.cpp:110`). So a MotionMaster-moved bot never gets reason 75; its walk waits for the cast. Observation g of the live test is therefore "no cancel, the movement waits"; a reason-75 result can only come from client-style movement flags (Spell.cpp:4336), which a bot session never produces.

### 9.3 U1: drinking: PASS (the eight points of SLICE2-I2-NOTES section 3)

Water: the vendor data of Innkeeper Farley (`npc_vendor` entry 295: items 159, 1179, 1205, 1708, 1645, 8766); `.tb item 159` -> `"Refreshing Spring Water": class 0/5, required level 1, spells 430 (trigger 0)`.

1. `.tb use Tbpriest 159`: `item 159: legacy no, 1 effect(s), on-use spell 430, count 6.` (not legacy, an ON_USE effect).
2. Food/drink recognised: the engine itself chose `drink` (`A:drink - OK`) and the helper passed the gate (no `item use except food and drink` count).
3. The drink spell is a pure self spell: the cast went through (aura 430).
4. Empty `SpellCastTargets` accepted: no `SPELL_FAILED_BAD_TARGETS`; the `self` word of `.tb use` was not needed.
5. Through the engine: `A:drink - OK src=high mana` (the trigger's own name, as T) and `[CAST_OK]`; console: `mana 48/487` -> `62/487` at the cast, aura `430` in the list, then `160/487` and `239/487` 3 s apart (rising about 80 per 3 s), `casting: no` (the drink is an aura, not a cast bar). The item count went 7 -> 6 at the cast (consumed at once).
6. A refusal: the real async `CAST_FAILED_ASYNC spell=430 reason=91` of 9.2 (the engine's drink while one was running), the action reported OK first (the refusal came after the call).
7. The last item of a stack was not tried (5 + 2 in the bags).
8. Moving: `ai->StopMoving()` is in the helper; not exercised (the priest stood).
Decision D3 is therefore not applied: drinking stays in the slice.

### 9.4 Smoke: heal and money loot

* Heal: `.tb hurt Tbbeta 40` (the master, 32/82): the priest's log `PUSH:lesser heal on party ... src=party member medium health`, `[CAST_START] spell=Lesser Heal(2050) castTime=1500ms`, `[CAST_OK]`; `.tb where Tbbeta` 6 s later `48/82`, and `122/122` within 15 s (his maximum grew by the priest's Power Word: Fortitude). `A:heal on party - IMPOSSIBLE` (the priest at level 10 does not know Heal, level 16) and `lesser heal on party` is the alternative it took.
* Money: master walked to the kobolds at Northshire (`.tb go`, then `.tb attack Tbbeta`), killed a Kobold Vermin; `.tb where`: **Tbpriest 4 copper, Tbalpha 2 copper**, master 0 (it is a spike bot with no engine); the action logs of both bots: `A:loot - OK src=loot available`, `A:open loot - OK src=can loot`, `[PUSH] PUSH:store loot ... src=loot response`, `A:store loot - OK`. One window per bot, no second open.
* Perf: `.tb perf` 11,427 ticks: world update time avg 1.14 ms, worst 22 ms; the module's average at 3 bot sessions was 44 us in the first window (its worst, 1.5 s, is the first tick: the engine config load).

### 9.5 `slice: missing` lines (61 over boots 2-3: 5 actions, 48 triggers, 8 values), explained

* 48 triggers: 28 are the trigger nodes of `default` itself (`activate taxi`, `area trigger`, `within area trigger`, `cannot equip`, `check mount state`, `force rebuff pending`, `gossip hello`, `group invite`, `group set leader`, `guild accept`, `honorgain`, `item push result`, `levelup`, `loot roll won`, `not enough money`, `not enough reputation`, `petition offer`, `ready check finished`, `release spirit`, `revive from corpse`, `summon request`, `taxi done`, `trade status`, `trade status extended`, `uninvite`, `uninvite guid`, `use game object`, `xpgain`: packet features outside this slice, decision D6); 7 quest packet triggers (`confirm quest`, `quest details`, `quest update add item`, `add kill`, `complete`, `failed`, `failed timer`); 7 more packet names `default` lists as supported (`loot start roll` and `loot vote`: rolls time out on F; `party command`, `ready check`, `bg status`, `random bot update`, `inventory change failure`); 6 state triggers of strategies the kit pulls in (`combat stuck`, `combat long stuck`, `feared`, `mounted`, `party member dead`, `resurrect`: stuck and fear handling, mounts and resurrection are outside the slice).
* 5 actions: `apply oil`, `check mail`, `check mount state`, `check values`, `use lightwell` (features outside the slice: oils, mail, mounts, a priest's lightwell).
* 8 values: `enemy player target`, `has enemy player targets`, `keepout set aside`, `outfit list`, `runners let go`, `unreachable entries`, `unreachable targets` (PvP, equip and dungeon-run values, none used by the kit) and **`incoming heals`** (read by `PartyMemberToHeal.cpp:114,222` as a `std::map<ObjectGuid, uint32>`: the stand-in answers an empty map, "nobody else is healing this member", which is neutral for one priest and would overheal a target a second healer is already healing: registering `IncomingHealsValue` (PartyMemberToHeal.h) is the first thing to add if two healers ever play together).
* The whole list is in `/tmp/r3-missing-3.txt` on the build VM; slice 1's list is in SLICE1-R6B-NOTES.

## 10. Open items and what the live test (R4) should look at first

1. `.tb train` was checked on a fresh priest (9.1); the retail-era spells it also gives (Mind Spike, Shadowfiend...) are the core's own data: a test that wants only 1.12 spells has to say so.
2. **Observation g (U3) is answered**: no cancel on MotionMaster movement; the walk waits (section 9.2). The live test's g becomes "the heal lands, the bot moves afterwards"; only a `.tb castfail ... 75` can show reason 75.
3. **Observation j (drinking) passes**, including the engine's own `A:drink - OK` and a real async `reason=91` (item on cooldown / already drinking): watch whether the engine retries it every tick (one `CAST_FAILED_ASYNC spell=430` line per attempt) after a refusal.
4. **Money**: both bots took their own share of a kobold's copper (4 and 2 copper: the party split of LootHandler.cpp:150-179, or two personal loots: not yet told apart; `.tb where` shows the money line). The master's share is the part of the split that goes to it: compare Tbbeta's money, which stayed 0 in the boot (it is a spike bot standing 4 yd away: the core's `IsAtGroupRewardDistance` decides).
5. **`incoming heals`** is unregistered (section 9.5): neutral for one healer.
6. **The other worker's console driver reaches any server that is up** (section 9). A live test that needs a clean run should be told when it may start, or run against its own run directory.
7. Spec section 4 rows `incoming: forced speed change` and `incoming: knock back` ("not needed on F") are still in the table (each counts one registration per engine); their packet handlers are not served either way. Remove them with the next change to `TbCompat_Unported.*`.
8. `UNPORTED.md`'s header count ("56 features") was stale before this package (R9 counted 70; the boot line says 74 now: slice 2 added `item usage`, merged the loot rows and removed `loot object stack`).

## 11. Gates (final commit)

| Gate | Result |
|---|---|
| Option ON `worldserver` build (slot 1, `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 build.sh 12 worldserver`) | `CMAKE_RC=0 MAKE_RC=0` (the last incremental build after the final host edit; the first full pass is section 7) |
| Option OFF module build (`-DTORTOISEBOTS_SLICE1=OFF build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`; 6 objects (`TbBotMgr`, `TbBotSession`, `TbScripts`, `TbItemPrototype`, `TbSpellEntry`, `host/TbEngineBrain`): the list R9 states, nothing new |
| Census `FOREVER_CENSUS_LIST=forever/SLICE2.txt` | `files 160  compile clean 160  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| Census `forever/SLICE1.txt` | see the report (run after the last code commit) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | file current; `(a)=0  (b)=234` |
| `tools/verify_tortoise_surface.sh` | `Tortoise 1.18.1 Core module surface: OK` |
| `forever-build/policy-tests.sh` (a private copy of the tree and script: `tb-r3`, `pt-r3`) | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL: the baseline (the same six files as SLICE1-R9-NOTES) |
| Turtle `vm-build ~/wt-slice2-integ --wait` | see the report |
| Turtle `vm-test tb ~/wt-slice2-integ` | see the report |
| The test server | stopped after every boot; `docker ps` shows `forever-bnet`, `forever-db` and no `forever-world`; the other worker's `worldserver` binary was put back after each boot |
