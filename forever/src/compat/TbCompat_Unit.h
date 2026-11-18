// Both-core helpers for unit, map and session calls whose shape differs (slice 1, round R3). Included from
// forever-compat-shim.h. The Turtle counterpart, with the same names and signatures that forward to the old calls, is
// ai/cmangos-compat-r3.h, so the shared bot code reads the same on both cores.
//
// Each helper says what the Turtle call was and what ForeverCore answers it with (T = Turtle core src/game, F = pinned
// ForeverCore src/server/game).
#pragma once

// (PhasingHandler.h is one of the core headers botpch_forever.h includes before the shim.)

// Line of sight between two objects of one map. T Object.cpp:1514: `IsWithinLOSInMap(obj, bool checkDynLos)`, the bool adds
// the dynamic (game object) models to the static ones. F Object.h:364 takes LineOfSightChecks: static data is VMAP, game
// object models are GOBJECT (SharedDefines.h:7475-7481), so true is both and false the static data alone.
inline bool IsWithinLOSInMapCompat(WorldObject const* from, WorldObject const* to, bool checkDynLos = true)
{
    return from->IsWithinLOSInMap(to, checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP);
}

// Line of sight between two points of a map. T Map.cpp:3047 `isInLineOfSight(x1..z2, checkDynLos)`; F Map.h:509 also asks
// for a phase shift and the ignore flags. A map-only caller has no viewer, so the empty phase shift (what a viewer without
// phases sees) and no ignored models.
inline bool IsInLineOfSightCompat(Map const* map, float x1, float y1, float z1, float x2, float y2, float z2, bool checkDynLos = true)
{
    return map->isInLineOfSight(PhasingHandler::GetEmptyPhaseShift(), x1, y1, z1, x2, y2, z2,
        checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing);
}

// Height of the ground at a point. T Map.h:568 `GetHeight(x, y, z, vmap, maxSearchDist)`, F Map.h:330 the same plus the phase
// shift (the game-object floors depend on it); the default search distance is 10 yards in both (T GridMap.h:38).
inline float GetHeightCompat(Map* map, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH)
{
    return map->GetHeight(PhasingHandler::GetEmptyPhaseShift(), x, y, z, vmap, maxSearchDist);
}

// The bearing from one object to another, 0..2*pi. T Object.cpp:1650-1668 (a null target gives 0), F Position.h:
// GetAbsoluteAngle (atan2 normalised to 0..2*pi).
inline float GetAngleCompat(WorldObject const* from, WorldObject const* to)
{
    return to ? from->GetAbsoluteAngle(to) : 0.0f;
}

// Whether the session belongs to a connected player and not to a headless bot. The Turtle core's WorldSession has
// HasNetworkTransport (a socket is attached); ForeverCore keeps the same fact as the flag the bot session sets when it is
// made (WorldSession.h:1078, TbBotSession.cpp). A null session has no transport.
inline bool HasNetworkTransportCompat(WorldSession* session) { return session && !session->IsBot(); }

// The creature's home point. T Creature.h:579 `GetRespawnCoord(x, y, z, &ori, &dist)`: where it respawns and the wander
// distance; F Creature.h:115 `GetRespawnPosition(&dist)` returns the same point as a Position.
inline void GetRespawnCoordCompat(Creature const* creature, float& x, float& y, float& z, float* ori = nullptr, float* dist = nullptr)
{
    Position const pos = creature->GetRespawnPosition(dist);
    x = pos.GetPositionX();
    y = pos.GetPositionY();
    z = pos.GetPositionZ();
    if (ori)
        *ori = pos.GetOrientation();
}

// How far a melee swing from `self` reaches `victim`. T Unit.cpp:11035 `GetCombatReach(victim, ability, flat_mod)`: both
// combat reaches plus a flat part plus the melee-leeway bonus; F Unit.cpp:749 `GetMeleeRange(victim)`: both combat reaches
// plus 4/3, at least the nominal melee range. The core's own answer to the same question; the flat part is added on top,
// the ability flag only selects Turtle's leeway and has no ForeverCore counterpart.
inline float GetCombatReachCompat(Unit const* self, Unit const* victim, bool /*ability*/, float flatMod)
{
    return self->GetMeleeRange(victim) + flatMod;
}

// The three "real player / same group" helpers the Turtle shim defines (ai/cmangos-compat-shim.h:1053-1057), here over the
// core's own group and session calls. T Group::IsMember takes the guid (ai/cmangos-compat-shim.h), F Group.h IsMember(ObjectGuid).
inline bool isRealPlayer_Helper(Player* p) { return p && p->GetSession() && HasNetworkTransportCompat(p->GetSession()); }
inline bool IsRealPlayer_Helper(Player* p) { return isRealPlayer_Helper(p); }
inline bool IsInGroup_Helper(Player* a, Player* b, bool sameGroup = false)
{
    if (!a || !b)
        return false;
    Group* group = a->GetGroup();
    if (!group)
        return false;
    if (sameGroup)
        return group == b->GetGroup();
    return group->IsMember(b->GetGUID());
}

