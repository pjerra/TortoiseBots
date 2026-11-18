// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R4V (values).
// Same names and signatures as forever/src/compat/TbCompat_V.h; each forwards to the call the bot always made.
// Owned by package R4V: no other package edits this file.
#pragma once

#include "Maps/PathFinder.h"   // PathFinder, PATHFIND_NORMAL, Movement::PointsArray: the path helpers at the end name them

// Players, guids, groups ------------------------------------------------------------------------------------------------
inline Player* GetPlayerCompat(ObjectGuid const& guid) { return sObjectMgr.GetPlayer(guid); }
inline uint64 GuidKeyCompat(ObjectGuid const& guid) { return guid.GetRawValue(); }
inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->m_duel ? player->m_duel->opponent : ObjectGuid();
}
inline ObjectGuid TargetIconGuidCompat(Group* group, uint8 icon) { return ObjectGuid(group->GetTargetIcon(icon)); }
inline ObjectGuid GroupMainTankGuidCompat(Group const* group) { return group->GetMainTankGuid(); }
inline ObjectGuid GroupMainAssistGuidCompat(Group const* group) { return group->GetMainAssistantGuid(); }
inline std::string GuidTextCompat(ObjectGuid const& guid)
{
    std::ostringstream out;
    out << guid;
    return out.str();
}
template <class Iterator>
inline void ShuffleCompat(Iterator begin, Iterator end) { std::shuffle(begin, end, *GetRandomGenerator()); }

// Grid searches: the searcher and the visit the call sites always made --------------------------------------------------
template <class Check>
inline void VisitUnitsCompat(WorldObject const* center, std::list<Unit*>& out, Check& check, float range)
{
    MaNGOS::UnitListSearcher<Check> searcher(out, check);
    Cell::VisitAllObjects(center, searcher, range);
}
template <class Check>
inline void VisitUnitsAtCompat(float x, float y, Map* map, std::list<Unit*>& out, Check& check, float range)
{
    MaNGOS::UnitListSearcher<Check> searcher(out, check);
    Cell::VisitAllObjects(x, y, map, searcher, range, true);
}
template <class Check>
inline void VisitGameObjectsCompat(WorldObject const* center, std::list<GameObject*>& out, Check& check, float range)
{
    MaNGOS::GameObjectListSearcher<Check> searcher(out, check);
    Cell::VisitAllObjects(center, searcher, range);
}
template <class Check>
inline void VisitGameObjectsAtCompat(float x, float y, Map* map, std::list<GameObject*>& out, Check& check, float range)
{
    MaNGOS::GameObjectListSearcher<Check> searcher(out, check);
    Cell::VisitAllObjects(x, y, map, searcher, range, true);
}
template <class Check>
inline void VisitGridObjectsCompat(WorldObject const* center, std::list<WorldObject*>& out, Check& check, float range)
{
    MaNGOS::WorldObjectListSearcher<Check> searcher(out, check);
    Cell::VisitGridObjects(center, searcher, range);
}
template <class Check>
inline void VisitGridObjectsAtCompat(float x, float y, Map* map, std::list<WorldObject*>& out, Check& check, float range)
{
    MaNGOS::WorldObjectListSearcher<Check> searcher(out, check);
    Cell::VisitGridObjects(x, y, map, searcher, range, true);
}
inline void VisitFriendlyUnitsCompat(Unit const* center, std::list<Unit*>& out, float range)
{
    MaNGOS::AnyFriendlyUnitInObjectRangeCheck check(center, range);
    VisitUnitsCompat(center, out, check, range);
}

// Auras -----------------------------------------------------------------------------------------------------------------
struct AuraOfTypeCompat
{
    SpellEntry const* spell;
    uint32 effIndex;
    Unit* caster;
};
template <class Fn>
inline bool ForEachAuraOfTypeCompat(Unit const* unit, AuraType type, Fn&& fn)
{
    for (Aura const* aura : unit->GetAurasByType(type))
    {
        if (!aura)
            continue;
        AuraOfTypeCompat const view{ GetSpellEntryCompat(aura->GetSpellProto()), uint32(aura->GetEffIndex()), aura->GetCaster() };
        if (fn(view))
            return true;
    }
    return false;
}
inline bool AuraIsPositiveCompat(Aura const* aura) { return aura->IsPositive(); }
inline bool IsSilencedCompat(Unit const* unit) { return unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED); }
inline bool UnitHasFlagCompat(Unit const* unit, uint32 flags) { return unit->HasFlag(UNIT_FIELD_FLAGS, flags); }
inline ObjectGuid UnitTargetGuidCompat(Unit const* unit) { return unit->GetGuidValue(UNIT_FIELD_TARGET); }

