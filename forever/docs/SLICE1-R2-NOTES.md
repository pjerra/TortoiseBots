# Slice 1, package R2: slice registry, factory, missing-key guard, check tool, cmake option

Branch `slice1/r2`. Spec: FOREVER-SLICE1-SPEC section 4 ("The cut"), section 8 (risks 2 and 3). Only `forever/` and the root
`CMakeLists.txt` changed: no Turtle gate is needed (`git diff --stat` in the report).

## Files

| File | What |
|---|---|
| `forever/src/slice/SliceAiObjectContext.cpp` | GENERATED. `AiObjectContext::AiObjectContext(PlayerbotAI*)` of the slice build: 6 local `NamedObjectContext`s with 3 strategies, 10 actions, 27 triggers, 139 values. Logs `slice: registered 3/10/27/139` once. |
| `forever/tools/slice_gen_context.py` | The generator: the key lists (the spec's) at the top; each creator is copied byte for byte from the full header. `--check` fails when the file on disk is stale, `--report` lists registered classes whose defining `.cpp` is not in `SLICE1.txt` (none). |
| `forever/src/slice/SliceAiFactory.cpp` | The 12 `AiFactory::` members, compiled instead of `ai/playerbot/AiFactory.cpp`. |
| `forever/src/slice/SliceMissingKeyGuard.h` | The missing-key guard: `GuardedContext<Base>` (used as `GuardedContext<WarriorAiObjectContext>` by the factory) and the inert value stand-in. Header only. |
| `forever/tools/slice_registry_check.py` | (a) slice keys not in the full build (or with another creator / context flags), (b) keys the slice sources reference and the slice does not register. |
| `forever/tools/slice_sources_check.txt` | The two slice `.cpp` files, for `FOREVER_CENSUS_LIST=` (syntax-only census of just them). |
| `CMakeLists.txt` | Option `TORTOISEBOTS_SLICE1`. |

## Registered keys

Each entry is the text of the shared header's creator, in a context class of the same shape (same `shared` / `siblings`
flags), so `GetSiblingStrategy` and sibling replacement behave as in the full build. Three strategy contexts instead of the
spec's single "strategy context" keep the flags: `close` sits in `StrategyContext` (no siblings), `follow` in
`MovementStrategyContext` and `dps assist` in `AssistStrategyContext` (both with siblings). Actions, triggers and values each
come from one context class (`ActionContext`, `TriggerContext`, `ValueContext`). Registration order is the full build's: the
strategy contexts, then actions, triggers, values. The warrior entries are added by `WarriorAiObjectContext`, compiled as it is.

### Strategies (3)

| key | creator | full-build context |
|---|---|---|
| dps assist | `DpsAssistStrategy(ai)` | AssistStrategyContext |
| close | `MeleeCombatStrategy(ai)` | StrategyContext |
| follow | `FollowMasterStrategy(ai)` | MovementStrategyContext |

### Actions (10)

| key | creator | full-build context |
|---|---|---|
| melee | `MeleeAction(ai)` | ActionContext |
| reach melee | `ReachMeleeAction(ai)` | ActionContext |
| follow | `FollowAction(ai)` | ActionContext |
| stop follow | `StopFollowAction(ai)` | ActionContext |
| dps assist | `DpsAssistAction(ai)` | ActionContext |
| select new target | `SelectNewTargetAction(ai)` | ActionContext |
| set combat state | `SetCombatStateAction(ai)` | ActionContext |
| set non combat state | `SetNonCombatStateAction(ai)` | ActionContext |
| set dead state | `SetDeadStateAction(ai)` | ActionContext |
| interrupt current spell | `InterruptCurrentSpellAction(ai)` | ActionContext |

### Triggers (27)