// "How far apart are these two objects": T Object.h:137 SizeFactor picks what is taken off the centre distance
// (Object.cpp:1383-1426): None nothing, BoundingRadius both bounding radii, CombatReach both combat reaches,
// CombatReachWithMelee both combat reaches raised to at least 1.5; the result is never negative. F has the combat-reach
// form as GetDistance(obj) (Object.cpp:431-435) and no enum, so the enum is declared here and the others are computed from
// the same members the Turtle code reads.
enum class SizeFactor
{
    None,
    BoundingRadius,
    CombatReach,
    CombatReachWithMelee,
};

inline float GetDistanceCompat(WorldObject const* self, WorldObject const* obj, SizeFactor distcalc)
{
    float const centre = self->GetExactDist(obj);
    float size = 0.0f;
    switch (distcalc)
    {
        case SizeFactor::BoundingRadius:
            if (Unit const* a = self->ToUnit())
                size = a->GetBoundingRadius();
            if (Unit const* b = obj->ToUnit())
                size += b->GetBoundingRadius();
            break;
        case SizeFactor::CombatReach:
            size = self->GetCombatReach() + obj->GetCombatReach();
            break;
        case SizeFactor::CombatReachWithMelee:
            size = std::max(1.5f, self->GetCombatReach()) + std::max(1.5f, obj->GetCombatReach());
            break;
        default:
            break;
    }
    float const dist = centre - size;
    return dist > 0.0f ? dist : 0.0f;
}

// Whether `target` lies in the arc in front of `self`. T Object.h:944 `HasInArc(target, arcangle, offset)`, F Position.h
// `HasInArc(arcangle, pos, border = 2)`: both test the target's bearing against the facing within +-arcangle/2 (the default
// border 2 halves the arc), the arguments come in the other order.
inline bool HasInArcCompat(WorldObject const* self, WorldObject const* target, float arcAngle)
{
    return self->HasInArc(arcAngle, target);
}

// Whether a melee SPELL of `bot` (a spell whose range is the combat range) reaches `target` now. T ReachTargetActions.h
// (before R3) asked Unit::CanReachWithMeleeSpellAttack with a modifier built from the caster's range spell mods and range
// auras; F answers it where the cast does: Spell::CalcMinMaxRange (Spell.cpp:7677), the numbers CheckRange tests, which
// start from the caster's melee range to this target and apply the range spell mods itself.
inline bool MeleeSpellReachCompat(Player* bot, SpellEntry const* spell, Unit* target)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    if (!info || !target)
        return false;
    SpellRange const range = Spell::CalcMinMaxRange(bot, target, false, info, false);
    return bot->GetExactDistSq(target) <= range.Max * range.Max;
}

// The weapon in a hand. T Player.h:1281 `GetWeaponForAttack(type, nonbroken, useable)`, F Player.h:1514 `GetWeaponForAttack(type,
// useable)`: F's `useable` is "not broken and the hand may be used" (the Turtle pair together), so either flag asks it.
inline Item* GetWeaponForAttackCompat(Player const* player, WeaponAttackType attackType, bool nonBroken, bool useable)
{
    return player->GetWeaponForAttack(attackType, nonBroken || useable);
}

// Whether the creature carries a weapon. T Creature.cpp:3981 reads the item class of its main-hand virtual item; F keeps the
// virtual item as an item id (Unit.h:1893), whose template has the class.
inline bool CreatureHasWeaponCompat(Creature const* creature)
{
    uint32 const itemId = creature->GetVirtualItemId(0);
    ItemTemplate const* tpl = itemId ? sObjectMgr.GetItemTemplate(itemId) : nullptr;
    return tpl && tpl->GetClass() == ITEM_CLASS_WEAPON;
}

// ---------------------------------------------------------------------------------------------------------------
// Maps and terrain, as WorldPosition.h uses them (all reads, no map is ever changed).
// ---------------------------------------------------------------------------------------------------------------
// The terrain object. T keeps one per map in the terrain manager for the life of the server and the bot holds a raw pointer
// (`sTerrainMgr.LoadTerrain(mapId)`, or `map->GetTerrain()` when the map is loaded); F's LoadTerrain returns a shared_ptr the
// manager does not own, so the helper keeps the pointers it handed out (one per map id) and the raw pointer stays valid.
inline TerrainInfo const* TerrainOfCompat(Map const* map, uint32 mapId)
{
    if (map)
        return map->GetTerrain();
    static std::mutex lock;
    static std::unordered_map<uint32, std::shared_ptr<TerrainInfo>> kept;
    std::lock_guard<std::mutex> guard(lock);
    std::shared_ptr<TerrainInfo>& slot = kept[mapId];
    if (!slot)
        slot = TerrainMgr::Instance().LoadTerrain(mapId);
    return slot.get();
}

