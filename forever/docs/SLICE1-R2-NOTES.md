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

TBD