| key | creator | full-build context |
|---|---|---|
| combat start | `CombatStartTrigger(ai)` | TriggerContext |
| combat end | `CombatEndTrigger(ai)` | TriggerContext |
| death | `DeathTrigger(ai)` | TriggerContext |
| enemy out of melee | `EnemyOutOfMeleeTrigger(ai)` | TriggerContext |
| invalid target | `InvalidTargetTrigger(ai)` | TriggerContext |
| in pve | `InPveTrigger(ai)` | TriggerContext |
| in pvp | `InPvpTrigger(ai)` | TriggerContext |
| in raid fight | `InRaidFightTrigger(ai)` | TriggerContext |
| often | `RandomTrigger(ai, "often", 5)` | TriggerContext |
| very often | `TimeTrigger(ai, "very often", 5)` | TriggerContext |
| random | `RandomTrigger(ai, "random", 20)` | TriggerContext |
| critical health | `CriticalHealthTrigger(ai)` | TriggerContext |
| almost full health | `AlmostFullHealthTrigger(ai)` | TriggerContext |
| low health | `LowHealthTrigger(ai)` | TriggerContext |
| target critical health | `TargetCriticalHealthTrigger(ai)` | TriggerContext |
| light rage available | `LightRageAvailableTrigger(ai)` | TriggerContext |
| medium rage available | `MediumRageAvailableTrigger(ai)` | TriggerContext |
| high rage available | `HighRageAvailableTrigger(ai)` | TriggerContext |
| melee light aoe | `MeleeLightAoeTrigger(ai)` | TriggerContext |
| melee medium aoe | `MeleeMediumAoeTrigger(ai)` | TriggerContext |
| melee high aoe | `MeleeHighAoeTrigger(ai)` | TriggerContext |
| melee very high aoe | `MeleeVeryHighAoeTrigger(ai)` | TriggerContext |
| ranged light aoe | `RangedLightAoeTrigger(ai)` | TriggerContext |
| ranged medium aoe | `RangedMediumAoeTrigger(ai)` | TriggerContext |
| not dps target active | `NotDpsTargetActiveTrigger(ai)` | TriggerContext |
| update follow | `UpdateFollowTrigger(ai)` | TriggerContext |
| stop follow | `StopFollowTrigger(ai)` | TriggerContext |

### Values (139)