// T GridMap.h:149-154 take the point; F's TerrainInfo (TerrainMgr.h:100-102) also takes the map id and a phase shift. The
// empty phase shift is what a viewer without phases sees; the map id is the terrain's own (TerrainMgr.h:49).
inline bool TerrainIsInWaterCompat(TerrainInfo const* terrain, float x, float y, float z)
{
    return const_cast<TerrainInfo*>(terrain)->IsInWater(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(), x, y, z);
}
inline bool TerrainIsUnderWaterCompat(TerrainInfo const* terrain, float x, float y, float z)
{
    return const_cast<TerrainInfo*>(terrain)->IsUnderWater(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(), x, y, z);
}
// T GetWaterLevel(x, y, z, &ground): the liquid level at the point and, through the pointer, the ground under it. F's
// GetWaterOrGroundLevel (TerrainMgr.h:104) returns the same pair (the water level, else the ground) and the ground.
inline float TerrainWaterLevelCompat(TerrainInfo const* terrain, float x, float y, float z, float* ground = nullptr)
{
    return const_cast<TerrainInfo*>(terrain)->GetWaterOrGroundLevel(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(), x, y, z, ground);
}
// T `sTerrainMgr.GetAreaId(mapId, x, y, z)`; F asks for a phase shift as well.
inline uint32 GetAreaIdCompat(WorldObject const* wo)
{
    return TerrainMgr::Instance().GetAreaId(wo->GetPhaseShift(), wo->GetMapId(), wo->GetPositionX(), wo->GetPositionY(), wo->GetPositionZ());
}

// The DBC row of a map (T SQL store `sMapStorage`, F DB2 store).
inline MapEntry const* MapEntryCompat(uint32 mapId) { return sMapStore.LookupEntry(mapId); }

// The instance id of the first loaded map with this map id (0 when none is). T walks the map manager's container,
// F visits every map with that id.
inline uint32 FirstInstanceIdCompat(uint32 mapId)
{
    uint32 instanceId = 0;
    bool found = false;
    sMapMgr.DoForAllMapsWithMapId(mapId, [&](Map* map)
    {
        if (!found)
        {
            instanceId = map->GetInstanceId();
            found = true;
        }
    });
    return instanceId;
}

// How far a client sees on this map: T `Map::GetVisibilityDistance()` (with `World::GetMaxVisibleDistance*` when the map is
// not loaded: the continent or the instance range), F `Map::GetVisibilityRange()` (Map.h:257) and the same two configured ranges
// (World.h:248-249).
inline float VisibilityDistanceCompat(Map const* map, bool overworld)
{
    if (map)
        return map->GetVisibilityRange();
    return sWorld.getFloatConfig(overworld ? CONFIG_MAX_VISIBILITY_DISTANCE_CONTINENT : CONFIG_MAX_VISIBILITY_DISTANCE_INSTANCE);
}

// Whether the area carries any of the flags. T AreaTableEntry::Flags is one word; F keeps two (AreaTableEntry::Flags[0] is
// the one the classic client data fills, the same word the shim's AREA_FLAG_* names come from).
inline bool AreaHasFlagsCompat(AreaTableEntry const* area, AreaFlags mask)
{
    return area && (area->Flags[0] & uint32(mask)) != 0;
}

// The point a ray from the source to the destination stops at. T `Map::GetLosHitPosition(src, dest&, modifyDist)` moves the
// destination to the hit point and says whether it did; F `Map::getObjectHitPos` (Map.h:519) returns the hit point apart.
inline bool GetLosHitPositionCompat(Map* map, float srcX, float srcY, float srcZ, float& destX, float& destY, float& destZ, float modifyDist)
{
    float hitX = destX, hitY = destY, hitZ = destZ;
    bool const hit = map->getObjectHitPos(PhasingHandler::GetEmptyPhaseShift(), srcX, srcY, srcZ, destX, destY, destZ, hitX, hitY, hitZ, modifyDist);
    if (hit)
    {
        destX = hitX;
        destY = hitY;
        destZ = hitZ;
    }
    return hit;
}

// The path object: T's PathFinder is the core's path generator, F names it PathGenerator.
using PathFinder = PathGenerator;

// Which area a point lies in, as the key into the area table: T `sTerrainMgr.GetAreaFlag(mapId, x, y, z)` is the grid's area FLAG
// (a per-map index that GetAreaEntryByAreaFlagAndMap turns into the row), F has no flag and keys the table by area id
// (TerrainMgr.h:147). The same question with each core's own key; whoever turns the key into an AreaTableEntry
// (WorldPosition.cpp, R4C) reads it the way its core does.
inline uint16 AreaKeyAtCompat(uint32 mapId, float x, float y, float z)
{
    return uint16(TerrainMgr::Instance().GetAreaId(PhasingHandler::GetEmptyPhaseShift(), mapId, x, y, z));
}

