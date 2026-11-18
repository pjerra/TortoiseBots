# ForeverCore port, slice 1, round R4A: actions, triggers, generic strategies, warrior

Package R4A of the four parallel R4 packages. List: `forever/SLICE1-R4A.txt` (33 `.cpp` files under `ai/playerbot/strategy/actions`,
`triggers`, `generic`, `warrior`). Compat pair: `forever/src/compat/TbCompat_A.h` (ForeverCore) and `ai/cmangos-compat-r4a.h`
(Turtle, each helper forwards to the call the bot always made). Branch `slice1/r4a`, build slot 3.
Measure: `FOREVER_SLOT=3 FOREVER_CENSUS_LIST=forever/SLICE1-R4A.txt forever/tools/vm-census.sh "" 14`.

T = the Turtle core `~/playerbots-references/penqle-tortoise-wow-modsys/src/game`, F = the pinned ForeverCore `~/ForeverCore/src/server/game`
(unless the path says `common/` or `shared/`).

## Numbers

| Run | Error places (whole census) | Files holding them | Places in files this package owns |
|---|---|---|---|
| BEFORE (branch start, `9b32de181`) | 504 | 24 | 473 |
| AFTER (final code) | 159 | 17 | 128 |

"Files this package owns" are the 33 `.cpp` files and the headers of `actions/`, `triggers/`, `generic/`, `warrior/`. The other 31 places
(both runs) sit in headers of package C, V or P (see NEEDS FROM) and are counted in every translation unit that includes them.

Remaining places in files this package owns (128), by feature (all of them are listed under "Left open"):

| Places | File | Feature |
|---|---|---|
| 66 | MovementActions.cpp | transports 33, jump packets 12, taxi 11, game-object portals 4, navmesh island unstick 4, aura list 2 |
| 20 | GenericTriggers.h | quest triage 17, aura holder map 3 |
| 13 | GenericSpellActions.cpp | item and totem casts 6, heal estimate 2, ammo 2, visibility 2, school immunity 1 |
| 10 | GenericActions.cpp | pets (spells, autocast, stance, pet table) |
| 6 | ChangeStrategyAction.cpp | strategy store transaction |
| 4 | GenericTriggers.cpp | wand silence and lockout |
| 3 | UseItemAction.h | item spells |
| 3 | PullStrategy.cpp | thrown weapons and ammo |
| 2 | GenericActions.h | lightwell game-object use |
| 1 | ReachTargetActions.h | creature combat start position |

## Families closed and how (a: same name, b: both-core helper identical on Turtle, c: helper with a different answer)

Counts are sites in the files of this package (grep of the helper names in the final tree), not census places. About 345 census places
closed in all: roughly 40 by (a), 255 by (b), 50 by (c) (hand count).