| key | creator | full-build context |
|---|---|---|
| action useful | `ActionUsefulValue(ai)` | ValueContext |
| add hazard | `AddHazardValue(ai)` | ValueContext |
| and | `BoolAndValue(ai)` | ValueContext |
| aoe count | `AoeCountValue(ai)` | ValueContext |
| aoe heal | `AoeHealValue(ai)` | ValueContext |
| aoe position | `AoePositionValue(ai)` | ValueContext |
| attack target | `AttackTargetValue(ai)` | ValueContext |
| attacker of non-tank target | `AttackerOfNonTankTargetValue(ai)` | ValueContext |
| attackers | `AttackersValue(ai)` | ValueContext |
| attackers count | `AttackersCountValue(ai)` | ValueContext |
| attackers targeting me | `AttackersTargetingMeValue(ai)` | ValueContext |
| automated attack target | `ExplicitAttackTargetValue(ai, "automated attack target")` | ValueContext |
| bag space | `BagSpaceValue(ai)` | ValueContext |
| balance | `BalancePercentValue(ai)` | ValueContext |
| behind | `IsBehindValue(ai)` | ValueContext |
| can free move | `CanFreeMoveValue(ai)` | ValueContext |
| cc target | `CcTargetValue(ai)` | ValueContext |
| closest attacker targeting me | `ClosestAttackerTargetingMeTargetValue(ai)` | ValueContext |
| collision | `CollisionValue(ai)` | ValueContext |
| combat | `IsInCombatValue(ai)` | ValueContext |
| combat start time | `CombatStartTimeValue(ai)` | ValueContext |
| combo | `ComboPointsValue(ai)` | ValueContext |
| corpse run | `CorpseRunValue(ai)` | ValueContext |
| current cc target | `CurrentCcTargetValue(ai)` | ValueContext |
| current target | `CurrentTargetValue(ai)` | ValueContext |
| dead | `IsDeadValue(ai)` | ValueContext |
| death count | `DeathCountValue(ai)` | ValueContext |
| distance | `DistanceValue(ai)` | ValueContext |
| dps aoe target | `DpsAoeTargetValue(ai)` | ValueContext |
| dps target | `DpsTargetValue(ai)` | ValueContext |
| duel target | `DuelTargetValue(ai)` | ValueContext |
| durability | `DurabilityValue(ai)` | ValueContext |
| durability inventory | `DurabilityInventoryValue(ai)` | ValueContext |
| enemy healer target | `EnemyHealerTargetValue(ai)` | ValueContext |
| energy | `EnergyValue(ai)` | ValueContext |
| estimated group dps | `EstimatedGroupDpsValue(ai)` | ValueContext |
| explicit attack target | `ExplicitAttackTargetValue(ai)` | ValueContext |
| facing | `IsFacingValue(ai)` | ValueContext |
| focus heal targets | `FocusHealTargetValue(ai)` | ValueContext |
| follow target | `FollowTargetValue(ai)` | ValueContext |
| formation | `FormationValue(ai)` | ValueContext |
| formation position | `FormationPositionValue(ai)` | ValueContext |
| free move center | `FreeMoveCenterValue(ai)` | ValueContext |
| free move range | `FreeMoveRangeValue(ai)` | ValueContext |
| group | `IsInGroupValue(ai)` | ValueContext |
| group and | `GroupBoolANDValue(ai)` | ValueContext |
| group members | `GroupMembersValue(ai)` | ValueContext |
| group or | `GroupBoolORValue(ai)` | ValueContext |
| group ready | `GroupReadyValue(ai)` | ValueContext |
| has aggro | `HasAggroValue(ai)` | ValueContext |
| has area debuff | `HasAreaDebuffValue(ai)` | ValueContext |
| has attackers | `HasAttackersValue(ai)` | ValueContext |
| has mana | `HasManaValue(ai)` | ValueContext |
| has strategy | `HasStrategyValue(ai)` | ValueContext |
| has totem | `HasTotemValue(ai)` | ValueContext |
| hazards | `HazardsValue(ai)` | ValueContext |
| health | `HealthValue(ai)` | ValueContext |
| ignore rpg target | `IgnoreRpgTargetValue(ai)` | ValueContext |
| incoming damage | `IncomingDamageValue(ai)` | ValueContext |
| inside target | `InsideTargetValue(ai)` | ValueContext |
| interrupt target | `InterruptTargetValue(ai)` | ValueContext |
| invalid target | `InvalidTargetValue(ai)` | ValueContext |
| inventory items | `InventoryItemValue(ai)` | ValueContext |
| item count | `ItemCountValue(ai)` | ValueContext |
| item for spell | `ItemForSpellValue(ai)` | ValueContext |
| last area trigger | `LastMovementValue(ai)` | ValueContext |
| last long move | `LastLongMoveValue(ai)` | ValueContext |
| last movement | `LastMovementValue(ai)` | ValueContext |
| last spell cast | `LastSpellCastValue(ai)` | ValueContext |
| last spell cast time | `LastSpellCastTimeValue(ai)` | ValueContext |
| last taxi | `LastMovementValue(ai)` | ValueContext |
| leader travel target | `LeaderTravelTargetValue(ai)` | ValueContext |
| lowest durability | `LowestDurabilityValue(ai)` | ValueContext |
| main tank | `PartyMemberMainTankValue(ai)` | ValueContext |
| mana | `ManaValue(ai)` | ValueContext |
| mana save level | `ManaSaveLevelValue(ai)` | ValueContext |
| manual bool | `BoolManualSetValue(ai)` | ValueContext |
| manual follow target | `ManualFollowTargetValue(ai)` | ValueContext |
| manual int | `IntManualSetValue(ai)` | ValueContext |
| manual saved int | `IntManualSetSavedValue(ai)` | ValueContext |
| manual string | `StringManualSetValue(ai)` | ValueContext |
| manual time | `TimeManualSetValue(ai)` | ValueContext |
| master target | `MasterTargetValue(ai)` | ValueContext |
| mounted | `IsMountedValue(ai)` | ValueContext |
| moving | `IsMovingValue(ai)` | ValueContext |
| my attacker count | `MyAttackerCountValue(ai)` | ValueContext |
| my threat | `MyThreatValue(ai)` | ValueContext |
| nearest adds | `NearestAddsValue(ai)` | ValueContext |
| nearest damaging traps | `NearestDamagingTrapsValue(ai)` | ValueContext |
| nearest dynamic objects no los | `NearestDynamicObjects(ai, kTrapScanRangeYd, LOS_IGNORE)` | ValueContext |
| nearest friendly players | `NearestFriendlyPlayersValue(ai)` | ValueContext |
| nearest game objects | `NearestGameObjects(ai)` | ValueContext |
| nearest game objects no los | `NearestGameObjects(ai, sPlayerbotAIConfig.sightDistance, LOS_IGNORE)` | ValueContext |
| nearest npcs | `NearestNpcsValue(ai)` | ValueContext |
| nearest stealthed units | `NearestStealthedUnitsValue(ai)` | ValueContext |
| neglect threat | `NeglectThreatValue(ai)` | ValueContext |
| new player nearby | `NewPlayerNearbyValue(ai)` | ValueContext |
| old target | `CurrentTargetValue(ai)` | ValueContext |
| party member to protect | `PartyMemberToProtect(ai)` | ValueContext |
| party member to remove roots | `PartyMemberToRemoveRoots(ai)` | ValueContext |
| party member without aura | `PartyMemberWithoutAuraValue(ai)` | ValueContext |
| party member without my aura | `PartyMemberWithoutMyAuraValue(ai)` | ValueContext |
| party tank without aura | `PartyTankWithoutAuraValue(ai)` | ValueContext |
| pet target | `PetTargetValue(ai)` | ValueContext |
| pos | `SinglePositionValue(ai)` | ValueContext |
| position | `PositionValue(ai)` | ValueContext |
| possible adds | `PossibleAddsValue(ai)` | ValueContext |
| possible attack targets | `PossibleAttackTargetsValue(ai)` | ValueContext |
| possible targets | `PossibleTargetsValue(ai)` | ValueContext |
| possible targets no los | `PossibleTargetsValue(ai, "possible targets", sPlayerbotAIConfig.sightDistance, true)` | ValueContext |
| possible triggers | `PossibleTriggersValue(ai)` | ValueContext |
| pull target | `PullTargetValue(ai)` | ValueContext |
| rage | `RageValue(ai)` | ValueContext |
| random bot update | `RandomBotUpdateValue(ai)` | ValueContext |
| range | `RangeValue(ai)` | ValueContext |
| rpg target | `RpgTargetValue(ai)` | ValueContext |
| rti | `RtiValue(ai)` | ValueContext |
| rti cc | `RtiCcValue(ai)` | ValueContext |
| rti cc target | `RtiCcTargetValue(ai)` | ValueContext |
| rti target | `RtiTargetValue(ai)` | ValueContext |
| run cc family | `RunCcFamilyValue(ai)` | ValueContext |
| self target | `SelfTargetValue(ai)` | ValueContext |
| skip spells list | `SkipSpellsListValue(ai)` | ValueContext |
| snare target | `SnareTargetValue(ai)` | ValueContext |
| spell cast useful | `SpellCastUsefulValue(ai)` | ValueContext |
| spell id | `SpellIdValue(ai)` | ValueContext |
| stance | `StanceValue(ai)` | ValueContext |
| stay time | `StayTimeValue(ai)` | ValueContext |
| stored hazards | `StoredHazardsValue(ai)` | ValueContext |
| swimming | `IsSwimmingValue(ai)` | ValueContext |
| talent spec | `TalentSpecValue(ai)` | ValueContext |
| tank target | `TankTargetValue(ai)` | ValueContext |
| tank threat | `TankThreatValue(ai)` | ValueContext |
| tank to remove roots | `TankToRemoveRoots(ai)` | ValueContext |
| threat | `ThreatValue(ai)` | ValueContext |
| travel target | `TravelTargetValue(ai)` | ValueContext |
| trigger active | `TriggerActiveValue(ai)` | ValueContext |
| trinkets on use | `EquipedUsableTrinketValue(ai)` | ValueContext |
| wait for attack time | `WaitForAttackTimeValue(ai)` | ValueContext |