// The two numbers of a spell range row: the hostile (first) minimum and maximum. T SpellRangeEntry minRange/maxRange, F
// RangeMin[0]/RangeMax[0] (DB2Structure.h:4158-4159; index 0 is the hostile range, 1 the friendly one). Same names as
// ai/cmangos-compat-shim.h:904-905.
inline float SpellRangeMin(SpellRangeEntry const* entry) { return entry ? entry->RangeMin[0] : 0.0f; }
inline float SpellRangeMax(SpellRangeEntry const* entry) { return entry ? entry->RangeMax[0] : 0.0f; }

// The core's string splitter on both cores: T shared/Util.cpp:173 splits on every character of `sep` and drops empty pieces.
using Tokens = std::vector<std::string>;
inline Tokens StrSplit(std::string const& src, std::string const& sep)
{
    Tokens r;
    std::string s;
    for (char const c : src)
    {
        if (sep.find(c) != std::string::npos)
        {
            if (!s.empty())
                r.push_back(s);
            s.clear();
        }
        else
            s += c;
    }
    if (!s.empty())
        r.push_back(s);
    return r;
}

// The bot's own case-insensitive substring search (Helpers.cpp:69), declared for every file as the Turtle shim does
// (ai/cmangos-compat-shim.h:385); the definition is the bot's, not the core's.
char* strstri(std::string const& s1, std::string const& s2);

// The faction template row by id. T `sObjectMgr.GetFactionTemplateEntry(id)`, F the DB2 store (DB2Stores.h).
inline FactionTemplateEntry const* GetFactionTemplateEntryCompat(uint32 id) { return sFactionTemplateStore.LookupEntry(id); }

// The name and level of an area. T AreaTableEntry::Name (a string) and AreaLevel; F AreaName (localized; the bot reads the
// first locale, enUS) and ExplorationLevel (DB2Structure.h AreaTableEntry).
inline std::string AreaNameCompat(AreaTableEntry const* area) { return area->AreaName[LOCALE_enUS]; }
inline int32 AreaLevelCompat(AreaTableEntry const* area) { return area->ExplorationLevel; }

// The object a guid names, looked up in one map. T `map->GetWorldObject(guid)`; F has the lookups per type.
inline WorldObject* GetWorldObjectCompat(Map* map, ObjectGuid const& guid)
{
    switch (guid.GetHigh())
    {
        case HighGuid::Creature:
        case HighGuid::Vehicle:
            return map->GetCreature(guid);
        case HighGuid::GameObject:
            return map->GetGameObject(guid);
        case HighGuid::Pet:
            return map->GetPet(guid);
        case HighGuid::DynamicObject:
            return map->GetDynamicObject(guid);
        case HighGuid::Corpse:
            return map->GetCorpse(guid);
        case HighGuid::Player:
            return ObjectAccessor::GetPlayer(map, guid);
        default:
            return nullptr;
    }
}

// The creature a spawn record is for, and the template's name. T CreatureData::creature_id is an array of ids per spawn (the bot
// reads the first), F CreatureData::id one id (SpawnData); T CreatureInfo::name / F CreatureTemplate::Name.
inline uint32 CreatureEntryOfCompat(CreatureData const& data) { return data.id; }
inline std::string const& CreatureNameCompat(CreatureInfo const* tpl) { return tpl->Name; }

// A grid search for units around an object. T's UnitListSearcher takes (list, check), F's (the searching object, list,
// check): the object gives the phase the search sees in. The wrapper has the F shape on both cores.
template <class Check>
struct UnitListSearcherCompat : Trinity::UnitListSearcher<Check>
{
    UnitListSearcherCompat(WorldObject const* searcher, std::list<Unit*>& objects, Check& check)
        : Trinity::UnitListSearcher<Check>(searcher, objects, check) { }
};

// Names as C strings: T GetName() returns char const*, F std::string; the bot hands it to printf-style calls (%s).
inline char const* CStrCompat(char const* text) { return text; }
inline char const* CStrCompat(std::string const& text) { return text.c_str(); }

// The player's experience and lifetime honorable kills. T reads the update fields (PLAYER_XP,
// PLAYER_FIELD_LIFETIME_HONORABLE_KILLS), F has them as accessors/active-player data fields.
inline uint32 PlayerXPCompat(Player const* player) { return player->GetXP(); }
inline uint32 PlayerLifetimeHonorableKillsCompat(Player const* player) { return player->m_activePlayerData->LifetimeHonorableKills; }

// A unit's main-hand damage range. T reads UNIT_FIELD_MINDAMAGE / UNIT_FIELD_MAXDAMAGE; F GetWeaponDamageRange (Unit.h:1606),
// the unit's base damage range for the attack type.
inline float UnitMinDamageCompat(Unit const* unit) { return unit->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE); }
inline float UnitMaxDamageCompat(Unit const* unit) { return unit->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE); }

// A server setting by its enum. T World::getConfig(eConfigUInt32Values), F World::getIntConfig (World.h); the shim's
// CONFIG_UINT32_* names are the F enumerators.
inline uint32 WorldIntConfigCompat(WorldIntConfigs index) { return sWorld.getIntConfig(index); }