| Family | How | Sites |
|---|---|---|
| core names printed through `%s` (`GetName()` is a `std::string` on F; `BotLog` and `SC_LOG` are printf style) | (b) `CStrCompat` (R3) at the call | 66 |
| `ObjectGuid::GetRawValue()` (a byte span on F) | (c) `GuidRawCompat` | 18 |
| `ObjectGuid` used as a bool | (a) `!x.IsEmpty()` (both cores have it) | 26 |
| `Event::GetSource` (the Turtle shim renames it) | (a) the real member `getSource` | 4 |
| line of sight, angle, height, hit position, combat reach, distances, in-arc | (b) the R3 helpers at the call sites, plus `IsWithinLOSCompat` | about 70 |
| bounding radius and melee reach of a world object | (c) `ObjectBoundingRadiusCompat`, `MeleeReachCompat` | 8 |
| movement flags | (a) `MOVEFLAG_*` as the F flag of the same fact; (b) `HasMovementFlagCompat`; (c) JUMPING | 7 + 6 |
| MotionMaster: point, follow, chase, clear; raw spline | (b)/(c) section 5 of `TbCompat_A.h` | 22 |
| path generator: calculate, points, type, end, length | (b) `PathCalculateCompat` and friends | 19 |
| liquid and terrain reads, liquid type flags | (c) `LiquidStatusCompat`, `TerrainIsSwimmableCompat`, `MAP_LIQUID_TYPE_*` | 18 |
| summon, teleport, emote state, yell range, underwater, near point | (b) one helper each | 25 |
| player and unit flag words (`HasFlag(PLAYER_FLAGS...)`) | (b) `HasPlayerFlagCompat`, `HasUnitFlagCompat` | 3 |
| member renames: `GetAuraMaxDuration`, `GetAttackTimer`, `IsRaidGroup`, `GetCastedTime`, `getUnitTargetGuid`, `GetMountID`, `SetTargetGuid` | (a) macros, each proven on both cores and checked free of collisions | about 25 |
| unit state and item trigger names | (a) `UNIT_STAT_MELEE_ATTACKING`; (c) `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE` | 11 |
| groups: member walk, main tank, scope guid | (b)/(c) `GroupFirstMemberCompat`, `GroupMainTankGuidCompat`, `GroupScopeGuidCompat` | 3 |
| spell objects: damage of an effect, mana cost, icon id, range modifier, state bound | (b)/(c) | 6 |
| pet commands (`CMSG_PET_ACTION`, `CMSG_PET_ABANDON`) | (b) `HandlePetActionCompat`, `HandlePetAbandonCompat` | 11 |
| reputation flags | (c) `ReputationHasFlagCompat`; includes `ReputationMgr.h`, `CharmInfo.h` | 6 |
| `ObjectMgr::GetPlayer(guid)` | (a) `ObjectAccessor::FindPlayer` (what T's `GetPlayer` calls, ObjectMgr.h:701) | 1 |
| thread of the `ThreatManager` current victim | (b) `ThreatCurrentVictimCompat` | 1 |
| `GetWeaponForAttack` with three arguments | (b) R3 `GetWeaponForAttackCompat` | 4 |

Call-site edits are text-preserving (script `apply1.py` style: the receiver and the name change, the argument text is kept byte for byte,
line counts are unchanged except the 11 pet blocks, which went from 5 lines to 1). Nothing behind an `#if` on the core, nothing stubbed.

## Movement: mapping table, Turtle call -> ForeverCore call (file:line on both)

Every call the bot makes on the unit's MotionMaster, its path generator or a raw spline is one of these helpers
(`forever/src/compat/TbCompat_A.h`, section 5-6; the Turtle side forwards to the old call).

| Helper | Turtle call | ForeverCore call |
|---|---|---|
| `MovePointCompat(mm, id, x, y, z, options)` | `MotionMaster::MovePoint(id, x, y, z, options)`, T MotionMaster.h:137; options `MOVE_PATHFINDING 1 / MOVE_WALK_MODE 2 / MOVE_RUN_MODE 4`, T MotionMaster.h:69-79 | `MovePoint(id, x, y, z, generatePath, finalOrient, speed, speedSelectionMode)`, F MotionMaster.h:174; pathfinding bit -> `generatePath`; walk/run bit -> `ForceWalk`/`ForceRun` (F MovementDefines.h:87) |
| `MoveFollowCompat(mm, target, dist, angle)` | `MoveFollow(target, dist, angle)`, T MotionMaster.h:131, MotionMaster.cpp:425 (clears to the default generator first) | `Clear()` then `MoveFollow(target, dist, ChaseAngle(angle))`, F MotionMaster.h:145,163 (the spike's way, `TbBotMgr.cpp:411-412`) |
| `MoveChaseCompat(mm, target, dist, angle)` | `MoveChase(target, dist, angle)`, T MotionMaster.h:132, MotionMaster.cpp:405 | `MoveChase(target, float, float)`, F MotionMaster.h:166 (`ChaseRange(dist)`, `ChaseAngle(angle)`) |
| `MotionClearCompat(mm, reset, all)` | `Clear(reset, all)`, T MotionMaster.h:112, `DirectClean` MotionMaster.cpp:212: pop to the bottom generator; `all` pops it too | `Clear()` (active slot, F MotionMaster.cpp:425) and, for `all`, `Clear(MOTION_SLOT_DEFAULT)` (F MotionMaster.h:148) |
| `MoveSplineInitCompat init(*unit, name)` with `MovebyPath`, `SetWalk`, `Launch` | `Movement::MoveSplineInit(Unit&, name)`, T MoveSplineInit.h:36,41,55,73 | `MoveSplineInit(Unit*)`, `MovebyPath(span)`, `SetWalk`, `Launch`, F Spline/MoveSplineInit.h:40,51,86,113 (the name is dropped) |
| current generator kind | `GetCurrentMovementGeneratorType()`, T MotionMaster.h:150 | the same call, F MotionMaster.h:130; `CHASE_MOTION_TYPE 5`, `POINT_MOTION_TYPE 8`, `FOLLOW_MOTION_TYPE 14`, `FLIGHT_MOTION_TYPE 7` (`TAXI_MOTION_TYPE` is the shim's name for it) have the same values and names; `EFFECT_MOTION_TYPE` is 15 on T and 16 on F |
| `MoveIdle()`, zero-argument `Clear()` | T MotionMaster.h:112 | the same names, F MotionMaster.h:157,145 (unchanged call sites) |
| `PathCalculateCompat(path, x, y, z, force)` and the two-point form | `PathInfo::calculate`, T PathFinder.h:66-67 | `PathGenerator::CalculatePath`, F PathGenerator.h:86,76 |
| `PathPointsCompat`, `PathTypeCompat`, `PathActualEndCompat`, `PathLengthCompat` | `getPath`, `getPathType`, `getActualEndPosition`, `Length` (T PathFinder.h:81-87, PathFinder.cpp:1011) | `GetPath`, `GetPathType`, `GetActualEndPosition`, `GetPathLength` (F PathGenerator.h:97-102, PathGenerator.cpp:1479; empty path = 0) |
| `LiquidStatusCompat(terrain, x, y, z, mask, &data)` | `TerrainInfo::getLiquidStatus`, T GridMap.h:156 | `TerrainInfo::GetLiquidStatus(phaseShift, mapId, ...)`, F TerrainMgr.h:85 |
| `TerrainIsSwimmableCompat`, `TerrainHeightStaticCompat`, `TerrainWaterOrGroundLevelCompat` | T GridMap.h:152,148,151 | the same questions over F TerrainMgr.h:85,97,104 |
| `IsUnderwaterCompat`, `HasMovementFlagCompat`, `UpdateSpeedCompat`, `PlayerMoverCompat` | `IsUnderwater`, `Player::HasMovementFlag` (Player.cpp:22989), `UpdateSpeed(type, forced)`, `Player::GetMover` | `IsUnderWater` (F Unit.h:1115), `HasUnitMovementFlag` (:1770), `UpdateSpeed(type)` (:1741), `GetUnitBeingMoved` (:1275) |
| `SummonCreatureCompat`, `TeleportToCompat`, `HandleEmoteStateCompat`, `GetNearPointCompat` | T Object.h:1076, Player.h:2057, Unit.h:643, Object.h:841 | F Object.h:422, Player.h:1266, Unit.h:882, Object.cpp:2777 |

The spike already runs `MoveFollow`, `MoveChase`, `MovePoint` live (`forever/src/TbBotMgr.cpp:255,381,403,412`); the helpers use the same F calls.

## Different answers (the two cores answer the same question with different numbers or rules)

1. **Follow and chase angle.** T walks to the exact slot (`angle` from the target's facing); F's `ChaseAngle` keeps the follower within
   +-pi/4 of it (`MovementDefines.h:119-121`) and `ChaseRange` keeps it inside the melee/combat reach. `MoveFollowCompat`, `MoveChaseCompat`.
2. **Follow clears first.** T's `MoveFollow` clears the generators above the default one and refuses while the unit has lost control; F's adds
   to the active slot, so the helper clears (`Clear()`), and a stunned unit does not move on F by the core's own rule.
3. **`Clear(reset, all)`.** The T `reset` (re-initialise the remaining generator) has no F counterpart; `all` also drops F's default slot.
   The zero-argument `Clear()` calls (5 sites) are the same name with the same meaning minus the reset.
4. **Raw splines.** The bot launches its own spline and then sets `MoveIdle()` (T: "a raw spline does not restore the default generator").
   F's MotionMaster does not know a raw spline; whether a later generator cancels it the same way is a live question.
5. **`MovePoint` options.** T's `speed` (0 = unit speed) and final orientation (-10 = none) are never set by the bot and are not carried; the
   generator type and its re-path rules are each core's own. The point id is passed on unchanged (the bot uses the map id as an id).
6. **Path type bits above 0x10.** T 0x20 forced destination, 0x40 fly, 0x80 underwater, 0x100 caster; F 0x20 short, 0x40/0x80 far from the
   polygon at start/end. NORMAL 1, SHORTCUT 2, INCOMPLETE 4, NOPATH 8, NOT_USING_PATH 0x10 are the same.
7. **Liquid.** The legacy type-flag numbers are kept (the module's policy files copy them: MAGMA 1, OCEAN 2, SLIME 4, WATER 8, DARK 0x10);
   F numbers them WATER 1, OCEAN 2, MAGMA 4, SLIME 8. F's liquid read takes a collision height (default 2.03) where T takes none, so the
   status near the surface can differ. `TerrainIsSwimmableCompat` is T's test (`GridMap.cpp:1060`, `JUMP_HEIGHT` 0.6) over F's read.
8. **`MOVEFLAG_JUMPING`.** T has a jump flag (0x2000) and a "fell far" flag (0x4000); F sets FALLING for the whole airborne stretch, so
   JUMPING reads true for a drop too. `MOVEFLAG_FALLINGFAR` is F's FALLING_FAR.
9. **Guid raw value.** `GuidRawCompat` mixes both halves of F's 128-bit guid into 64 bits: equal for equal guids, not the number T prints.
10. **Bounding radius of non-units.** T: display size of a game object; F: the player radius (ObjectDefines.h:39). **Melee reach:** T's own clamp
    (1.5) applied to F's combat reach. **Near point:** T adds both bounding radii, F both combat reaches (and ignores the radius argument).
11. **Item spell trigger 5.** T's `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE` (use without equip cooldown) has no F counterpart (F's 5 is ON_PICKUP); the
    name is a value no F item carries, so every comparison is false.
12. **Spell icon.** `SpellIconIdCompat`: T the icon index, F `IconFileDataId`; only compared between ranks of one spell.
13. **Mana cost.** `SpellPowerCostCompat`: T the one cost of the spell's power type, F the mana entry of its cost list (a spell paid in rage reads 0).
14. **Behind target.** `IsBehindTargetCompat` carries the non-strict form only (`!target->HasInArc(self)`); a strict call asserts.
15. **Reputation flags.** `FACTION_FLAG_INVISIBLE_FORCED` has no F flag (F's 0x08 is Header) and reads false; PEACE_FORCED reads F's Peaceful.
16. **Group scope id.** F uses the group's own guid, T a tag plus the group id; only compared and hashed.
17. **Spell state bound.** `SpellStateLastCompat`: T's `<= SPELL_STATE_DELAYED` is true for every state (DELAYED is the last value, 5); on F the last value
    is IDLE (5), so the answer stays "any state". (The comment next to the code describes a narrower intent; the code never had it.)
18. **Teleport options.** `TeleportToCompat` carries the value 0 only (the three calls it replaces pass 0); a call with options stays on the core call.

## Left open (not stubbed): counts, and what each needs

| Places | Where | Needs |
|---|---|---|
| 33 | MovementActions.cpp 616-747, 981, 1408, 1537, 1553, 3619, 3686-3731 (`MoveOnTransport`, `MoveOffTransport`, `UseTransport`, riding a transport in follow/chase) | the transport model: T `GenericTransport` with `AddPassenger`, `CalculatePassengerPosition/Offset`, `MoveSplineInit::SetTransport`, `m_movementInfo.t_guid`; F `TransportBase` and `MovementInfo::transport`, with the spline converting world to transport coordinates itself (`DisableTransportPathTransformations`). Boarding is out of the slice. |
| 12 | MovementActions.cpp 6240-6299 (`JumpTowards`) | jump by packets (`MSG_MOVE_JUMP`, `MSG_MOVE_KNOCK_BACK`, `MSG_MOVE_START_*`, `SetFallInformation`, `GetAverageDiff`): F's jump is a `MoveJump` generator or `MovementInfo` jump data. The decision is whether bots jump on F at all. |
| 11 | MovementActions.cpp 570-601, 2680 (`UseTaxi`, taxi path walk) | taxi: `Player::GetTaxi`, `TaxiPathEntry::from/to/price`, `ActivateTaxiPathTo`, `CleanupFlagsOnTaxiPathFinished` (see also TaxiRouteGuard.h under NEEDS FROM C) |
| 4 | MovementActions.cpp 1118, 1142-1147 | game-object portals: `GameObjectTemplate::spellcaster`, `GetGameObjectIfCanInteractWith(guid, type)`, the use packet (F `GameObjUse`, `CMSG_GAME_OBJ_USE`) |
| 4 | MovementActions.cpp 2322-2339 | the navmesh-island unstick query (`MMapFactory`, `PathInfo::FindWalkPoly`); F's MMapManager is a singleton keyed by map, instance and phase |
| 2 | MovementActions.cpp 4729, 4739 | `Unit::GetAurasByType` list of `Aura*` with `GetEffIndex` (the spec's aura-list adaptor, F has `AuraEffect` lists) |
| 17 | GenericTriggers.h 1750-1785 | quest triage view over `Quest::GetObjectives()` (already listed in R3); the trigger is not registered |
| 3 | GenericTriggers.h 1385, 1407, 1497 | `Player::GetSpellAuraHolderMap`: an aura-holder adaptor over `GetAppliedAuras()` |
| 4 | GenericTriggers.cpp 280-284 | wand blockers: silence is `UNIT_FLAG_SILENCED` on T and the silence aura (`SPELL_AURA_MOD_SILENCE`) on F, school lockout `IsSpellProhibited` is F `SpellHistory::IsSchoolLocked`; wanding is not in the warrior kit |
| 6 | GenericSpellActions.cpp 1409-1445 and UseItemAction.h 18, 153, 264 (3) | item and totem casts: `Spell` constructor, `SpellCastTargets` setters, `TRIGGERED_OLD_TRIGGERED`, `SetCastItem`, `AddSpellAndCategoryCooldowns` (the spec's `tbhost::NewSpell`) |
| 2 | GenericSpellActions.cpp 973, 976 | heal estimate: `Player::CalculateSpellDamage`, `SpellHealingBonusDone` over a `SpellInfo` (the mirror is a `SpellEntry`) |
| 4 | GenericSpellActions.cpp 767, 1172; PullStrategy.cpp 134, 151 | `GetVisibility`/`VISIBILITY_OFF`; ammo (`PLAYER_AMMO_ID`) and `SKILL_THROWN` do not exist on F |
| 1 | GenericSpellActions.cpp 430 | `CreatureTemplate::school_immune_mask`: F keeps immunities in `CreatureImmunities` rows |
| 8 | GenericActions.cpp 168-653 | pets: `Pet::LearnSpell`, `ToggleAutocast(spellId, ...)`, `m_petSpells`, `COMMAND_DISMISS`, `CallForAllControlledUnits` |
| 2 | GenericActions.cpp 237, 243 | `CharacterDatabase.PQuery` and `QueryResult::Fetch` (pet table read) |
| 2 | GenericActions.h 102, 104 | lightwell: game-object use packet |
| 6 | ChangeStrategyAction.cpp 21, 34, 59, 72, 129, 144 | strategy store: `BeginTransaction(DB_SERIAL_ID)` / `CommitTransaction()` against F's transaction object (needs a decision with `PlayerbotDbStore.cpp`, which is out of the slice) |
| 1 | ReachTargetActions.h 195 | **a blocker for the slice (the file is `ReachMeleeAction`)**: `Creature::GetCombatStartPosition` feeds the evade policy (`ai::EvadeChasePolicy`, `combatStartDistanceYd`). F does not record where a creature began its fight (grep over `Entities/Creature`). Needs a core hook, or a decision to feed the policy from the home distance. No value invented. |

## NEEDS FROM

- **C** (`ai/playerbot/*.h`, no change made here): `TaxiRouteGuard.h:37-46` (8), `GuidPosition.h:13,17,18,20,86` (5), `ServerFacade.h:136,142,268,274,278` (5),
  `RandomBotFacade.h:78-91` (4), `Talentspec.h:5,28` (4), `PlayerbotAI.h:73` (1; P): all already named in `SLICE1-R3-NOTES.md`; they are counted in every
  `.cpp` of this list that includes them. `ItemUsageValue.h:14-16` (4, V).
- **C** (`BotLog`/`SC_LOG`, one place): make the printf-style members take a `std::string` argument the way `CmangosLogProxy` does; then the 66 `CStrCompat`
  wrappers around `GetName()` in this package become unnecessary (harmless meanwhile: identity on Turtle).

## OFFERED (useful to other packages; the lead dedupes at the merge)

`GuidRawCompat`; the path helpers `PathCalculateCompat`, `PathPointsCompat`, `PathTypeCompat`, `PathActualEndCompat`, `PathLengthCompat` and the liquid helpers
(`LiquidStatusCompat`, `GridMapLiquidData`, `MAP_LIQUID_TYPE_*`, `TerrainIsSwimmableCompat`, `TerrainHeightStaticCompat`) for `WorldPosition.cpp` (C); `ThreatCurrentVictimCompat`
for `AttackerCountValues.cpp` and friends (V); `GroupFirstMemberCompat`/`GroupNextMemberCompat`; `HasMovementFlagCompat` and the `MOVEFLAG_*` names;
`SummonCreatureCompat`, `TeleportToCompat`, `IsUnderwaterCompat`, `HasPlayerFlagCompat`, `HasUnitFlagCompat`, `ForcedMovement` and `FORCED_MOVEMENT_*` (mirror of the
Turtle shim); the member macros `GetAuraMaxDuration`, `GetAttackTimer`, `IsRaidGroup`, `GetCastedTime`, `getUnitTargetGuid`, `GetMountID`, `SetTargetGuid`;
`UNIT_STAT_MELEE_ATTACKING`; `HandlePetActionCompat`. Two includes now live in `TbCompat_A.h` for everybody: `VMapManager.h` (also gives `VMAP_INVALID_HEIGHT_VALUE`) and
`PetPackets.h`, `ReputationMgr.h`, `CharmInfo.h`.

## Gates

(filled in at the end of the file by the gate run)

## What I am unsure about

- The follow and chase helpers change nothing on Turtle, but on F they are the first use of `ChaseAngle` tolerance and of `Clear()` before `MoveFollow` in the engine path; the spike
  used the same calls, the engine's re-issue rate (the bot calls `MoveFollow` every few ticks) is a live question.
- Raw splines plus `MoveIdle()` on F (Different answers 4).
- `ITEM_SPELLTRIGGER_ON_NO_DELAY_USE = 0xFFFFFFFF` is a sentinel, not a core value; it is there so comparisons stay false.
- The Turtle side was written against the reference tree by reading; it compiles only on the gate (`vm-build`).
- The 11 pet blocks changed from a built packet to a helper call; the Turtle helper builds the same three fields in the same order
  (a `uint64(0)` target became an empty `ObjectGuid`, which T streams as the same 8 zero bytes).