## Differences from the spec

The key lists are the spec's, unchanged: all 179 keys exist in the full build's contexts with the class the spec's header
names (checked by `slice_registry_check.py`, which compares the creator text and the context flags, not only the key).
Nothing in the spec's lists needed correcting. What differs in the design:

1. **Six local contexts, not four.** One per full-build context class that holds a slice key (`StrategyContext`,
   `MovementStrategyContext`, `AssistStrategyContext`, `ActionContext`, `TriggerContext`, `ValueContext`), so the
   `(shared, supportsSiblings)` flags of each stay what they are in the full build. A single strategy context would lose the
   siblings behaviour of `follow` and `dps assist` (`NamedObjectContextList::GetSiblings`).
2. **The guard is a template, in a header.** Spec: `SliceWarriorAiObjectContext` in `SliceAiFactory.cpp`. It is
   `GuardedContext<WarriorAiObjectContext>` in `SliceMissingKeyGuard.h` (the factory instantiates it); same effect, and
   another class context could be wrapped the same way later. The guard also covers strategy, trigger and action lookups
   (the spec named values only), and a missing value gets a stand-in, not only a log line.
3. **Two extra small files.** `SliceLog.h/.cpp`: `playerbot/playerbot.h` redefines `sLog` as `BotLog::Instance()`, which breaks
   the core's `TC_LOG_*` macros in any file that includes it (the first census run of this package showed it: "no viable
   conversion from 'BotLog' to 'Log *'"), so the slice's log lines go through two functions defined in a file that includes
   the core's `Log.h` only. Also `forever/tools/slice_gen_context.py`, the generator of `SliceAiObjectContext.cpp` (so the
   creator text is copied by a program, not by hand, and a change of the lists is one edit).
