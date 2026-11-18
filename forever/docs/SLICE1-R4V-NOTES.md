# ForeverCore port, slice 1, round R4V: the values (58 files, `strategy/values/`)

Package R4V owns the 58 `.cpp` files of `forever/SLICE1-R4V.txt`, the headers beside them, `forever/src/compat/TbCompat_V.h` and its
Turtle twin `ai/cmangos-compat-r4v.h`. T = the Turtle core (`~/playerbots-references/penqle-tortoise-wow-modsys/src/game`; the
copy there is older than the build image, so a few Turtle members cited below, for example `Group::GetTargetIcon`,
`DynamicObject::IsAffecting`, `Player::GetHomeBindLocation`, are only known from the bot's own calls), F = the pinned ForeverCore
(`~/ForeverCore/src/server/game`).

## Numbers

Census: `FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE1-R4V.txt forever/tools/vm-census.sh "" 14`.

| | places in files R4V owns | whole list (58 files and every header they pull) |
|---|---|---|
| BEFORE (`9b32de181`) | 220 | 250 in 39 files |
| AFTER (this branch) | 5 | 35 in 11 files (30 sit in headers owned by the other packages) |

Families closed (places in the baseline): unit flags and update fields 33, guid as a 64-bit key / as a bool / as text 30, pets, items,
creatures and other object members 28, spells in flight and spell calls 23, threat and hostility 20, grid searches 17, player lookup 16,
line of sight, distance, height 15, duel 12, sight 9, auras 6, left open 5.
By method: (a) a name line or include, 24 places (the macros and the enum at the top of `TbCompat_V.h`, the two includes); (b) and (c) a
both-core helper, 191 places, of which the ones where the cores answer differently are the 17 entries of "Different answers"; (d) left open 5.
Helpers that R3 already had were used, not copied: `IsWithinLOSInMapCompat` (9 places), `GetDistanceCompat` (6), `GetHeightCompat`,
`GetLosHitPositionCompat`, `GetAngleCompat`, `GetCombatReachCompat`, `UnitMin/MaxDamageCompat`, `GameObjectTrapSpellIdCompat`,
`CStrCompat`, `AreaHasFlagsCompat`, `GetAreaEntryByAreaID`.

## The helpers, the two definitions side by side

All in `TbCompat_V.h` (F) and `cmangos-compat-r4v.h` (T), same name and signature. The T body is the call the bot always made.

### Grid searches (one helper per kind of search)
| helper | T | F |
|---|---|---|
| `VisitUnitsCompat(center, list, check, range)` | `MaNGOS::UnitListSearcher<C> s(list, check); Cell::VisitAllObjects(center, s, range)` | `Trinity::UnitListSearcher<C> s(center, list, check)` (the object gives the phase); `Cell::VisitAllObjects(center, s, range)` |
| `VisitUnitsAtCompat(x, y, map, list, check, range)` | `Cell::VisitAllObjects(x, y, map, s, range, true)` (`dont_load`) | searcher built on `PhasingHandler::GetEmptyPhaseShift()`; `VisitAllObjects(x, y, map, s, range)` (loaded cells only, no flag) |
| `VisitGameObjectsCompat`, `VisitGameObjectsAtCompat` | the same with `GameObjectListSearcher` | the same |
| `VisitGridObjectsCompat`, `VisitGridObjectsAtCompat` | `WorldObjectListSearcher` + `Cell::VisitGridObjects` (dynamic objects live in the grid containers only) | the same |
| `VisitFriendlyUnitsCompat(unit, list, range)` | `AnyFriendlyUnitInObjectRangeCheck(unit, range)` | `AnyFriendlyUnitInObjectRangeCheck(unit, unit, range, false, false, false)` |

Call sites: `CollisionValue`, `NearestNpcsValue`, `NearestFriendlyPlayersValue`, `RaidTargetValues`, `PossibleTargetsValue` (3),
`NearestGameObjects` (6). The check classes the bot writes itself (`TrapsNearPointCheck`, `DynObjsNearPointCheck`,
`ListedKeepoutNearPoint`, `DynamicObjectsInRangeCheck`, `AnyGameObjectInObjectRangeCheck`) are unchanged: they only need
`operator()(Unit*/GameObject*/WorldObject*)`.

### Auras (what callers ask, not a container)
| helper | T | F |
|---|---|---|
| `ForEachAuraOfTypeCompat(unit, type, fn)`: fn gets `{spell, effIndex, caster}` and returns true to stop | `GetAurasByType(type)`: one `Aura` per effect; `GetSpellProto`, `GetEffIndex`, `GetCaster` | `GetAuraEffectsByType(type)` (Unit.h:1366): one `AuraEffect` per effect; `GetSpellInfo` (as the mirror entry), `GetEffIndex`, `GetCaster` |
| `AuraIsPositiveCompat(aura)` | `Aura::IsPositive()` | `aura->GetSpellInfo()->IsPositive()` (the `Aura` has no flag, the application has) |
| `IsSilencedCompat(unit)` | `HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED)` | `IsSilenced(SPELL_SCHOOL_MASK_ALL)` (Unit.h:1524) |
| `UnitHasFlagCompat(unit, flags)` | `HasFlag(UNIT_FIELD_FLAGS, flags)` | `HasUnitFlag(UnitFlags(flags))` |
| `UnitTargetGuidCompat(unit)` | `GetGuidValue(UNIT_FIELD_TARGET)` | `GetTarget()` |
| `DynamicObjectEffectIndexCompat(dyn)`, `DynamicObjectAffectsCompat(dyn, unit)` | `GetEffIndex()`, `IsAffecting(unit)` | the first `SPELL_EFFECT_PERSISTENT_AREA_AURA` effect of the dynamic object's spell (else 0); `unit->HasAura(spellId, casterGuid)` |

"Has aura X / stacks / remaining time" stay on `PlayerbotAI::HasAura / GetAura` (package P); the values only use `Aura::GetStackAmount`,
`GetAuraDuration` (R3 macro to `GetDuration`) and `GetAuraMaxDuration` (new macro to `GetMaxDuration`, F SpellAuras.h:217).

### Threat and hostility (who threatens me)
| helper | T | F |
|---|---|---|
| `CurrentThreatVictimCompat(unit)` | `ref = GetThreatManager().getCurrentVictim(); ref ? ref->getTarget() : nullptr` | `GetThreatManager().GetLastVictim()` |
| `ForEachThreatenerCompat(me, fn)`: fn(attacker) true to stop; returns whether anyone had `me` on a list | walk `me->GetHostileRefManager().getFirst()` .. `next()`, `GetSource()->getOwner()` | `IsThreateningAnyone(true)`, then `GetThreatenedByMeList()` entries' `GetOwner()` |
| `CreatureRankCompat(creature)` | `GetCreatureInfo()->rank` | boss type flag of the difficulty (`isWorldBoss`) is 3, else the classification mapped to 0/1/2/4; `CreatureEliteType` enum added for F |
| `CreatureTauntImmuneCompat` | `HasImmunityFlag(CREATURE_IMMUNITY_TAUNT)` | template `flags_extra & CREATURE_FLAG_EXTRA_NO_TAUNT` |
| `CreatureHasLootRecipientCompat`, `CreatureIsTappedByCompat` | `HasLootRecipient()`, `IsTappedBy(p)` | `hasLootRecipient()`, `isTappedBy(p)` |

`HasAggroValue` (`AttackerCountValues.cpp`) is rewritten on `ForEachThreatenerCompat` + `CurrentThreatVictimCompat` with the same
control flow as before (units that threaten the bot, then the target's current victim only if the bot had any). R3's open item
`ServerFacade.h:136,142` (`HostileRefManager`, 2 places) is no longer needed by any file of this package; its other users
(`TellTargetAction.cpp:42,76`) can use `ForEachThreatenerCompat`. See NEEDS FROM.

### Guids, players, groups
| helper | T | F |
|---|---|---|
| `GetPlayerCompat(guid)` | `sObjectMgr.GetPlayer(guid)` (= `ObjectAccessor::FindPlayer`, ObjectMgr.h:701) | `ObjectAccessor::FindPlayer(guid)` (ObjectAccessor.h:82) |
| `GuidKeyCompat(guid)` | `GetRawValue()` | `std::hash<ObjectGuid>` (FNV-1a over 16 bytes, ObjectGuid.h:441), 0 when empty |
| `DuelOpponentGuidCompat(player)` | `m_duel ? m_duel->opponent : ObjectGuid()` | `duel ? duel->Opponent->GetGUID() : Empty` |
| `TargetIconGuidCompat(group, icon)` | `ObjectGuid(group->GetTargetIcon(icon))` | `group->GetTargetIcon(icon)` |
| `GroupMainTankGuidCompat`, `GroupMainAssistGuidCompat` | `GetMainTankGuid()`, `GetMainAssistantGuid()` | the first member slot with `MEMBER_FLAG_MAINTANK / MAINASSIST` |
| `GuidTextCompat(guid)` | `ostringstream << guid` | `guid.ToString()` |
| `ShuffleCompat(b, e)` | `std::shuffle(b, e, *GetRandomGenerator())` | `Trinity::Containers::RandomShuffle(b, e)` |
| `ComboTargetGuidCompat`, `ComboPointsCompat` | `GetComboTargetGuid()`, `GetComboPoints()` | `GetClassicComboTarget()`, `GetPower(POWER_COMBO_POINTS)` |

### Spells in flight, sight, pets, items, paths
| helper | T | F |
|---|---|---|
| `SpellUnitTargetGuidCompat`, `SpellCorpseTargetGuidCompat` | `m_targets.getUnitTargetGuid()`, `getCorpseTargetGuid()` | `GetUnitTargetGUID()`, `GetCorpseTargetGUID()` (SpellDefines.h:385,393) |
| `SpellDestinationCompat(spell, x, y, z)` | `m_targetMask & TARGET_FLAG_DEST_LOCATION` + `getDestination` | `HasDst()` + `GetDstPos()` |
| `SpellIsPreparingCompat` | `getState() == SPELL_STATE_PREPARING` | same |
| `SpellIsInFlightOrChannelingCompat` | `SPELL_STATE_CASTING` or `CHANNELING` | `SPELL_STATE_LAUNCHED` or `CHANNELING` |
| `SpellEffectAmountCompat(spell, i, target)` | `CalculateDamage(SpellEffectIndex(i), target)` | `CalculateDamage(spellInfo->GetEffect(i), target)` |
| `SpellPowerCostCompat(entry, caster)` | `Spell::CalculatePowerCost(entry, caster, nullptr, nullptr)` | `SpellInfo::CalcPowerCost(Powers(entry->powerType), false, caster, schoolMask)` |
| `IsHighRankOfSpellCompat(a, b)` | `sSpellMgr.IsHighRankOfSpell(a, b)` | `SpellInfo::IsHighRankOf` |
| `ItemFitsSpellCompat(item, entry)` | `item->IsFitToSpellRequirements(entry)` | the same with `GetSpellInfoCompat(entry)` |
| `CanSeeOrDetectCompat(viewer, unit)` | `IsVisibleForOrDetect(viewer, camera body, true)` | `viewer->CanSeeOrDetect(unit, {DistanceCheck = true})` |
| `IsVisibleForCompat(target, viewer)` | `target->isVisibleFor(viewer, viewer)` | `viewer->CanSeeOrDetect(target)` |
| `IsHiddenGmCompat(unit)` | `GetVisibility() == VISIBILITY_OFF` | `!IsVisible()` (Unit.cpp:9124) |
| `PetSpellIdsCompat(pet)` | `m_petSpells` minus `PETSPELL_REMOVED` | `m_spells` minus `PETSPELL_REMOVED` |
| `PetIsMiniPetCompat`, `PetIsHappyCompat` | `getPetType() == MINI_PET`, `GetHappinessState() == HAPPY` | false (no such pet type), `== Pet::HAPPY` |
| `ForEachControlledUnitCompat(unit, fn)` | `CallForAllControlledUnits(fn, PET\|GUARDIANS\|CHARM\|MINIPET\|TOTEMS)` | each of `m_Controlled` |
| `ItemDurabilityCompat`, `ItemMaxDurabilityCompat` | `GetUInt32Value(ITEM_FIELD_[MAX]DURABILITY)` | `*m_itemData->[Max]Durability` |
| `HomeBindLocationCompat(player)` | `GetHomeBindLocation()` | `m_homebind` |
| `GetCombatDistanceCompat(a, b)` | `GetCombatDistance(b)` | `GetDistanceCompat(a, b, SizeFactor::CombatReach)` |
| `IsWithinLOSPointCompat(from, x, y, z, dyn)` | `IsWithinLOS(x, y, z, dyn)` | `IsWithinLOS(x, y, z, ALL or VMAP checks)` |
| `PathCalculateCompat`, `PathIsCompleteCompat`, `PathPointsCompat` | `calculate`, `getPathType() == PATHFIND_NORMAL`, `getPath` | `CalculatePath`, `GetPathType() == PATHFIND_NORMAL`, `GetPath` |

Names (a): `GetPetGuid` -> `GetPetGUID`, `GetCreatorGuid` -> `GetCreatorGUID`, `IsSwimming()` -> the swimming movement flag (T Object.h:977),
`GetAuraMaxDuration` -> `GetMaxDuration`, `UNIT_STAT_MELEE_ATTACKING`, `UNIT_STAT_CAN_NOT_REACT` (T: STUNNED | FEIGN_DEATH | CONFUSED |
FLEEING, the union R3 used), `UNIT_FLAG_UNTARGETABLE` (= `UNIT_FLAG_UNINTERACTIBLE`, 0x02000000, as the Turtle shim aliases it),
`CreatureEliteType`, and the includes `TradeData.h` and `Containers.h`. No bot class defines a member of any of those names (grep).

Other edits in the files: `ObjectGuid` truth tests became `!guid.IsEmpty()` (same on T), `AreaEntry::GetById` -> `GetAreaEntryByAreaID`,
the CREATURE-name switch in `AttackerCountValues` is on `CreatureRankCompat`, `GetName()` printed with `%s` goes through `CStrCompat`.

## Different answers (the live test must know where behaviour can differ)

1. **Unit search ranges.** T `AnyUnitInObjectRangeCheck` / `AnyFriendlyUnitInObjectRangeCheck` / `AnyUnfriendlyUnitInObjectRangeCheck`
   (Maps/GridNotifiers.h:865-924): alive, `IsWithinDistInMap(u, range)` (3-D distance, same map), friendliness, and
   `CanSeeInWorld` (the other party can see this one). F (GridNotifiers.h:1190-1340): alive, `IsWithinDist` (3-D) for the any/unfriendly
   checks, no sight test at all; the friendly check is a vertical cylinder of the range (own and target combat reach turned off by the
   helper), same map and phase. So F lists a stealthed or invisible unit T would leave out, and the friendly search is a cylinder, not a sphere.
   Callers that need the sight test already ask `CanSeeOrDetectCompat` afterwards (`PossibleTargetsValue::IsValid`,
   `CurrentTargetValue`); the plain "nearest npcs / friendly players / collision" lists do not.
2. **`dont_load`.** T point searches pass `dont_load = true` (do not load grids); F visits only loaded cells.
3. **`GuidKeyCompat`.** T: the 64-bit guid; F: a 64-bit hash of the 128-bit guid. Equal guid, equal key, inside one process; never
   reversible; a collision is possible in theory (2^-64). Anything that turns a key back into an `ObjectGuid` (`ObjectGuid(uint64)`
   in `BotActionLog.cpp`, `BossTacticsActions`) cannot be closed this way: those are package A/C/P's.
4. **Aura positivity.** T per aura effect, F per spell (`SpellInfo::IsPositive`). A spell with a harmful and a beneficial effect is
   one answer on F (`PossibleAttackTargetsValue::IsImmuneToDamage`, `PossibleAttackTargetsValue.cpp:218`, reads the mechanic first and the positivity second, so only a spell with both kinds of effect can differ).
5. **Silence.** T: the silenced unit flag; F: any school silenced (`SilencedSchoolMask != 0`). A single-school lock-out counts on F.
6. **Dynamic objects.** T knows the effect index and the exact set of units the object affects. F knows neither: the effect index is
   the spell's persistent-area-aura effect, "affects" is "the unit carries the spell from that caster", so a unit just inside the radius
   that has not been ticked yet is not counted, and a spell of several area effects uses the first one's positivity.
7. **Current threat victim.** T reads the stored reference (it may point at an offline entry); F `GetLastVictim` returns null when the
   stored reference should be offline. `GetCurrentVictim` was not used: it re-selects and writes to the creature's list (and asserts).
8. **Threatened-by list.** T's `HostileRefManager` holds every reference including offline ones; F's `GetThreatenedByMeList` is a
   hash map (the order differs; `HasAggroValue` only asks "any"), asked with `includeOffline` so the emptiness answer is T's.
9. **Rank.** T `rank` 0..4 from `creature_template`; F has `Classification` (0 normal, 1 elite, 2 rare elite, 4 rare, plus obsolete,
   trivial, minus) and a boss flag. World boss on F is the boss type flag of the creature's difficulty, T's is rank 3; which creatures carry each depends on each
   core's data, so a dungeon boss can be x5 on one core and x3 on the other. `AttackerCountValues` weights by it (x5 world boss, x3
   elite and rare elite, x2 rare); `RaidTargetValues` asks only for the world boss.
10. **Taunt immunity.** T: template immunity flag; F: `CREATURE_FLAG_EXTRA_NO_TAUNT` of the template. A boss that is only immune to the
    taunt mechanic through an aura or a spell script is not covered by either.
11. **Tapping.** T `HasLootRecipient` (a lootgroup or a lone recipient); F `hasLootRecipient` (non-empty tap list). `IsTappedBy`:
    both say "tapped by the player or a party member".
12. **Sight.** T `IsVisibleForOrDetect(viewer, camera body, detect)` (stealth by distance, GM, phase-less); F `CanSeeOrDetect` with the
    distance check on (also phase, private objects, spawn tracking, death recap). `IsVisibleForCompat` is F `CanSeeOrDetect` without the
    distance check on the same core. Hidden GM: T `VISIBILITY_OFF`, F the GM server-side visibility.
13. **Cast state.** T `SPELL_STATE_CASTING` (cast time over, the spell's delay running) against F `SPELL_STATE_LAUNCHED` (missile in
    flight). A spell with no travel time leaves that state at once on both. Used by `SpellCastUsefulValue` to avoid recasting the spell just sent.
14. **Power cost.** T `Spell::CalculatePowerCost` (the spell's own cost, no mods, for the spell's `powerType`); F `CalcPowerCost`
    takes the caster and applies its own cost formula (percent cost auras, free-cast states). Used only to tell "mana is short"; the numbers can differ by those mods.
15. **Mini pet.** F has no pet type for a vanity companion; the helper answers false (a companion is a creature there, never a `Pet`),
    so `PartyMemberWithoutAura` and `PartyMemberToHeal::CanHealPet` treat every F `Pet` as a real pet.
16. **Combo points.** T tracks target guid and count on the player; F (classic combo target + the combo power). The helper reads
    them; whether ForeverCore's combo power is the plain count (as T's `GetComboPoints`) is not verified: the live test has to show it.