// Threat and hostility --------------------------------------------------------------------------------------------------
inline Unit* CurrentThreatVictimCompat(Unit* unit)
{
    HostileReference* ref = unit->GetThreatManager().getCurrentVictim();
    return ref ? ref->getTarget() : nullptr;
}
template <class Fn>
inline bool ForEachThreatenerCompat(Unit* me, Fn&& fn)
{
    HostileReference* ref = me->GetHostileRefManager().getFirst();
    if (!ref)
        return false;
    while (ref)
    {
        ThreatManager* threatManager = ref->GetSource();
        Unit* attacker = threatManager->getOwner();
        if (fn(attacker))
            break;
        ref = ref->next();
    }
    return true;
}
inline uint32 CreatureRankCompat(Creature const* creature) { return creature->GetCreatureInfo()->rank; }
inline bool CreatureTauntImmuneCompat(Creature const* creature) { return creature->HasImmunityFlag(CREATURE_IMMUNITY_TAUNT); }
inline bool CreatureHasLootRecipientCompat(Creature const* creature) { return creature->HasLootRecipient(); }
inline bool CreatureIsTappedByCompat(Creature const* creature, Player const* player) { return creature->IsTappedBy(player); }

// Sight -----------------------------------------------------------------------------------------------------------------
inline bool CanSeeOrDetectCompat(Player* viewer, Unit const* target)
{
    return target->IsVisibleForOrDetect(viewer, viewer->GetCamera().GetBody(), true);
}
inline bool IsVisibleForCompat(WorldObject const* target, Player const* viewer) { return target->isVisibleFor(viewer, viewer); }
inline bool IsHiddenGmCompat(Unit* unit) { return unit->GetVisibility() == VISIBILITY_OFF; }

// Spells in flight ------------------------------------------------------------------------------------------------------
inline ObjectGuid SpellUnitTargetGuidCompat(Spell const* spell) { return spell->m_targets.getUnitTargetGuid(); }
inline ObjectGuid SpellCorpseTargetGuidCompat(Spell const* spell) { return spell->m_targets.getCorpseTargetGuid(); }
inline bool SpellDestinationCompat(Spell const* spell, float& x, float& y, float& z)
{
    if (!(spell->m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
        return false;
    spell->m_targets.getDestination(x, y, z);
    return true;
}
inline bool SpellIsPreparingCompat(Spell* spell) { return spell->getState() == SPELL_STATE_PREPARING; }
inline bool SpellIsInFlightOrChannelingCompat(Spell* spell)
{
    return spell->getState() == SPELL_STATE_CASTING || spell->getState() == SPELL_STATE_CHANNELING;
}
inline int32 SpellEffectAmountCompat(Spell* spell, uint32 effIndex, Unit* target)
{
    return spell->CalculateDamage(SpellEffectIndex(effIndex), target);
}
inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit* caster)
{
    return Spell::CalculatePowerCost(spell, caster, nullptr, nullptr);
}
inline bool IsHighRankOfSpellCompat(uint32 spellId, uint32 otherId) { return sSpellMgr.IsHighRankOfSpell(spellId, otherId); }
inline bool ItemFitsSpellCompat(Item* item, SpellEntry const* spell) { return item->IsFitToSpellRequirements(spell); }

inline std::vector<uint32> PetSpellIdsCompat(Pet const* pet)
{
    std::vector<uint32> ids;
    for (PetSpellMap::const_iterator itr = pet->m_petSpells.begin(); itr != pet->m_petSpells.end(); ++itr)
        if (itr->second.state != PETSPELL_REMOVED)
            ids.push_back(itr->first);
    return ids;
}
inline bool PetIsMiniPetCompat(Pet const* pet) { return MINI_PET == pet->getPetType(); }
inline bool PetIsHappyCompat(Pet const* pet) { return pet->GetHappinessState() == HAPPY; }

// Controlled units, combo points, items, home bind ----------------------------------------------------------------------
template <class Fn>
inline void ForEachControlledUnitCompat(Unit* unit, Fn&& fn)
{
    unit->CallForAllControlledUnits(fn, CONTROLLED_PET | CONTROLLED_GUARDIANS | CONTROLLED_CHARM | CONTROLLED_MINIPET | CONTROLLED_TOTEMS);
}
inline ObjectGuid ComboTargetGuidCompat(Player const* player) { return player->GetComboTargetGuid(); }
inline uint8 ComboPointsCompat(Player const* player) { return player->GetComboPoints(); }
inline uint32 ItemDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_DURABILITY); }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY); }
inline decltype(auto) HomeBindLocationCompat(Player* player) { return player->GetHomeBindLocation(); }
inline float GetCombatDistanceCompat(WorldObject const* self, WorldObject const* target) { return self->GetCombatDistance(target); }

inline SpellEffectIndex DynamicObjectEffectIndexCompat(DynamicObject const* dyn) { return dyn->GetEffIndex(); }
inline bool DynamicObjectAffectsCompat(DynamicObject* dyn, Unit* unit) { return dyn->IsAffecting(unit); }

inline bool PathCalculateCompat(PathFinder& path, float x, float y, float z, bool forceDest) { return path.calculate(x, y, z, forceDest); }
inline bool PathIsCompleteCompat(PathFinder const& path) { return path.getPathType() == PATHFIND_NORMAL; }
inline Movement::PointsArray const& PathPointsCompat(PathFinder const& path) { return path.getPath(); }

inline bool IsWithinLOSPointCompat(WorldObject const* from, float x, float y, float z, bool checkDynLos = true)
{
    return from->IsWithinLOS(x, y, z, checkDynLos);
}