4. **The slice files include `compat/botpch_forever.h` themselves.** The CMake pre-include is forced on the legacy files only
   (the brief); the two slice sources that include legacy headers therefore name it in their first line.
5. **AiFactory defaults follow the spec, not the full kit.** The full `AddDefaultCombatStrategies` also adds `dungeon`, `mount`,
   `racials`, `default`, `duel`, `pvp`, `cast time` and, for a warrior, `behind`, `aoe`, `cc`, `boost`; the non-combat kit adds
   `buff`, `boost`, `wbuff`, `dungeon` and the config strings. The slice adds exactly the spec's names. `GetPlayerRoles` returns
   dps for a warrior and none for any other class (the full one also reads the forced role, stance and gear).
6. **`enemy is out of melee`** (a trigger named by `WarriorAiObjectContext.cpp:174`) is registered nowhere, as the spec says
   (risk 9); the check script lists it in (b) as "NOT IN FULL BUILD".

## The missing-key guard (`SliceMissingKeyGuard.h`)

`AiObjectContext::GetStrategy / GetTrigger / GetAction / GetUntypedValue` are virtual, so no shared header changes.
`GuardedContext<Base>` overrides the four:

- a miss is logged once per process and name: `slice: missing strategy|trigger|action|value <name>` (category `tortoisebots`,
  warning level). The qualifier is not part of the once-key: `distance::self target` and `distance::x` count as `distance`;
  the first full name seen is printed.