// The spell a trap game object casts. T GameObjectInfo::trap.spellId, F GameObjectTemplate::trap.spell (GameObjectData.h:185-196).
inline uint32 GameObjectTrapSpellIdCompat(GameObjectTemplate const* info) { return info->trap.spell; }

// The quest in a quest-log slot (0 = empty), with the Turtle helper's own bounds check (ai/cmangos-compat-shim.h:36-41). T reads
// the slot's update field; F asks the player (Player.h:1831). The log has more slots on F (40 against 20).
inline uint32 GetQuestSlotIdCompat(Player const* player, uint16 slot)
{
    return player && slot < MAX_QUEST_LOG_SIZE ? player->GetQuestSlotQuestId(slot) : 0;
}

// =====================================================================================================================
// R4M (slice 1, integration): helpers that two or more of the packages V, A, P, C had defined, one name and one definition each.
// The table with both definitions side by side and the reason for each choice is forever/docs/SLICE1-R4M-NOTES.md section 1.
// Turtle side: ai/cmangos-compat-r3.h (same names and signatures). The names and macros of the same kind are in TbCompat_Names.h.
// =====================================================================================================================

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

// A guid as text. T prints through its own ostream operator, F's guid has none (ObjectGuid.h:397 ToString).
inline std::string GuidTextCompat(ObjectGuid const& guid) { return guid.ToString(); }

// The account a character belongs to, by guid. T `sObjectMgr.GetPlayerAccountIdByGUID`, F `CharacterCache` (Cache/CharacterCache.h:75).
inline uint32 AccountIdOfCharacterCompat(ObjectGuid const& guid) { return sCharacterCache->GetCharacterAccountIdByGuid(guid); }

// ---------------------------------------------------------------------------------------------------------------
// Duel. T Player::m_duel->opponent is the opponent's guid (null pointer = no duel); F Player::duel is a DuelInfo holding the
// opponent Player* (Player.h:462-472, :2205).
// ---------------------------------------------------------------------------------------------------------------
inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->duel && player->duel->Opponent ? player->duel->Opponent->GetGUID() : ObjectGuid::Empty;
}

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

// A player flag (T `HasFlag(PLAYER_FLAGS, flag)` on the object, F Player::HasPlayerFlag, Player.h:1306) and a unit flag (T
// `HasFlag(UNIT_FIELD_FLAGS, flag)`, F Unit.h:862 HasUnitFlag). The flag names the bot uses (PLAYER_FLAGS_GHOST, UNIT_FLAG_NOT_SELECTABLE)
// exist on both cores with the same meaning. A non-player object has no player flags.
inline bool HasPlayerFlagCompat(WorldObject const* obj, uint32 flag)
{
    Player const* player = obj->ToPlayer();
    return player && player->HasPlayerFlag(static_cast<PlayerFlags>(flag));
}

inline bool HasUnitFlagCompat(Unit const* unit, uint32 flag)
{
    return unit->HasUnitFlag(static_cast<UnitFlags>(flag));
}

// An item's durability and its maximum. T update fields ITEM_FIELD_DURABILITY / ITEM_FIELD_MAXDURABILITY, F the item data
// (Item.h:274-276: Durability, MaxDurability).
inline uint32 ItemDurabilityCompat(Item const* item) { return *item->m_itemData->Durability; }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return *item->m_itemData->MaxDurability; }

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

// ---------------------------------------------------------------------------------------------------------------------
// Threat and hostility: who threatens me, who do I threaten.
// ---------------------------------------------------------------------------------------------------------------------
// The unit the creature's threat list has picked as its target (null when the list has none). T ThreatManager.h:206 returns the
// stored HostileReference (getTarget() is the unit), a plain read. F's GetCurrentVictim (ThreatManager.cpp:245) re-selects the
// victim when the stored one is gone, which writes to the creature's threat list from the engine's thread; GetLastVictim
// (ThreatManager.cpp:253) is the same stored reference read without that write (null when it should be offline).
inline Unit* CurrentThreatVictimCompat(Unit* unit) { return unit->GetThreatManager().GetLastVictim(); }

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

// The cost of a spell in its own power type. T Spell::CalculatePowerCost (Spell.h:373); F SpellInfo::CalcPowerCost (SpellInfo.h:578),
// asked for the spell's power type, 0 when the spell has no cost in it.
inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    Optional<SpellPowerCost> cost = info->CalcPowerCost(Powers(spell->powerType), false, caster, info->GetSchoolMask());
    return cost ? uint32(cost->Amount) : 0u;
}

// ---------------------------------------------------------------------------------------------------------------
// 11. SPELL OBJECTS the healing triggers read.
// ---------------------------------------------------------------------------------------------------------------
// What one effect of a cast would do to a target. T Spell.h:372 CalculateDamage(effectIndex, target) (an int); F Spell.h:554
// CalculateDamage(effect row, target) (a double). The effect row is the cast spell's own.
inline int32 SpellCalculateDamageCompat(Spell* spell, uint32 effect, Unit* target)
{
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || effect >= info->GetEffects().size())
        return 0;
    return static_cast<int32>(spell->CalculateDamage(info->GetEffect(static_cast<SpellEffIndex>(effect)), target));
}