17. **Main tank / main assist.** T keeps one guid each (the raid sets it); F keeps member flags; the first flagged slot wins. They agree
    unless F lets two members carry the flag (`SetGroupMemberFlag` removes the previous holder, Group.cpp:1775).

## NEEDS FROM other packages
- **C** (`ServerFacade.h:136,142`): delete `HostileRefManager& GetHostileRefManager(Unit*)` once `TellTargetAction.cpp` (A) is moved to
  `ForEachThreatenerCompat`; no file of R4V calls it any more. `ServerFacade::GetThreatManager` is fine on F (`Unit::GetThreatManager`).
- **A** (`TellTargetAction.cpp:42,76`): use `ForEachThreatenerCompat`.
- **P** (`PlayerbotAI::GetAura`, `GetAuras`, `HasAura`): the values call `ai->GetAura(...)->GetStackAmount()/GetAuraDuration()/
  GetAuraMaxDuration()` and read `GetSpellProto()` of `ai->GetAuras(...)` entries. They compile as long as the F return type is the core's
  `Aura*` (one per spell). If P returns `AuraEffect*`, `DpsTargetValue.cpp:36`, `TankTargetValue.cpp:65`,
  `PartyMemberWithoutAuraValue.cpp:42-59` and `PossibleAttackTargetsValue.cpp:218` need the helper `AuraIsPositiveCompat` re-typed.