- a missing strategy, trigger or action stays null: `Engine.cpp` skips those (843-857, 945-949, 1051-1060).
- a missing value returns an inert stand-in. `GetValue<T>` is a non-virtual template, `dynamic_cast<Value<T>*>(GetUntypedValue())`,
  and the override cannot know T. The stand-in is one object, `InertValues<Ts...> : UntypedValue, InertValue<Ts>...`, with a
  `Value<T>` base for each of 16 types (`SliceInertValue` in the header); the cast is a cross-cast and finds the base. A read
  returns `T{}`, `Set` does nothing. One stand-in per full name lives in the context and dies with it.

**What it cannot catch:**
- a missing value read as a type that is not in the list: the cast gives null and `->Get()` crashes (the log line comes first, so
  the crash names its key). The check script flags those (`[stand-in lacks: ...]`); today there are none.
- a caller of `GetAction`, `GetTrigger` or `GetStrategy` that does not test for null.
- values reached without the context: `HasValue` / `HAS_AI_VALUE` answer from the created-set and never touch the guard (the full
  build behaves the same; note `NamedObjectContext::Create` stores a null for a missing key, so `HasValue` of a key that missed
  once reads true).
- values that exist but whose class is not linked (R6) or whose class compiles against a stub (R8's problem).
- the shared context (`sSharedObjectContext`, `GAI_VALUE`): no slice source uses it (grep, 0 files).
- a stand-in answers `T{}`: bools false, numbers 0, pointers null. A branch that needs another neutral answer is a value to register.

## CMake (`TORTOISEBOTS_SLICE1`, default OFF)

OFF: the only change is `list(FILTER ... "/forever/src/slice/*.cpp")` on the glob, so the new files are not compiled; the build
graph is the same as before (checked: `build.ninja` of the OFF build has no `slice` target, `CMAKE_RC=0`, `MAKE_RC=0`).
ON adds, in `CMakeLists.txt`:
- the legacy files of `forever/SLICE1.txt` (read with `file(STRINGS)` at configure time, `#` lines and blanks skipped, the
  list is a configure dependency; a listed file that does not exist is a `FATAL_ERROR`), minus `ai/playerbot/AiFactory.cpp` and
  `ai/playerbot/strategy/AiObjectContextRegistry.cpp` (excluded by name, so it does not matter whether R1's file exists or is listed);
- `forever/src/slice/*.cpp`;
- the pre-include `forever/src/compat/botpch_forever.h` as a `COMPILE_OPTIONS` source property of the legacy files only (`-include`,
  `/FI` on MSVC); not on `forever/src` files;
- `-I forever/compat -I forever/src/compat`, and the eight old include folders (`OLD_INCLUDE_DIRS` of `port_census.py` = the folders
  of `TortoiseBots.cmake`) as `-idirafter`, so a legacy header never shadows a core header of the same name (the census does the
  same);
- `-DMANGOSBOT_ZERO=1 -DCMANGOS=1` (private) and `-DTORTOISEBOTS_SLICE1=1` (public: R5's host code tests it).
Checked: configure with the option ON succeeds (`cmake ... -DTORTOISEBOTS_SLICE1=ON`, `CMAKE_RC=0`); the generated compile lines
carry the pre-include for `strategy/Engine.cpp` and not for `TbBotMgr.cpp` or the slice files, and `ai/playerbot/AiFactory.cpp`
is not a target.

## Syntax census of the slice files (`FOREVER_CENSUS_LIST=forever/tools/slice_sources_check.txt`)

Method: `FOREVER_SLOT=3 FOREVER_CENSUS_LIST=forever/tools/slice_sources_check.txt forever/tools/vm-census.sh "" 14` (clang
`-fsyntax-only`, the precompiled `botpch_forever.h`, the same flags as the full census).

| Run | Result |
|---|---|
| first | `files 2 ... error places 414 (in 36 files)`: 3 of them in my own files (below) |
| after the fix | `files 2 ... error places 411 (in 33 files)`: **0 places in `forever/src/slice/`**; `rc 1` of both files comes only from legacy headers |

**Caused by my code (fixed):** `TC_LOG_INFO` / `TC_LOG_WARN` after `playerbot/playerbot.h` ("no viable conversion from 'BotLog' to
'Log *'", 3 places: `SliceAiObjectContext.cpp`, `SliceAiFactory.cpp`, `SliceMissingKeyGuard.h`). `playerbot.h` redefines
`sLog` as `BotLog::Instance()`. Now the log goes through `SliceLog.h/.cpp`.

**Caused by legacy headers that do not compile on ForeverCore yet (the next packages' work), 411 places in 33 headers:**

| Family | Places | Where | Package |
|---|---|---|---|
| `Position` members `x y z mapId o`, `sTerrainMgr`, `const TerrainInfo`, `PathFinder` | ~250 | `WorldPosition.h` 225, `GuidPosition.h` 12, `PositionValue.h` 8, `WorldSquare.h` 4, `TravelMgr.h` 7, `TravelNode.h` 2 | R3 (compat headers) |
| inline code of action / trigger / value headers (`LineOfSightChecks`, `CanReachWithMeleeAutoAttack`, `IsRooted`, `GetAngle`, `HasNetworkTransport`, ...) | ~110 | `ReachTargetActions.h` 31, `RangeTriggers.h` 26, `GenericTriggers.h` 25, `MovementActions.h` 4, `GenericSpellActions.h` 3, `GenericActions.h` 2, `AttackAction.h` 1, `DistanceValue.h` 4, `AttackerCountValues.h` 3, `StatsValues.h` 4, a few single places | R4A, R4V |
| `PlayerbotAI.h`, `PlayerbotAIConfig.h`, `RandomBotFacade.h`, `Talentspec.h`, `ServerFacade.h` | ~31 | the PlayerbotAI class and helper headers | R4P, R4C |
| `ItemUsageValue.h`, `ItemVisitors.h`, `AuctionEntry`, `GuildValues.h` | ~9 | headers of removed-or-item files that the slice's headers still pull in | R3 / R6 |
| `Config/Config.h` does not exist on ForeverCore (the census gives it an empty stand-in, 2 includes) | 2 | `PlayerbotAIConfig.h` | R4C (`ConfigMgr` adaptor) |

Not one of them is in the two slice sources or in `SliceMissingKeyGuard.h`; the guard's extra includes (`LootObjectStack.h`,
`PatchwerkOpeningPolicy.h`, `RunnerFinishPolicy.h`, `GuildValues.h`, `TravelValues.h`) bring `GuildValues.h` (1) and `TravelMgr.h`
(7, also reached through the generic contexts' own headers) and nothing that is not already in the slice.

## Gates

- Option OFF: `ssh yulon-ubuntu2 'FOREVER_SLOT=3 ~/forever-build/build.sh 12 tortoisebots'` -> `CMAKE_RC=0`, `MAKE_RC=0`.
- Option ON configures: `CMAKE_RC=0`; 135 objects in `tortoisebots`, the three slice `.cpp` among them. It does not build yet (legacy headers).
- `python3 forever/tools/slice_registry_check.py`: `(a)=0  (b)=166`: strategies 46, actions 52, triggers 35, values 33 referenced by the
  slice's sources and not registered. Of those, 7 strategies, 4 actions and 2 triggers do not exist in the full build either
  (dead references or dynamic names), and every one of the 33 values exists in the full build. The 33 values are the to-do for R6/R8:
  `can move around`, `grind target`, `loot target`, `travel target active`, `enemy player target` and the like, each read as one of the
  16 types the stand-in covers.