// A spell's range after the player's own modifiers. T `ApplySpellMod(spellId, SPELLMOD_RANGE, range)`; F Player.h:2142
// ApplySpellMod(SpellInfo const*, SpellModOp::Range, range).
inline void ApplySpellModRangeCompat(Player* player, uint32 spellId, float& range)
{
    if (SpellInfo const* info = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE))
        player->ApplySpellMod(info, SpellModOp::Range, range);
}

// ---------------------------------------------------------------------------------------------------------------
// 3. GEOMETRY. (The R3 helpers GetAngleCompat, GetHeightCompat, IsInLineOfSightCompat, IsWithinLOSInMapCompat, HasInArcCompat,
//    GetDistanceCompat, GetCombatReachCompat, GetLosHitPositionCompat are in TbCompat_Unit.h.)
// ---------------------------------------------------------------------------------------------------------------
// Line of sight from an object to a point. T Object.h:931 `IsWithinLOS(x, y, z, checkDynLos, targetHeight)` (the bool adds the
// dynamic game-object models); F Object.h:363 takes LineOfSightChecks (VMAP = static data, GOBJECT = game objects,
// SharedDefines.h:7475-7481), so true is both and false the static data alone, as IsWithinLOSInMapCompat does.
inline bool IsWithinLOSCompat(WorldObject const* from, float x, float y, float z, bool checkDynLos = true)
{
    return from->IsWithinLOS(x, y, z, checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP);
}

// ---------------------------------------------------------------------------------------------------------------
// 9. SMALL SHAPE DIFFERENCES found in the actions, triggers and warrior files.
// ---------------------------------------------------------------------------------------------------------------
// "Is the unit under water". T Unit.h:679 IsUnderwater(), F Unit.h:1115 IsUnderWater() (the bot's ServerFacade has a member
// IsUnderwater of its own, so the name cannot be a macro).
inline bool IsUnderwaterCompat(Unit const* unit) { return unit->IsUnderWater(); }

// ---------------------------------------------------------------------------------------------------------------
// 6. PATHS. T PathFinder (= PathInfo, Maps/PathFinder.h:146) and F PathGenerator (Movement/PathGenerator.h:63); the alias is in
//    TbCompat_Unit.h. The path types NORMAL 1, SHORTCUT 2, INCOMPLETE 4, NOPATH 8, NOT_USING_PATH 0x10 are the same on both;
//    DIFFERENT ANSWER: the higher bits (T 0x20 forced destination, 0x40 fly path, 0x80 underwater, 0x100 caster; F 0x20 short,
//    0x40/0x80 start/end far from the polygon).
// ---------------------------------------------------------------------------------------------------------------
// T PathInfo::calculate(x, y, z, forceDest) (PathFinder.h:66) from the owner's position; F CalculatePath(x, y, z, forceDest)
// (PathGenerator.h:86).
inline bool PathCalculateCompat(PathFinder& path, float x, float y, float z, bool forceDest = false)
{
    return path.CalculatePath(x, y, z, forceDest);
}

// T calculate(start, dest, forceDest) (PathFinder.h:67); F CalculatePath(sx, sy, sz, dx, dy, dz, forceDest) (:76).
inline bool PathCalculateCompat(PathFinder& path, G3D::Vector3 const& start, G3D::Vector3 const& dest, bool forceDest = false)
{
    return path.CalculatePath(start.x, start.y, start.z, dest.x, dest.y, dest.z, forceDest);
}

// The points of the last path (T getPath, F GetPath).
inline Movement::PointsArray const& PathPointsCompat(PathFinder const& path) { return path.GetPath(); }

// The type bits of the last path (T getPathType, F GetPathType).
inline uint32 PathTypeCompat(PathFinder const& path) { return static_cast<uint32>(path.GetPathType()); }

// The closest reachable point to the asked end (T getActualEndPosition by value, F GetActualEndPosition).
inline G3D::Vector3 PathActualEndCompat(PathFinder const& path) { return path.GetActualEndPosition(); }

// The length of the point path. T Length() (PathFinder.cpp:1011) asserts a non-empty path; F GetPathLength (PathGenerator.cpp:1479)
// would loop over an unsigned underflow on an empty one, so an empty path is 0 here.
inline float PathLengthCompat(PathFinder const& path)
{
    return path.GetPath().empty() ? 0.0f : path.GetPathLength();
}

inline bool PathIsCompleteCompat(PathFinder const& path) { return path.GetPathType() == PATHFIND_NORMAL; }


// The zone id of an area row and its name. T AreaTableEntry::ZoneId (0 for a zone itself) / Name (a plain string, may be null), F ParentAreaID
// (0 for none, DB2Structure.h:131) / AreaName (the enUS text). The name of a null row, or of a row with no name, is empty. (R4M: P and C each
// had these two; P checked the row, C checked the name, this one checks both.)
inline uint32 AreaZoneIdCompat(AreaTableEntry const* area) { return area->ParentAreaID; }
inline std::string AreaNameOrEmptyCompat(AreaTableEntry const* area)
{
    char const* name = area ? area->AreaName[LOCALE_enUS] : nullptr;
    return name ? name : "";
}

