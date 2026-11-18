// ForeverCore side of the both-core helpers and names of slice 1, round R4V (values).
// Owned by package R4V: no other package edits this file. Turtle side: ai/cmangos-compat-r4v.h (same names and signatures,
// each forwarding to the call the bot always made). The notes forever/docs/SLICE1-R4V-NOTES.md hold the two definitions side
// by side and the list of places where the cores answer differently.
//
// T = the Turtle core ~/playerbots-references/penqle-tortoise-wow-modsys/src/game, F = the pinned ForeverCore src/server/game.
#pragma once

#include "Containers.h"   // Trinity::Containers::RandomShuffle
#include "TradeData.h"   // TRADE_SLOT_NONTRADED and TradeData::GetItem (F declares them in a header the botpch does not pull)

// ---------------------------------------------------------------------------------------------------------------------
// Names (a): one line, the same meaning on both cores.
// ---------------------------------------------------------------------------------------------------------------------
// The guid of a unit's pet / of the unit that created it. T Unit.h:1365 (GetCreatorGuid), F Unit.h:1215,1219.
#define GetPetGuid GetPetGUID
#define GetCreatorGuid GetCreatorGUID
// "Is this unit swimming": T Object.h:977 reads the swimming movement flag, the same flag here (cf. IsRooted in TbCompat_Names.h).
#define IsSwimming() m_movementInfo.HasMovementFlag(MOVEMENTFLAG_SWIMMING)
// "How long does this aura last at most, in ms (-1 never)": T SpellAuras.h (Aura asks its holder), F SpellAuras.h:217.
#define GetAuraMaxDuration GetMaxDuration
// "Unit state: the unit is in a melee swing" T UNIT_STAT_MELEE_ATTACKING, F Unit.h UNIT_STATE_MELEE_ATTACKING.
#define UNIT_STAT_MELEE_ATTACKING UNIT_STATE_MELEE_ATTACKING
// T UNIT_STAT_CAN_NOT_REACT = STUNNED | FEIGN_DEATH | CONFUSED | FLEEING (UnitDefines.h); the same union of the F bits
// TbCompat_Names.h maps (FEIGN_DEATH is F's DIED).
#define UNIT_STAT_CAN_NOT_REACT (UNIT_STATE_STUNNED | UNIT_STATE_DIED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING)
// T's untargetable flag is the "not selectable" bit (cmangos-compat-shim.h:1013 aliases it), F's is UNIT_FLAG_UNINTERACTIBLE
// (UnitDefines.h:191, 0x02000000), the same bit forever-compat-shim.h:254 already names UNIT_FLAG_NOT_SELECTABLE.
#ifndef UNIT_FLAG_UNTARGETABLE
#define UNIT_FLAG_UNTARGETABLE UNIT_FLAG_UNINTERACTIBLE
#endif

// The kinds of creature rank the bot sorts by. T SharedDefines.h:1020-1027 (CreatureEliteType); F has no such enum
// (CreatureClassifications, a different numbering and no world boss), CreatureRankCompat below returns these numbers.
enum CreatureEliteType
{
    CREATURE_ELITE_NORMAL    = 0,
    CREATURE_ELITE_ELITE     = 1,
    CREATURE_ELITE_RAREELITE = 2,
    CREATURE_ELITE_WORLDBOSS = 3,
    CREATURE_ELITE_RARE      = 4,
    CREATURE_UNKNOWN         = 5
};

// ---------------------------------------------------------------------------------------------------------------------
// Players, guids, groups
// ---------------------------------------------------------------------------------------------------------------------
// A player that is in the world, by guid. T ObjectMgr.h:701 `ObjectMgr::GetPlayer(guid)` is `ObjectAccessor::FindPlayer`,
// F ObjectAccessor.h:82 `FindPlayer(guid)`. Offered to the other packages (126 uses in the tree).
inline Player* GetPlayerCompat(ObjectGuid const& guid) { return ObjectAccessor::FindPlayer(guid); }

