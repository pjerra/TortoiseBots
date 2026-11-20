# Slice 11, packages K11h, K11w, D11, P11 and H11: the hunter and warlock class code, their pets, data, kits and keys

Branch `s11/pets` (worktree `~/wt-s11`), build slot 8 (`FOREVER_SLOT=8`, `src-8` / `build-8` on `/mnt/forever`), off `tbf/main` feab6c882 (slices 1-6 merged; the newest list is SLICE5.txt). Spec: `~/TortoiseBots-audit/FOREVER-SLICE11-PETS-SPEC.md` with "Lead decisions" (D1-D6 as recommended). Core read only at the pin 6517af05, zero core changes. Reference module `~/ref-wfb` (GPL-2.0) read for the order of the pet steps, nothing copied. Not in this package: M11 (the merge with the S8 and S10 lines) and the live test (section 9).

## 1. The list

`forever/SLICE11.txt` = **SLICE5.txt (176 paths) + 25**: the 22 `.cpp` of `strategy/hunter/` (11) and `strategy/warlock/` (11) (the spec's table counts 10 hunter files, but `HunterTriggers.cpp` is in its place list: 3,250 hunter lines = 11 files), and the three files the keys of section 5 need (`CancelChannelAction.cpp`: `cancel channel`; `CombatActions.cpp`: `switch to melee` / `switch to ranged`; `ActiveSpellValue.cpp`: `active spell`). **201 paths**, each checked to exist, no duplicates. CMake (`TB_SLICE_LIST`) and `slice_registry_check.py` read it; the registry check counts the hunter and warlock contexts as registered (as warrior and priest). The S8 and S10 lists are unioned at the merge (S8 also adds `CancelChannelAction.cpp`: the same line).

## 2. K11h and K11w: files ported (census before / after in section 8)

| File | Change |
|---|---|
| `hunter/HunterActions.cpp:161` | `GetHappinessState() != HAPPY` -> `!PetIsHappyCompat(pet)` (existing, TbCompat_V.h:210). F keeps a bot session's hunter pet at full happiness every update (Pet.cpp:724-727, G7): the feed branch never fires |
| `HunterActions.cpp:201, :233 x2` | `GetUInt32Value(PLAYER_AMMO_ID)` -> `AmmoItemIdCompat(bot)` (existing, TbCompat_Unported.h:284, `Player::GetAmmoId`); the set + `UpdateDamagePhysical` -> `SetAmmoCompat(bot, id)` (new) |
| `HunterActions.cpp:289` | `GetDistance(target, SizeFactor::CombatReach)` -> `GetDistanceCompat` (existing, TbCompat_Unit.h:106) |
| `HunterActions.cpp:412` | the threat-list walk (`getThreatList`, `HostileReference`) -> `ForEachThreatListTargetCompat(attacker, fn)` (new) |
| `HunterActions.h:423` | `creature->GetCreatureInfo()->isTameable()` -> `IsTameableCompat(bot, creature)` (new) |
| `HunterTriggers.h:229 x2` | `PLAYER_AMMO_ID` -> `AmmoItemIdCompat` |
| `HunterTriggers.h:268, :351, :581` | `GetDistance(.., CombatReach)` -> `GetDistanceCompat` |
| `HunterTriggers.h:400` | `GetCombatDistance` -> `GetCombatDistanceCompat` (existing, TbCompat_V.h:233) |
| `HunterTriggers.h:308, :514` and `HunterRangedTelemetry.h` | `IsImmobilizedStateCompat` (new on F; the Turtle one is ai/cmangos-compat-shim.h:64) by including the K11h wrapper |
| `warlock/WarlockActions.cpp:36` | `StoreNewItemInInventorySlot` -> `AddItemCompat(bot, id, count)` (new; F `StoreNewItemInBestSlots`, the call `.tb additem` makes). Reached only under the item cheat, which the slice keeps off (D3) |
| `WarlockActions.h:236` | `to_string(GetRawValue())` -> `to_string(GuidKeyCompat(guid))` (existing) |
| `WarlockActions.h:710` | `IsImmuneToSpellEffect` -> `IsImmuneToSpellEffectCompat` (existing, TbCompat_P.h:115, its F side bounds the effect index) |
| `WarlockSpreadTargeting.h:28` | `HasExtraFlag` -> `CreatureExtraFlagCompat(creature, flag)` (new; F reads `CreatureTemplate::flags_extra`) |
| `WarlockTriggers.cpp:44` | `Spell::CalculatePowerCost` -> `SpellPowerCostCompat` (existing, TbCompat_Unit.h:521) |
| `actions/CombatActions.cpp:45` (a list member since this slice) | `bot->AttackStop(true)` -> `AttackStopCompat(bot, true)` (new; **S10's `TbCompat_S10.h` adds the same helper under the same name and body**: the two wrappers are never in one translation unit, one definition stays at the merge) |

Compat pairs, found through a wrapper each (the same pattern as K8 and K10: the two shim headers are untouched): `forever/src/compat/TbCompat_K11h.h` + `ai/cmangos-compat-k11h.h` (wrapper `ai/playerbot/TbCompatK11h.h`), `TbCompat_K11w.h` + `cmangos-compat-k11w.h` (`TbCompatK11w.h`), `TbCompat_P11.h` + `cmangos-compat-p11.h` (`TbCompatP11.h`). The Turtle side of every helper is the original expression (the call is moved, not changed). `CreatureExtraFlagCompat` takes `CreatureFlagsExtra` on both cores: the Turtle accessor `Creature::HasExtraFlag` is typed and a `uint32` argument is a compile error (the Turtle module build found it).

Different answers to watch live: `IsTameableCompat` reads a beast as not tameable when it is exotic and the bot cannot tame exotics (F's rule, asked of the caster); `SetAmmoCompat` answers false when F's `SetAmmo` refuses (no such stack in the bags, not usable), so the equip-ammo action does nothing then; `HunterTriggers.h:307, :508` and `HunterRangedTelemetry.h` still call the one-argument `GetDistance(target)`: on F that is the combat-reach distance (TbCompat_V.h note), on Turtle the centre distance minus the radii, so the dead-zone numbers (8 yd) may read a little differently (gate d).

## 3. P11: the pet host

**Compat pair P11** (`TbCompat_P11.h`, the first four moved out of `TbCompat_Unported.h` and `cmangos-compat-unported.h`):

| Helper | Turtle side | ForeverCore side (core cite) |
|---|---|---|
| `PetLearnSpellCompat(pet, id)` | `Pet::LearnSpell` | `Pet::learnSpell` (Pet.cpp:1962: adds, tells the owner, re-sends the pet bar, updates the training points) |
| `PetToggleAutocastCompat(pet, id, on)` | `Pet::ToggleAutocast(id, on)` | the two calls of the client's opcode (PetHandler.cpp:668-675): `Pet::ToggleAutocast(SpellInfo, on)` (Pet.cpp:2136) and `CharmInfo::SetSpellAutocast`; a spell the pet does not know or that is not autocastable is left alone |
| `PetDismissCommandCompat(bot, pet)` | pet action `COMMAND_DISMISS` | `COMMAND_ABANDON` (UnitDefines.h:466; for a summoned pet PetHandler.cpp:236 removes it, for a hunter pet it would DELETE it, which the helper refuses); the answer is read back: true only when the bot has no pet any more (G4) |
| `ForAllGuardiansCompat(bot, fn)` | `CallForAllControlledUnits(CONTROLLED_GUARDIANS)` | a snapshot of `Unit::m_Controlled` (Unit.h:1256) without the pet and totems (G5) |
| `PetDeadCompat(bot)` (`PetIsDeadValue`, StatsValues.cpp) | pet not alive, or the pet guid set with no object | live pet not alive, or an active stable row with `Health == 0` (G2: F removes a dead pet's body after its timer, Pet.cpp:670-677; Call Pet then refuses silently, Spell.cpp:6779-6787) |
| `HasStoredPetCompat(bot)` (`InitializePetAction::isUseful`) | the `character_pet` query (slot current or stable) | an active-slot row of `PetStable` whose creature exists (G1: F's current slot is -3 and its active slots 0-4, PetDefines.h:40-48, so the query finds nothing) |

The spec's `PetSpellsCompat` is not added: `PetSpellIdsCompat` (TbCompat_V.h:198, slice 2) already walks `m_spells`, and `TogglePetSpellAutoCastAction`'s `UnportedBlockCompat(Feature::Pets, ...)` block (GenericActions.cpp) is open code over it now. Call sites changed: GenericActions.cpp (include, stored-pet query, the block), StatsValues.cpp (`PetIsDeadValue`), PlayerbotAI.cpp (include only). The `pets and guardians` row of UNPORTED.md has no call site left.

**`forever/src/host/TbHostPetFactory.cpp`** (the spec's `TbPetFactory.cpp`; the name starts with `TbHost` so the existing `host/TbHost*.cpp` glob compiles it in the slice build only) defines the five `PlayerbotFactory` members that were link stand-ins (SliceLinkStubs.cpp, those lines are gone):

* `InitPet(saveToDb)` -> `p11::GiveHunterPet` (`TbPetFactory.h`): a hunter of 10 or more (`HUNTER_PET_MIN_LEVEL`) with no pet out and none in an active slot gets `Unit::CreateTamedPetFrom(entry)` (Unit.cpp:11656: pet, stats for the owner's level, a free active slot, the beast's own abilities by `LearnTameSpells`), `InitLoyalty(BEST_FRIEND)`, defensive, then the four steps of `Spell::EffectTameCreature` (SpellEffects.cpp:2757-2774: add to the map, `SetMinion`, `SavePetToDB(PET_SAVE_AS_CURRENT)`, `PetSpellInitialize`), the default autocast of every active spell, and `characters.summonedPetNumber` written at once (`SaveSummonedPetNumberCompat` -> F `DirectPExecute`, the character-database shim; the schema-guard test admits no other direct write; the ForeverCore team's setup does the same because a map change unsummons the pet and a later save would write 0, so a re-login would never call it back). The answer is read back (`bot->GetPet()` in the world). The legacy "force dismiss at the end" (`SetDeathState(JUST_DIED)`) and the pet name pool are not ported. The entry is the starter beast of the bot's race (D11: `forever/data/slice11/hunter-starter-pets.csv`), checked tameable and not exotic against the data at run time, else the faction fallback.
* `InitPetSpells()` and `TrainHunterPet(...)`: the legacy planner (`PlanHunterPetSpells`, `PetSpellRankPolicy.h`) on F's cost table (`Pet::GetSpellTrainingPoints`, the classic table: 0 = free; a spell id with no spell entry costs more than any budget) and F's points (`UpdateTrainingPoints`, one cost per spell chain, instead of `SetTP`); the warlock branch teaches the demon's lines by `ForEachWarlockWantedSpell` through `PetLearnSpellCompat`.
* `PetAutoTrainAllowed(bot)`: `AiPlayerbot.PetAutoTrain` (the key the legacy code names "pool bots and hires"). A host bot is a hired bot: no pool bot (`IsRandomBot` false) and no real player's alt, so the alts key (`PetAutoTrainAlts`, default 0) would switch the training off for every host pet. This is the one decision the spec left open ("config as T").
* `EquipAction.cpp` is **not** in the list: the warlock's firestone/spellstone actions call `EquipAction::EquipItem` (WarlockActions.cpp:60, :82), and the file pulls `RandomItemMgr`, `ai::ItemUsageValue`, `BotGearService`, `ClaimLifecycle` and `PlayerbotDbStore` behind it (nine symbols in five owners outside the slice; the option-ON link lists them). `SliceLinkStubs.cpp` answers the one symbol the two call sites use: the stone stays in the bags (the bot does less; the stones are level 28 and 36, above the slice's 18-22). When a later slice ports the gear services, delete the stand-in.
* `InitAmmo()`: no stock (D3). The core picks the ammo the bot carries (`SetPreferDrawableAmmo(true)` + `AutoSelectAmmo`, Player.h:1527; a clientless bot would otherwise take a stack the client draws as a white box). `TbScripts.cpp` sets the preference at login for every host bot (G8).

**`.tb pet <bot> give|call|dismiss|show` / `order <words>`** (`forever/src/host/TbPet.h/.cpp`, reply grammar 2 through the one encoder, `RequireBuilt` first, the option-OFF build answers `engine-not-built`): documented in `TB-COMMANDS.md` section 10. `give` is `GiveHunterPet` + `InitPetSpells`; `call` and `dismiss` (hunter) go through the engine's `call pet` / `dismiss pet` actions (a cast: `status=started`), a warlock's `dismiss` through `PetDismissCommandCompat` (`ok` only when the pet is gone); `order` hands the words `autocast <spell>`, `aggressive`, `defensive`, `passive`, `follow`, `stay` to the chat `pet` action (`abandon` and `dismiss` are refused: D5); `show` lists the spells and training points. **State keys** (after `practice=`, hunter and warlock only): `pet=<entry>:<alive|dead|none>:<hp pct>`, `petreact=`, `petauto=`, `loyal=`, `ammo=<item>:<count>`, `shards=<n>`; `.tb grammar selftest` prints one example of each.

## 4. U3 and the mirror (a find, with a fix that is a no-op when the core is right)

`hotfixes.spell_name` (valid `hotfix_data` rows) names the warlock spells 980 and 1014 **"Bane of Agony"**; the engine finds a spell by the Classic name its action carries (`curse of agony`, ChatHelper name map and `SpellIdValue`), so the affliction rotation would never cast it. `TbSpellEntry.cpp` (the mirror) gives those two ids the name "Curse of Agony" only while the core's own name is "Bane of Agony" (a data fix on the core's side makes it a no-op); other locales are untouched. Whether the live name is the renamed one is gate a (`.tb spells Mlock`).

## 5. H11: registry keys, class contexts, kits

`SliceAiFactory.cpp` (own entries only; S8 and S10 add theirs in the same functions: a merge of line rows):

| | Hunter | Warlock |
|---|---|---|
| context | `GuardedContext<HunterAiObjectContext>` | `GuardedContext<WarlockAiObjectContext>` |
| spec tab | 1 (marksmanship; the slice never reads talents) | 0 (affliction) |
| role | damage | damage |
| combat | marksmanship, dps assist, ranged, aspect, sting, pet, buff (+ threat where `KeepsPartyThreatBackoff` says so) | affliction, dps assist, ranged, pet, curse, buff (+ threat) |
| non-combat | marksmanship, dps assist, aspect, pet, buff, follow, food, default | affliction, dps assist, pet, pet voidwalker, buff, follow, food, default |
| reaction | marksmanship | affliction |

Left out as the spec says: `cc`, `aoe`, `boost`, the warlock's `flee`.

Registry (`slice_gen_context.py` `SLICE11_KEYS`, the generator now also knows `ChatTriggerContext`; `slice: registered` goes from 17/48/75/170 to **17/56/85/173**): actions `initialize pet`, `toggle pet spell`, `set pet stance`, `pet attack`, `pet` (chat), `cancel channel`, `switch to melee`, `switch to ranged`; triggers `no pet`, `has pet`, `new pet`, `pet attack`, `pet` (chat), `enemy ten yards`, `enemy fifteen yards`, `low mana`, `target of attacker close`, `has aggro in raid group`; values `pet dead`, `pet happy`, `active spell`. Beyond the spec's list (named by the kits, each checked): `cancel channel` (Drain Soul and Arcane-style channels are cancelled), `switch to melee/ranged` (the dead-zone fallback), the five triggers and `active spell` (without it `CastAutoShotAction::isUseful` reads 0 != the spell id and re-casts Auto Shot every tick: MORE, not less). Left unregistered on purpose (each does less): chat `tame` (TameAction.cpp stays out), `flee with pet`, `boss tactics seek pet`, `thaddius *`, `manual saved string` (`choice pet`, `choice sting`, `choice aspect` and `.bot choice` stay off, as S10 D5), `apply oil`, `apply stone`, `equip upgrades`, `dispel enrage` (Tranquilizing Shot is level 60), `player has flag`, `nearest stealthed unit`, `warlock spread attacker my aura` (the raid DoT spread), `aoe fear`. `slice_registry_check.py`: `(a)=0`, `(b)=221` against SLICE11.txt with the four class contexts counted (before this slice, SLICE5: 215).

## 6. D11: data (`forever/data/slice11/`)

All from SELECTs on the world database of the forever test server (read only, TDB 2026-10-14 + the client DB2 70938); no id from memory. Generators are in the folder.

* `hunter-spells-1-22.csv` (43 rows), `warlock-spells-1-22.csv` (37 rows) by `gen_spells.py`: the trainer rows (hunter trainers 20 and 40, warlock trainers 14) up to level 22, the starting spells, and the rows the spec lists outside a spawned trainer (Tame Beast 1515 on trainer 40 at level 13 only; Revive Pet 982, Dismiss Pet 2641, Beast Training 5149, Feed Pet 6991 by `spell_learn_spell` of 883 and 1515; Summon Imp 688 and Summon Voidwalker 697 by quest). **Names:** the collaborator's `forever-spell-ids.csv` and `hotfixes.spell_name`; 29 hunter and 12 warlock ids carry no name in either table (`(name: .tb spells)`), the live `.tb spells` prints the core's own. The client's `SpellName.db2` could not be read here (the WDC5 reader of the dashboard script reads another layout).
* `kits-level18.csv`, `kits-level22.csv` by `gen_kits.py` (adapted from `slice10/gen_kits.py`, same inputs and rules): roles `marksmanship hunter` (dwarf, leather, a two-hander, a **bow** so the kit's ammo is Rough Arrow 2512; a gun and Light Shot 2516 are the recorded alternative) and `affliction warlock` (gnome, cloth, a staff and a wand); 19 rows per role, food and drink from `food-water-5-25.csv`. EMPTY rows (no sourced piece): head, trinket2 and the off-hand slot of a two-hander. As in D10, `.tb itemscan` / `.tb equip` need a live server: M11 reads each row's `EQUIP_ERR_*`; some picks are Turtle/Forever custom ids with `requiredLevel 0`.
* `hunter-starter-pets.csv`: one starter beast per race (human 1128 Young Black Bear, orc and troll 3121 Durotar Tiger, dwarf and gnome 1196 Ice Claw Bear, night elf 2042 Nightsaber, undead 1765 Worg, tauren 2958 Prairie Wolf; fallbacks 1128 and 2958): beast type, a family, `TypeFlags` tameable, not exotic, wild level 5-12 (`creature_template`, `creature_template_difficulty` DifficultyID 0, `creature_classic_level`). **Data gap:** the spec names the collaborator's `hunter-pets.csv`; its 77 rows are NPC humanoids (type 7, family 0: Dannal Stern, David Trias ...), not beasts, so it cannot feed a pet table (the file's own note says "family x creature_classic_level"; the join is on the wrong key). `TbHostPetFactory.cpp` carries the table above; `test_slice11_wiring.py` keeps the CSV and the table equal.
* `stocks.csv`: Rough Arrow 2512 and Light Shot 2516 (vendor 1285 Thurman Mullby sells both; 77 and 61 vendors), Tough Jerky 117 (not stocked: D2), Soul Shard 6265 (no vendor row: Drain Soul gives it, the test seeds 3).

## 7. Needs at merge (M11)

1. `CMakeLists.txt` (`TB_SLICE_LIST`, the FATAL_ERROR text) and `slice_registry_check.py` (`SLICE_LIST`, `CLASS_CONTEXTS`): one list `SLICE11.txt` = the union with S8's and S10's lines; the registry generator keys of S8 (`SLICE8_KEYS`), S10 and this slice are unioned; regenerate `SliceAiObjectContext.cpp` and check `--check`.
2. `SliceAiFactory.cpp`: one row per class in the same functions (this slice's rows are separate `else if` branches).
3. `AttackStopCompat` exists in `TbCompat_S10.h` and `TbCompat_K11h.h`: keep one (they are never in one translation unit before the merge).
4. `TbScripts.cpp` rows (`.tb cc`, `.tb strategy` of S8, `.tb pet` here) and the include; `TbPartyState.cpp` (one call each), `TbH3.cpp` (one selftest call each).
5. `forever/conf`: nothing new (no conf key added). The live setup sets nothing: `AiPlayerbot.PetAutoTrain` is 1 by default.

## 8. Gates (code commit 51fedcdb9; this section is a notes-only commit)

| Gate | Result |
|---|---|
| option-ON `worldserver` (slot 8, `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0 MAKE_RC=0`, `grep -c "undefined reference" make.log` = 0, `[225/225] Linking CXX executable bin/Release/bin/worldserver`; `nm -C worldserver` has the strong `ai::EquipAction::EquipItem(PlayerbotAI*, Player*, Item*, bool)` (the stand-in) |
| option-OFF module build (`-DTORTOISEBOTS_SLICE1=OFF`, target `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` (6 s); `ar t libtortoisebots.a` = 19 objects (bot manager, session, scripts, item/spell mirrors, engine brain, H3Off, H4, dashboard, the plan/run host files, TbPet) |
| census SLICE11 (slot 8) | `files 201  compile clean 201  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| census SLICE5 / 4 / 3 / 2 / 1 (slot 8) | `files 176 / 174 / 170 / 160 / 133  compile clean = files  with errors 0  error places 0` and `includes of a header ForeverCore does not have 0` in each |
| generator, registry | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=221` |
| `policy-tests.sh` (VM, tree `forever-build/tb`) | `472 PASS`, `4 COMPILE_FAIL`, `2 RUN_FAIL` = the baseline six (`test_combat_monitor_service`, `_race`, `test_decision_capture_files`, `test_run_trace_writer`; `test_decision_capture_codec`, `test_log_file_rotation`) |
| wiring tests | `test_slice11_wiring.py` OK (201 paths, 22 class files, 3 compat pairs, 8 starter races); `test_slice4_d4_wiring.py` 10 tests OK; `test_run_review_wiring.py` 9 tests OK |
| run tests (VM, g++ -std=c++17 -Wall -Wextra) | `test_run_orders` 128 checks, `test_run_c6` 28 checks, `test_run_facts` PASS, `test_run_liquid`, `test_run_cancel` PASS |
| Turtle `vm-build ~/wt-s11 --wait` | the first run (tree ba1842068) failed in five TUs: `ai/cmangos-compat-k11w.h:10:131: error: invalid conversion from 'uint32' to 'CreatureFlagsExtra'` (the Turtle `Creature::HasExtraFlag` is typed, GCC refuses the conversion); after the signature fix (sha 555d2c7d4): `COMPILE_SECONDS=70 CMAKE_RC=0 MAKE_RC=0`; final tree (sha 51fedcdb9): `COMPILE_SECONDS=2521 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0`, mangosd and realmd built |
| Turtle `vm-test tb ~/wt-s11` | first run found a real failure: `tools/test_forever_no_direct_queries` on `TbHostPetFactory.cpp:163` (`CharacterDatabase.DirectPExecute` for `summonedPetNumber`) -> routed through `SaveSummonedPetNumberCompat` (the shim counts 2 calls now); final tree: `RESULT: PASS kind=tb wt=wt-s11 sha=51fedcdb9 dirty=1 files=3876 rc=0 vm_duration=990s` (`dirty=1` is this notes file; 2671 engine tests), the scenario `action_loop` / `Mage stay FAILED x9+ at 4.0s` is the standing baseline of every tree today (same in `wt-dash-runs` and other logs) |
| files outside `forever/` changed by the fix | `ai/cmangos-compat-k11w.h` (the one-line signature; the Turtle expression is unchanged) |

## 9. The live test (M4 party, gates a-i of the spec)

(filled in at the end of the package)