// Is the cast a channel in progress. T has no channeling state of its own: SPELL_STATE_CASTING (2) IS the channel (Spell.cpp:3999, a channeled
// spell's handle_immediate), and a missile in flight is SPELL_STATE_DELAYED; F SPELL_STATE_CHANNELING (Spell.h:259). (R4M: V's helper also
// answered F's LAUNCHED, a state T does not have an answer for; the read of both cores is in SLICE1-R4M-NOTES.md section 2.)
inline bool SpellIsChannelingCompat(Spell const* spell) { return spell->getState() == SPELL_STATE_CHANNELING; }

// ---------------------------------------------------------------------------------------------------------------
// 7. LIQUID AND TERRAIN reads. T GridMap.h:149-156 (TerrainInfo, GridMapLiquidData / GridMapLiquidStatus, MAP_LIQUID_TYPE_*
//    in Maps/GridMapDefines.h:57-74); F TerrainMgr.h:85-104 and common/Collision/Maps/MapDefines.h:96-150 (LiquidData,
//    ZLiquidStatus, map_liquidHeaderTypeFlags).
//    The legacy type-flag NUMBERS are kept (the module's policy files copy them, MagmaHazardPolicy.h): MAGMA 1, OCEAN 2, SLIME 4,
//    WATER 8, DARK_WATER 0x10. F numbers them WATER 1, OCEAN 2, MAGMA 4, SLIME 8, DARK 0x10, so a result is re-encoded.
//    DIFFERENT ANSWER: F asks for a collision height (its default 2.03) where T takes none; the status bands near the surface
//    can differ by that.
// ---------------------------------------------------------------------------------------------------------------
constexpr uint32 MAP_LIQUID_TYPE_NO_WATER = 0x00;
constexpr uint32 MAP_LIQUID_TYPE_MAGMA = 0x01;
constexpr uint32 MAP_LIQUID_TYPE_OCEAN = 0x02;
constexpr uint32 MAP_LIQUID_TYPE_SLIME = 0x04;
constexpr uint32 MAP_LIQUID_TYPE_WATER = 0x08;
constexpr uint32 MAP_LIQUID_TYPE_DARK_WATER = 0x10;
constexpr uint32 MAP_ALL_LIQUIDS = MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_SLIME;

struct GridMapLiquidData
{
    uint32 type_flags = 0;
    uint32 entry = 0;
    float level = 0.0f;
    float depth_level = 0.0f;
};
using GridMapLiquidStatus = ZLiquidStatus;

inline map_liquidHeaderTypeFlags LiquidFlagsFromLegacyCompat(uint32 legacy)
{
    map_liquidHeaderTypeFlags flags = map_liquidHeaderTypeFlags::NoWater;
    if (legacy & MAP_LIQUID_TYPE_WATER) flags |= map_liquidHeaderTypeFlags::Water;
    if (legacy & MAP_LIQUID_TYPE_OCEAN) flags |= map_liquidHeaderTypeFlags::Ocean;
    if (legacy & MAP_LIQUID_TYPE_MAGMA) flags |= map_liquidHeaderTypeFlags::Magma;
    if (legacy & MAP_LIQUID_TYPE_SLIME) flags |= map_liquidHeaderTypeFlags::Slime;
    if (legacy & MAP_LIQUID_TYPE_DARK_WATER) flags |= map_liquidHeaderTypeFlags::DarkWater;
    return flags;
}

inline uint32 LiquidFlagsToLegacyCompat(EnumFlag<map_liquidHeaderTypeFlags> flags)
{
    uint32 legacy = 0;
    if (flags.HasFlag(map_liquidHeaderTypeFlags::Water)) legacy |= MAP_LIQUID_TYPE_WATER;
    if (flags.HasFlag(map_liquidHeaderTypeFlags::Ocean)) legacy |= MAP_LIQUID_TYPE_OCEAN;
    if (flags.HasFlag(map_liquidHeaderTypeFlags::Magma)) legacy |= MAP_LIQUID_TYPE_MAGMA;
    if (flags.HasFlag(map_liquidHeaderTypeFlags::Slime)) legacy |= MAP_LIQUID_TYPE_SLIME;
    if (flags.HasFlag(map_liquidHeaderTypeFlags::DarkWater)) legacy |= MAP_LIQUID_TYPE_DARK_WATER;
    return legacy;
}