// A 64-bit key of a guid, for the maps and policy functions the bot keys by `uint64`. T: GetRawValue() (the whole guid is 64
// bits). F: the guid is 128 bits (ObjectGuid.h:325), there is no raw 64-bit value; the key is the hash the core's own
// std::hash<ObjectGuid> gives (FNV-1a over the 16 bytes, ObjectGuid.h:441), 0 for the empty guid. Same guid, same key, in this
// process only. NOT reversible: nothing may rebuild an ObjectGuid from it. Offered: every package that keys by raw guid must
// use this one name, or two packages will disagree about the key of one unit.
inline uint64 GuidKeyCompat(ObjectGuid const& guid)
{
    return guid.IsEmpty() ? 0 : uint64(std::hash<ObjectGuid>()(guid));
}

// The guid of the player a player is dueling now, empty when there is no duel. T Player.h:2806 `m_duel->opponent` (an
// ObjectGuid), F Player.h:462-472 `duel->Opponent` (a Player*).
inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->duel ? player->duel->Opponent->GetGUID() : ObjectGuid::Empty;
}

// The guid on a raid target icon slot (0..7), empty when the slot is unset. T Group.h returns the raw uint64 of the guid, F Group.h:362
// returns the ObjectGuid.
inline ObjectGuid TargetIconGuidCompat(Group const* group, uint8 icon) { return group->GetTargetIcon(icon); }

// The raid's Main Tank / Main Assist. T Group.h:284-285 keeps one guid each; F keeps them as member flags (Group.h:76-78,
// MemberSlot::flags), set through SetGroupMemberFlag which removes the flag from the previous holder (Group.cpp:1775), so
// the first slot that carries the flag is the one holder. Empty when nobody holds it.
inline ObjectGuid GroupFlagHolderCompat(Group const* group, uint8 flag)
{
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.flags & flag)
            return slot.guid;
    return ObjectGuid::Empty;
}
inline ObjectGuid GroupMainTankGuidCompat(Group const* group) { return GroupFlagHolderCompat(group, MEMBER_FLAG_MAINTANK); }
inline ObjectGuid GroupMainAssistGuidCompat(Group const* group) { return GroupFlagHolderCompat(group, MEMBER_FLAG_MAINASSIST); }

// A guid as text. T prints through its own ostream operator, F's guid has none (ObjectGuid.h:397 ToString).
inline std::string GuidTextCompat(ObjectGuid const& guid) { return guid.ToString(); }

// Shuffle a range with the core's random source. T has none of its own: the bot's shim keeps a PRNG (cmangos-compat-shim.h:982),
// F Containers.h:171.
template <class Iterator>
inline void ShuffleCompat(Iterator begin, Iterator end) { Trinity::Containers::RandomShuffle(begin, end); }