- **A/C** (`BossTacticsPolicy.h`, `SeekAuraPolicy.h`, `BossTacticsActions.cpp`, `TankPostLatchedUnit`): these key by `uint64`; every
  call that fills one from an `ObjectGuid` must use `GuidKeyCompat` or the policy and the values disagree about which unit a key means.

## OFFERED (other packages may use; the lead dedupes at the merge)
`GetPlayerCompat` (126 uses of `sObjectMgr.GetPlayer` in the tree), `GuidKeyCompat`, `GuidTextCompat`, `DuelOpponentGuidCompat`,
`TargetIconGuidCompat`, `GroupMainTankGuidCompat`, `GroupMainAssistGuidCompat`, `ShuffleCompat`, the `Visit*Compat` searches, `ForEachAuraOfTypeCompat`,
`ForEachThreatenerCompat`, `CurrentThreatVictimCompat`, `CreatureRankCompat`, `CanSeeOrDetectCompat`, `IsVisibleForCompat`, `IsHiddenGmCompat`,
`IsWithinLOSPointCompat`, `PathCalculateCompat` / `PathIsCompleteCompat` / `PathPointsCompat` (`WorldPosition.cpp` and `AttackersValue.cpp` ask
the same of `PathFinder`), `HomeBindLocationCompat` (`TravelMgr.cpp:40`, `UnstuckAction.cpp:31,76`), `ComboTargetGuidCompat`,
`SpellUnitTargetGuidCompat` and the other spell-in-flight helpers, the names block.

## Left open (d)
- `TargetValue.cpp:361`: `Creature::GetCombatTime()` (seconds since the creature entered combat). ForeverCore does not record it
  (`Creature::AtEngage` is the only hook, Creature.h:463). It feeds `CcMarkHoldPolicy.h` (a CC mark is held for a few seconds after
  the pull); that header already has `combatSeconds = -1` for "unknown". Needs: a core hook that stamps the engage time, or the
  linking round's decision to feed -1. Not a follow / assist / melee path.
- `ItemUsageValue.h:14-16` (4 places): `Item::GetItemRandomPropertyId`, `LootItem::randomPropertyId`, `AuctionEntry::itemTemplate`. Loot and
  auction records; ForeverCore has random bonus lists instead of random properties. Pulled in only through the headers of removed values.
  Needs: the linking round either keeps the three constructors out of `ItemQualifier` or maps the random property to a bonus list id.

## Gates
(see the end of this file when filled in)