// T `terrain->getLiquidStatus(x, y, z, mask, &data)`; F `GetLiquidStatus(phaseShift, mapId, x, y, z, flags, &data)`.
inline GridMapLiquidStatus LiquidStatusCompat(TerrainInfo const* terrain, float x, float y, float z, uint32 reqLiquidType, GridMapLiquidData* data = nullptr)
{
    // Request 0 is "no type filter" on T (GridMap.cpp:550 `if (ReqLiquidType && !(ReqLiquidType & type))`, VMapManager2.cpp:281), which is an
    // empty request on F (TerrainMgr.cpp:728); a non-zero mask is a filter on both, translated type by type. (R4M: R4A passed 0 as an
    // engaged filter that matches nothing; R4C collapsed the four-type mask to "no filter", which adds dark water. T's exact rule is kept.)
    Optional<map_liquidHeaderTypeFlags> requested;
    if (reqLiquidType)
        requested = LiquidFlagsFromLegacyCompat(reqLiquidType);
    LiquidData out;
    ZLiquidStatus const status = const_cast<TerrainInfo*>(terrain)->GetLiquidStatus(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(),
        x, y, z, requested, &out);
    if (data)
    {
        data->type_flags = LiquidFlagsToLegacyCompat(out.type_flags);
        data->entry = out.entry;
        data->level = out.level;
        data->depth_level = out.depth_level;
    }
    return status;
}

// T GridMap.cpp:1060-1076 IsSwimmable: in the water, under it, walking on it, or just above it within JUMP_HEIGHT (0.6, :1057), and
// deeper than the radius. F has no such call; the same test over the liquid read above. T first asks whether a grid exists at the
// point; F's liquid read answers no water where there is none.
inline bool TerrainIsSwimmableCompat(TerrainInfo const* terrain, float x, float y, float z, float radius = 1.5f, GridMapLiquidData* data = nullptr)
{
    GridMapLiquidData local;
    GridMapLiquidData* liquid = data ? data : &local;
    GridMapLiquidStatus const status = LiquidStatusCompat(terrain, x, y, z, MAP_ALL_LIQUIDS, liquid);
    if (status == LIQUID_MAP_IN_WATER || status == LIQUID_MAP_UNDER_WATER || status == LIQUID_MAP_WATER_WALK ||
        (status == LIQUID_MAP_ABOVE_WATER && liquid->level + 0.6f >= z))
        return liquid->level - liquid->depth_level > radius;
    return false;
}

// T `terrain->GetHeightStatic(x, y, z)` (GridMap.h:148, checkVMap true, DEFAULT_HEIGHT_SEARCH); F GetStaticHeight (TerrainMgr.h:97).
inline float TerrainHeightStaticCompat(TerrainInfo const* terrain, float x, float y, float z)
{
    return const_cast<TerrainInfo*>(terrain)->GetStaticHeight(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(), x, y, z);
}

// T `terrain->GetWaterOrGroundLevel(x, y, z, &ground, swim)` (GridMap.h:151, a const call); F TerrainMgr.h:104 (not const, and
// the swim flag is unused there).
inline float TerrainWaterOrGroundLevelCompat(TerrainInfo const* terrain, float x, float y, float z, float* ground, bool swim)
{
    return const_cast<TerrainInfo*>(terrain)->GetWaterOrGroundLevel(PhasingHandler::GetEmptyPhaseShift(), terrain->GetId(), x, y, z, ground, swim);
}

// Where a creature began its fight, for the evade-chase policy (how far it may be pulled before it evades). T Creature.h:925
// GetCombatStartPosition. F records no combat start point; lead decision (findings item 11): the creature's HOME position
// (Creature.h:399 GetHomePosition), the point F itself measures a creature's leash from. DIFFERENT ANSWER: for a patrolling mob the bot's
// leash estimate is from its spawn point, not from where the pull began.
inline void CombatStartPositionCompat(Creature const* creature, float& x, float& y, float& z)
{
    Position const& home = creature->GetHomePosition();
    x = home.GetPositionX();
    y = home.GetPositionY();
    z = home.GetPositionZ();
}

// Seconds since the creature entered combat, -1 when unknown. T Creature.h:833 GetCombatTime (a time_t). F keeps no engage time
// (Creature::AtEngage is the only hook); lead decision (findings item 14): -1, the value CcMarkHoldPolicy.h already defines for
// "unknown" (the mark-hold window then stays closed, as for a creature the bot has not seen engage).
inline time_t CombatTimeCompat(Creature const* /*creature*/) { return time_t(-1); }

// Does an applied aura (T: the spell's aura holder) carry one of the mechanics in a legacy mask (bit `mechanic - 1`). T
// SpellAuraHolder::HasMechanicMask (SpellAuras.cpp:7392: the spell's mechanic, or the mechanic of an effect the holder applied); F
// SpellInfo::GetAllEffectsMechanicMask (SpellInfo.cpp:2800), whose bit is `mechanic` (not `mechanic - 1`), hence the shift. The mechanic ids
// are the same numbers on both (SLEEP 10). DIFFERENT ANSWER: F reads the spell's effects, not the effects this application applied.
inline bool AuraHasMechanicMaskCompat(Aura const* aura, uint32 legacyMask)
{
    return (aura->GetSpellInfo()->GetAllEffectsMechanicMask() & (uint64(legacyMask) << 1)) != 0;
}