// ---------------------------------------------------------------------------------------------------------------------
// Grid searches: one helper per kind of search. T's searchers take (container, check), F's also the object the search sees
// from (its phase, UnitListSearcher in GridNotifiers.h:561) or, for a point, the phase shift; T's Cell::Visit* take a
// `dont_load` flag F has no use for (F visits the loaded cells). The Check classes are the core's own or the bot's own
// (a callable taking Unit*, GameObject* or WorldObject*), unchanged.
// ---------------------------------------------------------------------------------------------------------------------
// Every unit around an object, players and creatures (T Cell::VisitAllObjects, F Cell.h:101).
template <class Check>
inline void VisitUnitsCompat(WorldObject const* center, std::list<Unit*>& out, Check& check, float range)
{
    Trinity::UnitListSearcher<Check> searcher(center, out, check);
    Cell::VisitAllObjects(center, searcher, range);
}
// The same around a point of a map (T VisitAllObjects(x, y, map, ..., dont_load = true)).
template <class Check>
inline void VisitUnitsAtCompat(float x, float y, Map* map, std::list<Unit*>& out, Check& check, float range)
{
    Trinity::UnitListSearcher<Check> searcher(PhasingHandler::GetEmptyPhaseShift(), out, check);
    Cell::VisitAllObjects(x, y, map, searcher, range);
}
// Every game object around an object / a point.
template <class Check>
inline void VisitGameObjectsCompat(WorldObject const* center, std::list<GameObject*>& out, Check& check, float range)
{
    Trinity::GameObjectListSearcher<Check> searcher(center, out, check);
    Cell::VisitAllObjects(center, searcher, range);
}
template <class Check>
inline void VisitGameObjectsAtCompat(float x, float y, Map* map, std::list<GameObject*>& out, Check& check, float range)
{
    Trinity::GameObjectListSearcher<Check> searcher(PhasingHandler::GetEmptyPhaseShift(), out, check);
    Cell::VisitAllObjects(x, y, map, searcher, range);
}
// World objects of the grid containers only (dynamic objects live there; T Cell::VisitGridObjects).
template <class Check>
inline void VisitGridObjectsCompat(WorldObject const* center, std::list<WorldObject*>& out, Check& check, float range)
{
    Trinity::WorldObjectListSearcher<Check> searcher(center, out, check);
    Cell::VisitGridObjects(center, searcher, range);
}
template <class Check>
inline void VisitGridObjectsAtCompat(float x, float y, Map* map, std::list<WorldObject*>& out, Check& check, float range)
{
    Trinity::WorldObjectListSearcher<Check> searcher(PhasingHandler::GetEmptyPhaseShift(), out, check);
    Cell::VisitGridObjects(x, y, map, searcher, range);
}
// The living units around `center` that are friendly to it, the check being the core's AnyFriendlyUnitInObjectRangeCheck. T takes
// (center, range); F takes (center, friendly-to unit, range, playerOnly, includeOwnRadius, includeTargetRadius) and adds the
// combat reaches to the range unless told not to: asked here not to, so the range is the plain one T uses.
inline void VisitFriendlyUnitsCompat(Unit const* center, std::list<Unit*>& out, float range)
{
    Trinity::AnyFriendlyUnitInObjectRangeCheck check(center, center, range, false, false, false);
    VisitUnitsCompat(center, out, check, range);
}

// ---------------------------------------------------------------------------------------------------------------------
// Auras: what callers ask (has it, how long, who cast it), not a container.
// ---------------------------------------------------------------------------------------------------------------------
// One aura effect of a type, as the loops over `GetAurasByType` read it. T: one Aura per effect (Unit.h GetAurasByType, the list
// of Aura*), F: one AuraEffect per effect of an Aura (Unit.h:1366 GetAuraEffectsByType, AuraEffect::GetBase/GetEffIndex).
struct AuraOfTypeCompat
{
    SpellEntry const* spell;   // the spell, mirror entry on F (null when the core has no such spell)
    uint32 effIndex;           // the effect of the spell this aura carries
    Unit* caster;              // the unit that cast it, null when gone
};
// Calls fn(AuraOfTypeCompat const&) for each aura of that type on the unit; fn returns true to stop. True when it stopped.
template <class Fn>
inline bool ForEachAuraOfTypeCompat(Unit const* unit, AuraType type, Fn&& fn)
{
    for (AuraEffect const* effect : unit->GetAuraEffectsByType(type))
    {
        if (!effect)
            continue;
        AuraOfTypeCompat const view{ GetSpellEntryCompat(effect->GetSpellInfo()), effect->GetEffIndex(), effect->GetCaster() };
        if (fn(view))
            return true;
    }
    return false;
}
// Is the aura a beneficial one. T Aura::IsPositive is the per-effect flag, F's Aura has none (the AuraApplication has it, per
// target): the spell's own classification, SpellInfo::IsPositive (SpellInfo.h:520).
inline bool AuraIsPositiveCompat(Aura const* aura) { return aura->GetSpellInfo()->IsPositive(); }

// Is the unit silenced (any school). T: the unit flag, F: the silenced school mask (Unit.h:1524).
inline bool IsSilencedCompat(Unit const* unit) { return unit->IsSilenced(SPELL_SCHOOL_MASK_ALL); }
// Does the unit carry one of the unit flags (the bits UNIT_FLAG_NOT_ATTACKABLE_1, UNTARGETABLE/NOT_SELECTABLE, PACIFIED have the same
// value on both cores). T `HasFlag(UNIT_FIELD_FLAGS, flag)`, F Unit.h:862 HasUnitFlag.
inline bool UnitHasFlagCompat(Unit const* unit, uint32 flags) { return unit->HasUnitFlag(UnitFlags(flags)); }
// The guid the unit has selected / is aiming at. T `GetGuidValue(UNIT_FIELD_TARGET)`, F Unit.h GetTarget (same field).
inline ObjectGuid UnitTargetGuidCompat(Unit const* unit) { return unit->GetTarget(); }

