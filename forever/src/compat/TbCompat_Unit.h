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
inline bool HasNetworkTransportCompat(WorldSession const* session) { return session && !session->IsBot(); }

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
inline std::string const& CreatureNameCompat(CreatureTemplate const* tpl) { return tpl->Name; }

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
