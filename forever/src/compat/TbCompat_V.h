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
// T UNIT_STAT_CAN_NOT_REACT = STUNNED | FEIGN_DEATH | CONFUSED | FLEEING (UnitDefines.h); the same union of the F bits
// TbCompat_Names.h maps (FEIGN_DEATH is F's DIED).
#define UNIT_STAT_CAN_NOT_REACT (UNIT_STATE_STUNNED | UNIT_STATE_DIED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING)
// T's untargetable flag is the "not selectable" bit (cmangos-compat-shim.h:1013 aliases it), F's is UNIT_FLAG_UNINTERACTIBLE
// (UnitDefines.h:191, 0x02000000), the same bit forever-compat-shim.h:254 already names UNIT_FLAG_NOT_SELECTABLE.
#ifndef UNIT_FLAG_UNTARGETABLE
#define UNIT_FLAG_UNTARGETABLE UNIT_FLAG_UNINTERACTIBLE
#endif

// The guid on a raid target icon slot (0..7), empty when the slot is unset. T Group.h returns the raw uint64 of the guid, F Group.h:362
// returns the ObjectGuid.
inline ObjectGuid TargetIconGuidCompat(Group* group, uint8 icon) { return group->GetTargetIcon(icon); }

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

// Is the unit silenced (any school). T: the unit flag, F: the silenced school mask (Unit.h:1524).
inline bool IsSilencedCompat(Unit const* unit) { return unit->IsSilenced(SPELL_SCHOOL_MASK_ALL); }
// The guid the unit has selected / is aiming at. T `GetGuidValue(UNIT_FIELD_TARGET)`, F Unit.h GetTarget (same field).
inline ObjectGuid UnitTargetGuidCompat(Unit const* unit) { return unit->GetTarget(); }

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

// The same walk with the threat `me` has on each attacker's list: fn(attacker, threat). T `HostileReference::getThreat`, F
// `ThreatReference::GetThreat` (ThreatManager.h:295, the amount plus the temporary modifier, never below 0). Added in R4M for
// TellTargetAction (A), which printed the amount and was the last user of the HostileRefManager walk besides the commander.
template <class Fn>
inline bool ForEachThreatenerThreatCompat(Unit* me, Fn&& fn)
{
    ThreatManager const& threat = me->GetThreatManager();
    if (!threat.IsThreateningAnyone(true))
        return false;
    for (auto const& entry : threat.GetThreatenedByMeList())
    {
        Unit* attacker = entry.second->GetOwner();
        if (attacker && fn(attacker, entry.second->GetThreat()))
            break;
    }
    return true;
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
inline bool DynamicObjectAffectsCompat(DynamicObject* dyn, Unit* unit)
{
    return unit->HasAura(dyn->GetSpellId(), dyn->GetCasterGUID());
}