// ---------------------------------------------------------------------------------------------------------------------
// Threat and hostility: who threatens me, who do I threaten.
// ---------------------------------------------------------------------------------------------------------------------
// The unit the creature's threat list has picked as its target (null when the list has none). T ThreatManager.h:206 returns the
// stored HostileReference (getTarget() is the unit), a plain read. F's GetCurrentVictim (ThreatManager.cpp:245) re-selects the
// victim when the stored one is gone, which writes to the creature's threat list from the engine's thread; GetLastVictim
// (ThreatManager.cpp:253) is the same stored reference read without that write (null when it should be offline).
inline Unit* CurrentThreatVictimCompat(Unit* unit) { return unit->GetThreatManager().GetLastVictim(); }
// Calls fn(Unit* attacker) for each unit that has `me` on its threat list; fn returns true to stop. Returns true when there was at
// least one such unit (T: the first HostileReference is not null; F: ThreatManager::IsThreateningAnyone counts offline entries
// when asked, as T's reference list does).
template <class Fn>
inline bool ForEachThreatenerCompat(Unit* me, Fn&& fn)
{
    ThreatManager const& threat = me->GetThreatManager();
    if (!threat.IsThreateningAnyone(true))
        return false;
    for (auto const& entry : threat.GetThreatenedByMeList())
    {
        Unit* attacker = entry.second->GetOwner();
        if (attacker && fn(attacker))
            break;
    }
    return true;
}

// The rank of a creature. T creature_template.rank (CreatureEliteType); F: the classification mapped to the same numbers, the world
// boss being the boss type flag of the creature's difficulty (Creature.cpp:2637 isWorldBoss) which F keeps apart.
inline uint32 CreatureRankCompat(Creature const* creature)
{
    if (creature->isWorldBoss())
        return CREATURE_ELITE_WORLDBOSS;
    switch (creature->GetCreatureClassification())
    {
        case CreatureClassifications::Elite:     return CREATURE_ELITE_ELITE;
        case CreatureClassifications::RareElite: return CREATURE_ELITE_RAREELITE;
        case CreatureClassifications::Rare:      return CREATURE_ELITE_RARE;
        default:                                 return CREATURE_ELITE_NORMAL;
    }
}
// Is the creature immune to taunt. T template immunity flag, F the template's "no taunt" extra flag, the one Unit::TauntApply tests.
inline bool CreatureTauntImmuneCompat(Creature const* creature)
{
    return (creature->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_TAUNT) != 0;
}
// Has the creature a tap (loot rights) holder / is it tapped by this player or a member of their party. T Creature.h:741,746,
// F Creature.h:314,317 (spelt with a lower case first letter).
inline bool CreatureHasLootRecipientCompat(Creature const* creature) { return creature->hasLootRecipient(); }
inline bool CreatureIsTappedByCompat(Creature const* creature, Player const* player) { return creature->isTappedBy(player); }

// ---------------------------------------------------------------------------------------------------------------------
// Sight
// ---------------------------------------------------------------------------------------------------------------------
// Can this player see that unit now (stealth, invisibility, GM state; not line of sight). T IsVisibleForOrDetect(player, the camera
// body, detect = true); F WorldObject::CanSeeOrDetect (Object.h:398) with the distance check on (stealth detection by distance).
inline bool CanSeeOrDetectCompat(Player* viewer, Unit const* target)
{
    CanSeeOrDetectExtraArgs args;
    args.DistanceCheck = true;
    return viewer->CanSeeOrDetect(target, args);
}
// Does the viewer see the object (T WorldObject::isVisibleFor(viewer, viewPoint), the viewpoint being the viewer itself in the bot).
inline bool IsVisibleForCompat(WorldObject const* target, Player const* viewer) { return viewer->CanSeeOrDetect(target); }
// A GM hidden from players (T VISIBILITY_OFF; F Unit::IsVisible is the GM visibility, Unit.cpp:9124).
inline bool IsHiddenGmCompat(Unit* unit) { return !unit->IsVisible(); }

// ---------------------------------------------------------------------------------------------------------------------
// Spells in flight: what a unit is casting at whom.
// ---------------------------------------------------------------------------------------------------------------------
// The unit / corpse a cast was aimed at. T SpellCastTargets::getUnitTargetGuid, F SpellDefines.h:385,393.
inline ObjectGuid SpellUnitTargetGuidCompat(Spell const* spell) { return spell->m_targets.GetUnitTargetGUID(); }
inline ObjectGuid SpellCorpseTargetGuidCompat(Spell const* spell) { return spell->m_targets.GetCorpseTargetGUID(); }
// The ground point a cast was aimed at; false when it has none. T `m_targetMask & TARGET_FLAG_DEST_LOCATION` + getDestination,
// F SpellDefines.h:416,427 (HasDst, GetDstPos).
inline bool SpellDestinationCompat(Spell const* spell, float& x, float& y, float& z)
{
    if (!spell->m_targets.HasDst())
        return false;
    WorldLocation const* dst = spell->m_targets.GetDstPos();
    x = dst->GetPositionX();
    y = dst->GetPositionY();
    z = dst->GetPositionZ();
    return true;
}
// Is the cast still being prepared (the cast time runs). Both cores name the state the same (T Spell.h, F Spell.h:257).
inline bool SpellIsPreparingCompat(Spell* spell) { return spell->getState() == SPELL_STATE_PREPARING; }
// Has the cast left the caster and is running (T SPELL_STATE_CASTING: cast time over, waiting for the delay; or channelling).
// F: LAUNCHED (the missile is in flight, Spell.h:258) or channelling. Different numbers: see the notes.
inline bool SpellIsInFlightOrChannelingCompat(Spell* spell)
{
    return spell->getState() == SPELL_STATE_LAUNCHED || spell->getState() == SPELL_STATE_CHANNELING;
}
// The amount effect `effIndex` of a cast gives to a target. T Spell::CalculateDamage(index, target), F takes the effect record.
inline int32 SpellEffectAmountCompat(Spell* spell, uint32 effIndex, Unit* target)
{
    return int32(spell->CalculateDamage(spell->GetSpellInfo()->GetEffect(SpellEffIndex(effIndex)), target));
}
// The power the spell costs its caster, in the spell's own power type, without the caster's spell mods. T Spell::CalculatePowerCost
// (static), F SpellInfo::CalcPowerCost (SpellInfo.h:578), 0 when the spell has no cost in that power.
inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    Optional<SpellPowerCost> cost = info->CalcPowerCost(Powers(spell->powerType), false, caster, info->GetSchoolMask());
    return cost ? uint32(cost->Amount) : 0;
}
// Is `spellId` a higher rank of the same spell than `otherId`. T SpellMgr::IsHighRankOfSpell, F SpellInfo.h:593.
inline bool IsHighRankOfSpellCompat(uint32 spellId, uint32 otherId)
{
    SpellInfo const* a = GetSpellInfoCompat(GetSpellEntryCompat(spellId));
    SpellInfo const* b = GetSpellInfoCompat(GetSpellEntryCompat(otherId));
    return a && b && a->IsHighRankOf(b);
}
// Does the item fit what the spell asks of its target item (T takes the spell entry, F the core's spell info).
inline bool ItemFitsSpellCompat(Item* item, SpellEntry const* spell) { return item->IsFitToSpellRequirements(GetSpellInfoCompat(spell)); }

// The spells of a pet that are not removed. T Pet::m_petSpells (state PETSPELL_REMOVED marks a gone one), F Pet.h:182 m_spells.
inline std::vector<uint32> PetSpellIdsCompat(Pet const* pet)
{
    std::vector<uint32> ids;
    for (auto const& entry : pet->m_spells)
        if (entry.second.state != PETSPELL_REMOVED)
            ids.push_back(entry.first);
    return ids;
}
// Is the pet a "mini pet" (a vanity companion). T Pet::getPetType() == MINI_PET; F has no such pet type, a companion is a plain
// creature (Pet.h PetType holds SUMMON_PET, HUNTER_PET): a Pet is never one.
inline bool PetIsMiniPetCompat(Pet const* /*pet*/) { return false; }
// Is a hunter's pet happy. T `GetHappinessState() == HAPPY`, F the enum is nested (Pet.h:97).
inline bool PetIsHappyCompat(Pet const* pet) { return pet->GetHappinessState() == Pet::HAPPY; }

// ---------------------------------------------------------------------------------------------------------------------
// Controlled units, combo points, items, home bind
// ---------------------------------------------------------------------------------------------------------------------
// Calls fn(Unit*) for every unit the unit controls: pet, mini pet, guardians, totems and charm. T CallForAllControlledUnits with
// the five CONTROLLED_* kinds (Unit.h:1563), F Unit.h:1256 m_Controlled (one set holding pets, guardians, totems and charms).
template <class Fn>
inline void ForEachControlledUnitCompat(Unit* unit, Fn&& fn)
{
    for (Unit* controlled : unit->m_Controlled)
        fn(controlled);
}
// The guid of the unit the player's combo points are on, and how many there are. T Player.h:1832,1834, F Unit.h:1225
// (GetClassicComboTarget) and the combo power.
inline ObjectGuid ComboTargetGuidCompat(Player const* player) { return player->GetClassicComboTarget(); }
inline uint8 ComboPointsCompat(Player const* player) { return uint8(player->GetPower(POWER_COMBO_POINTS)); }

// Durability of an item now / at most. T item fields ITEM_FIELD_DURABILITY / ITEM_FIELD_MAXDURABILITY, F the item's update data.
inline uint32 ItemDurabilityCompat(Item const* item) { return *item->m_itemData->Durability; }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return *item->m_itemData->MaxDurability; }

// The player's home bind (map and point). T Player::GetHomeBindLocation, F Player.h:2820 m_homebind.
inline decltype(auto) HomeBindLocationCompat(Player* player) { return (player->m_homebind); }

// The distance between two objects as the bot meant by "combat distance" (T Object.h:890): the combat-reach form,
// GetDistanceCompat with SizeFactor::CombatReach (TbCompat_Unit.h).
inline float GetCombatDistanceCompat(WorldObject const* self, WorldObject const* target)
{
    return GetDistanceCompat(self, target, SizeFactor::CombatReach);
}

// A dynamic object's ground effect. T keeps the effect index and the affected set on the dynamic object (DynamicObject.h:51);
// F's dynamic object holds only the spell and its caster (DynamicObject.h:80-83, the aura it carries is protected). The effect
// index: the spell's persistent-area-aura effect, the one that makes a dynamic object (0 if the spell has none). Affecting: the
// unit carries the spell from that caster. Different numbers: see the notes.
inline SpellEffectIndex DynamicObjectEffectIndexCompat(DynamicObject const* dyn)
{
    if (SpellInfo const* info = dyn->GetSpellInfo())
        for (SpellEffectInfo const& effect : info->GetEffects())
            if (effect.Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                return SpellEffectIndex(effect.EffectIndex);
    return SpellEffectIndex(0);
}
inline bool DynamicObjectAffectsCompat(DynamicObject const* dyn, Unit* unit)
{
    return unit->HasAura(dyn->GetSpellId(), dyn->GetCasterGUID());
}

// The path object (R3 names PathFinder = PathGenerator). Compute a path to a point; is it a complete walkable path; its points.
// T PathFinder::calculate / getPathType / getPath, F PathGenerator::CalculatePath / GetPathType / GetPath.
inline bool PathCalculateCompat(PathFinder& path, float x, float y, float z, bool forceDest) { return path.CalculatePath(x, y, z, forceDest); }
inline bool PathIsCompleteCompat(PathFinder const& path) { return path.GetPathType() == PATHFIND_NORMAL; }
inline Movement::PointsArray const& PathPointsCompat(PathFinder const& path) { return path.GetPath(); }

// Is a point in line of sight of an object. T WorldObject::IsWithinLOS(x, y, z, checkDynLos), F takes LineOfSightChecks (true is
// VMAP + game objects, false VMAP alone, as IsWithinLOSInMapCompat in TbCompat_Unit.h).
inline bool IsWithinLOSPointCompat(WorldObject const* from, float x, float y, float z, bool checkDynLos = true)
{
    return from->IsWithinLOS(x, y, z, checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP);
}
