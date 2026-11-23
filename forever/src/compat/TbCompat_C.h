// ForeverCore side of the both-core helpers and names of slice 1, round R4C (engine core, config, log, positions).
// Owned by package R4C: no other package edits this file. Turtle side: ai/cmangos-compat-r4c.h. Out-of-line parts: TbCompat_C.cpp.
//
// Included from forever-compat-shim.h after the R3 files and before TbCompat_Position.h, so `WorldLocation` still means the
// core's class in here. Same names and signatures as the Turtle file; there each one forwards to the call the bot always made.
// T = the Turtle core (~/playerbots-references/penqle-tortoise-wow-modsys/src), F = the pinned ForeverCore (~/ForeverCore/src).
#pragma once

#include "ReputationMgr.h"   // the reputation manager is only forward-declared by the pre-included set

// ---------------------------------------------------------------------------------------------------------------
// Logging. The bot's own `sLog` is BotLog::Instance() (playerbot.h); BotLog falls back to the core logger. T `Log::Instance()`,
// F's `sLog` macro is the printf proxy of the shim (the core's own TC_LOG macros need `sLog` to stay the core's, so BotLog.cpp
// asks for the proxy by this name instead of spelling `Log::Instance()`).
// ---------------------------------------------------------------------------------------------------------------
inline CmangosLogProxy LegacyLogCompat() { return CmangosLogProxy(); }

// ---------------------------------------------------------------------------------------------------------------
// Names as C strings. T `GetName()` is `char const*` (a null check was possible), F returns `std::string const&`.
// CStrCompat is R3's; this one is for the places that tested the pointer and printed "?" or "(none)".
// ---------------------------------------------------------------------------------------------------------------
inline char const* CStrOrCompat(char const* text, char const* fallback) { return text ? text : fallback; }
inline char const* CStrOrCompat(std::string const& text, char const* /*fallback*/) { return text.c_str(); }

// The 64-bit number a legacy hook or a typed-in qualifier hands over, as a guid. Only the empty guid (0) has a ForeverCore form;
// any other number cannot be a 128-bit guid, so the caller gets the empty guid and one error line (TbCompat_C.cpp): a refusal
// that is loud once, never a made-up guid.
ObjectGuid GuidFromRawCompat(uint64 raw);

// A guid for "this kind of thing, this template entry, this number", as GuidPosition(HIGHGUID_UNIT, entry) builds it to ask
// about a template (faction, name). T packs the three numbers (ObjectGuid.h:127: nothing when the counter is 0). F makes a world
// object guid with the entry and the counter and map id 0, or a plain database-id guid for a player or item (no entry in those).
// It is a guid-shaped record of the three numbers: it names no live object.
inline ObjectGuid GuidFromNumbersCompat(HighGuid hi, uint32 entry, uint32 counter)
{
    if (!counter)
        return ObjectGuid::Empty;
    switch (hi)
    {
        case HighGuid::Player:
            return ObjectGuid::Create<HighGuid::Player>(counter);
        case HighGuid::Item:
            return ObjectGuid::Create<HighGuid::Item>(counter);
        default:
            return ObjectGuidFactory::CreateWorldObject(hi, 0, 0, 0, 0, entry, counter);
    }
}

// The text form of a guid inside a GuidPosition string (GuidPosition::to_string and its string constructor read and write the
// same form). T one decimal number. F two decimal numbers, "high/low", the two halves of the 128 bits (SetRawValue keeps them
// apart): lossless, but not readable by the Turtle build and the other way round.
inline void GuidWriteCompat(std::ostream& out, ObjectGuid const& guid) { out << guid.GetRawValue(1) << '/' << guid.GetRawValue(0); }
inline ObjectGuid GuidReadCompat(std::istream& in)
{
    uint64 high = 0, low = 0;
    char slash = 0;
    in >> high >> slash >> low;
    ObjectGuid guid;
    if (in && slash == '/')
        guid.SetRawValue(high, low);
    return guid;
}

// ---------------------------------------------------------------------------------------------------------------
// Names for constants and settings.
// ---------------------------------------------------------------------------------------------------------------
// "Either team", the value ChatHelper::parseTeam returns for an unknown word. T SharedDefines maps it to team 0 in the shim
// (ai/cmangos-compat-shim.h:173); F's name for team 0 is TEAM_OTHER and R3 made TEAM_NONE mean it.
#define TEAM_BOTH_ALLOWED TEAM_NONE

// Server settings by their Turtle names (T World.h eConfig*Values, F World.h WorldIntConfigs/WorldBoolConfigs).
inline bool WorldBoolConfigCompat(WorldBoolConfigs index) { return sWorld.getBoolConfig(index); }

// The highest level a character can reach. T `DEFAULT_MAX_LEVEL` is the compile-time 60 (SharedDefines.h); F has no such
// constant (the level cap is the setting MaxPlayerLevel, CONFIG_MAX_PLAYER_LEVEL, World.h:288).
inline uint32 MaxLevelCompat() { return sWorld.getIntConfig(CONFIG_MAX_PLAYER_LEVEL); }

// ---------------------------------------------------------------------------------------------------------------
// Players, units.
// ---------------------------------------------------------------------------------------------------------------
// The zone a world object stands in. T `Object::getZoneId()` (cached), F `WorldObject::GetZoneId()` (Object.h:332, cached).
inline uint32 ZoneIdCompat(WorldObject const* wo) { return wo->GetZoneId(); }

// Whether the player is waiting in a battleground queue. T `Player::InBattleGroundQueue()`, F `InBattlegroundQueue(ignoreArena)`
// (Player.h:2719); arenas do not exist on the Turtle core, so the default (arenas count too) is the same question.
inline bool InBattleGroundQueueCompat(Player const* player) { return player->InBattlegroundQueue(); }

// Whether the guid waits in a dungeon finder queue. T `sLFGMgr.IsPlayerInQueue`; F asks the finder for the state (LFGMgr.h:355),
// "queued" being the state of a player on the list.
inline bool IsPlayerInLfgQueueCompat(ObjectGuid const& guid) { return sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED; }

// Whether the category cooldown of a spell is running. T `Player::HasSpellCategoryCooldown`, F SpellHistory (SpellHistory.h:170,
// a category cooldown is the remaining category time).
inline bool HasSpellCategoryCooldownCompat(Player const* player, uint32 category)
{
    return player->GetSpellHistory()->GetRemainingCategoryCooldown(category) > SpellHistory::Duration::zero();
}

// Turn a unit to an angle now and tell the clients. T sets the orientation and sends a movement heartbeat
// (`Unit::SendHeartBeat`); F has no such call (the heartbeat belongs to the movement generators) and turns a unit with a facing
// spline, `SetFacingTo(ori, force)` (Unit.h:1218), which sets the orientation at its end and tells the clients itself.
inline void SetOrientationAndSendCompat(Unit* unit, float angle) { unit->SetFacingTo(angle, true); }

// The unit a chase or follow movement generator is going after (nullptr when the current generator is neither). T moved the
// block out of ServerFacade.cpp word for word (the generators are templated on the mover); F's two generators are plain
// classes with the target in the follower base (FollowMovementGenerator.h:32, ChaseMovementGenerator.h:31). Out of line
// (TbCompat_C.cpp) because the two generator headers are not in the pre-included set.
Unit* TargetedMovementTargetCompat(Unit* unit);

// ---------------------------------------------------------------------------------------------------------------
// Auras, for the action log's dump of what a bot carries. T keeps a map of aura holders by spell id and 64 visible slots
// as unit fields; F keeps the applied auras in a multimap by spell id (Unit.h:1335) and the visible ones in a set ordered by slot.
// ---------------------------------------------------------------------------------------------------------------
inline std::vector<uint32> AuraSpellIdsCompat(Unit const* unit)
{
    std::vector<uint32> ids;
    for (auto const& pair : unit->GetAppliedAuras())
        ids.push_back(pair.first);
    return ids;
}
inline std::vector<std::pair<uint32, uint32>> VisibleAuraSlotsCompat(Unit const* unit)
{
    std::vector<std::pair<uint32, uint32>> slots;
    for (AuraApplication const* application : unit->GetVisibleAuras())
        slots.emplace_back(uint32(application->GetSlot()), application->GetBase()->GetId());
    return slots;
}

// ---------------------------------------------------------------------------------------------------------------
// Item classes. The AhBot tables (PlayerbotAIConfig.h:217, runtime/policy/ah/AhBotItemPoolPolicy.h:46) hold the 16 classes of the
// 1.12 data and every reader tests `itemClass >= kAhBotMaxItemClass` first. T `MAX_ITEM_CLASS` is that 16
// (Objects/ItemPrototype.h:164); F's MAX_ITEM_CLASS is 21 (ItemTemplate.h:497, the later classes too), so the legacy count
// is spelled out: an item of a class from 16 up is not priced, as the guard in the readers says.
// ---------------------------------------------------------------------------------------------------------------
inline constexpr uint32 LegacyItemClassCountCompat = 16;

// ---------------------------------------------------------------------------------------------------------------
// Factions and reputation. T FactionEntry/FactionTemplateEntry have lower-case fields and DBC strings by locale index; F's
// DB2 rows have capitalised fields and a LocalizedString (DB2Structure.h:1678, :1712).
// ---------------------------------------------------------------------------------------------------------------
// The faction a faction template belongs to. T `->faction`, F `->Faction`.
inline uint32 FactionOfTemplateCompat(FactionTemplateEntry const* faction) { return faction->Faction; }
// The faction row by id. T `sObjectMgr.GetFactionEntry`, F the DB2 store.
inline FactionEntry const* FactionEntryOfCompat(uint32 id) { return sFactionStore.LookupEntry(id); }
// A faction's name in the locale index the bot's text manager gives (0 = enUS). T `->name[loc]`, F `->Name[locale]`.
inline std::string FactionNameCompat(FactionEntry const* faction, int locale) { return faction->Name[LocaleConstant(locale)]; }

// What a player's reputation list says about one faction: whether it holds an entry, shows it, and has it at war. T
// FactionState::Flags is a plain word (FACTION_FLAG_VISIBLE 0x01, FACTION_FLAG_AT_WAR 0x02; ReputationMgr.h:32-33), F an
// EnumFlag of ReputationFlags with the same two bits (ReputationMgr.h:36-37).
struct ReputationStateCompat
{
    bool known = false;
    bool visible = false;
    bool atWar = false;
};
inline ReputationStateCompat ReputationStateOfCompat(Player const* player, FactionEntry const* faction)
{
    ReputationStateCompat state;
    if (FactionState const* entry = player->GetReputationMgr().GetState(faction))
    {
        state.known = true;
        state.visible = entry->Flags.HasFlag(ReputationFlags::Visible);
        state.atWar = entry->Flags.HasFlag(ReputationFlags::AtWar);
    }
    return state;
}
inline ReputationRank ReputationRankCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().GetRank(faction); }
inline int32 ReputationValueCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().GetReputation(faction); }
inline bool ReputationAtWarCompat(Player const* player, FactionEntry const* faction) { return player->GetReputationMgr().IsAtWar(faction); }
// The rank the core forces for a faction template (a scripted or aura-forced reaction), nullptr when none.
inline ReputationRank const* ForcedReputationRankCompat(Player const* player, FactionTemplateEntry const* faction)
{
    return player->GetReputationMgr().GetForcedRankIfAny(faction);
}

// Flags of a unit and a player. T `HasFlag(UNIT_FIELD_FLAGS, mask)` / `HasFlag(PLAYER_FLAGS, mask)` on the object's update fields,
// F `HasUnitFlag` / `HasPlayerFlag` on the update data (Unit.h:872, Player.h:3049). Same bits by name.
inline bool IsPlayerControlledCompat(Unit const* unit) { return unit->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED); }
inline bool IsContestedPvpCompat(Player const* player) { return player->HasPlayerFlag(PLAYER_FLAGS_CONTESTED_PVP); }

// The faction a game object template carries. T `GameObjectInfo::faction`, F keeps it in the template's addon row
// (ObjectMgr.h:1116), 0 when there is none.
inline uint32 GameObjectFactionCompat(GameObjectTemplate const* tpl)
{
    GameObjectTemplateAddon const* addon = sObjectMgr.GetGameObjectTemplateAddon(tpl->entry);
    return addon ? addon->Faction : 0;
}

// A teleport location's point. T GameTele::x/y/z, F position_x/y/z (ObjectMgr.h:156).
inline void GameTelePointCompat(GameTele const* tele, float& x, float& y, float& z)
{
    x = tele->position_x;
    y = tele->position_y;
    z = tele->position_z;
}

// ---------------------------------------------------------------------------------------------------------------
// Maps, creatures.
// ---------------------------------------------------------------------------------------------------------------
// The creature or pet a guid names, looked up in one map. T `Map::GetAnyTypeCreature`, F has `GetCreature` for creatures and
// vehicles and `GetPet` for pets (Map.h:446-449; R3's GetWorldObjectCompat splits them the same way).
inline Creature* GetAnyTypeCreatureCompat(Map* map, ObjectGuid const& guid)
{
    return guid.IsPet() ? static_cast<Creature*>(map->GetPet(guid)) : map->GetCreature(guid);
}
// Whether the grid under a point is loaded. T `Map::IsLoaded(x, y)`, F `Map::IsGridLoaded(x, y)` (Map.h:281).
inline bool MapIsLoadedCompat(Map const* map, float x, float y) { return map->IsGridLoaded(x, y); }

// ---------------------------------------------------------------------------------------------------------------
// Names and texts for chat links (ChatHelper.cpp). The locale index the bot's text manager hands out (0 = enUS, 1 = koKR, 2 = frFR,
// 3 = deDE, 4 = zhCN, 5 = zhTW, 6 = esES, 7 = esMX, 8 = ruRU) is the same number as F's LocaleConstant for those nine locales
// (Common.h LocaleConstant, Common.h:105 for the string type).
// ---------------------------------------------------------------------------------------------------------------
// A race's name and id. T ChrRacesEntry::name[locale] / RaceID, F Name[locale] / ID (DB2Structure.h:865).
inline char const* RaceNameCompat(ChrRacesEntry const* race, int locale) { return race->Name[LocaleConstant(locale)]; }
inline uint32 RaceIdCompat(ChrRacesEntry const* race) { return race->ID; }
// A skill line's name. T SkillLineEntry::name[locale], F DisplayName[locale] (DB2Structure.h:3696).
inline char const* SkillNameCompat(SkillLineEntry const* skill, int locale) { return skill->DisplayName[LocaleConstant(locale)]; }
// A player's skill numbers. T GetSkillValuePure / GetSkillMaxPure / GetSkillBonusPermanent / GetSkillBonusTemporary, F
// GetPureSkillValue / GetPureMaxSkillValue / GetSkillPermBonusValue / GetSkillTempBonusValue (Player.h:2477-2482).
inline uint32 SkillValuePureCompat(Player const* player, uint32 skill) { return player->GetPureSkillValue(skill); }
inline uint32 SkillMaxPureCompat(Player const* player, uint32 skill) { return player->GetPureMaxSkillValue(skill); }
inline int32 SkillBonusPermanentCompat(Player const* player, uint32 skill) { return player->GetSkillPermBonusValue(skill); }
inline int32 SkillBonusTemporaryCompat(Player const* player, uint32 skill) { return player->GetSkillTempBonusValue(skill); }

// A text of the server's string table by id and locale index. T `sObjectMgr.GetMangosString`, F `GetTrinityString` (ObjectMgr.h:1594).
// Both tables are the core's own: the ids are the shared names of Language.h (LANG_REP_*), the words each core's own.
inline char const* MangosStringCompat(uint32 id, int locale) { return sObjectMgr.GetTrinityString(id, LocaleConstant(locale)); }

// Whether a string holds digits only (empty counts). T shared/Util.h:358 takes a std::string, F's isNumeric takes a C string
// (Util.h:217); same loop.
inline bool IsNumericCompat(std::string const& text) { return isNumeric(text.c_str()); }

// A quest's title in a locale index (locale < 0: the base text, as the bot passes when no locale is wanted), then the quest level
// the 1.12 client shows. T `GetTitle()` and `QuestLocale::Title[]`, F `GetLogTitle()` and `QuestTemplateLocale::LogTitle[]`
// (QuestDef.h:440,673); T `GetQuestLevel()`, F `GetClassicQuestLevel()` (QuestDef.h:639), the classic data's level, 0 for a quest it does not carry.
inline std::string QuestTitleCompat(Quest const* quest, int locale)
{
    std::string title = quest->GetLogTitle();
    if (locale >= 0)
        if (QuestTemplateLocale const* texts = sObjectMgr.GetQuestLocale(quest->GetQuestId()))
            if (static_cast<size_t>(locale) < texts->LogTitle.size() && !texts->LogTitle[locale].empty())
                title = texts->LogTitle[locale];
    return title;
}
inline int32 QuestLevelCompat(Quest const* quest) { return quest->GetClassicQuestLevel(); }

// An item's name in a locale index, `fallback` when there is no localised text. T the `ItemLocale` row of the item, F
// `ItemTemplate::GetName(locale)` (ItemTemplate.h:905).
inline std::string ItemLocalizedNameCompat(uint32 itemId, int locale, std::string const& fallback)
{
    if (locale >= 0)
        if (ItemTemplate const* tpl = sObjectMgr.GetItemTemplate(itemId))
            if (char const* name = tpl->GetName(LocaleConstant(locale)))
                if (*name)
                    return name;
    return fallback;
}

// ---------------------------------------------------------------------------------------------------------------
// WorldPosition.cpp: areas, spawn data, creature templates, terrain, line of sight, transports, grids, paths.
// T = the Turtle core, F = ForeverCore.
// ---------------------------------------------------------------------------------------------------------------

// ---- Areas ----------------------------------------------------------------------------------------------------
// The area row a position's area KEY names (the key is what AreaKeyAtCompat, R3, returns). T keys a per-map area FLAG:
// AreaEntry::GetByAreaFlagAndMap(flag, map) walks the table for the row whose explore flag matches (the one on this map, else
// any, else the map's linked zone; Maps/Map.h:152-169). F's key is the area ID and the row is the DB2 row of that id (0 gives none).
// The key is therefore not comparable between the cores, only within one.
inline AreaTableEntry const* AreaEntryOfKeyCompat(uint32 key, uint32 /*mapId*/) { return sAreaTableStore.LookupEntry(key); }
// The zone and area ids an area key stands for (0, 0 for an unknown key). T `TerrainManager::GetZoneAndAreaIdByAreaFlag(zone, area, flag,
// map)` (Maps/GridMap.cpp:1316): the area is the row's id, the zone the row's parent zone or the row itself when it is a zone. F reads the same
// from the DB2 row of the id. Offered for the callers outside R4C that look a key up (TravelMgr.cpp:2775, FishAction.cpp:561, GuildOrderSources.cpp:123).
inline void ZoneAndAreaIdOfKeyCompat(uint32& zoneId, uint32& areaId, uint16 key, uint32 mapId)
{
    AreaTableEntry const* area = AreaEntryOfKeyCompat(key, mapId);
    areaId = area ? area->ID : 0;
    zoneId = area ? (area->ParentAreaID ? area->ParentAreaID : area->ID) : 0;
}
// The row's own numbers. T AreaEntry::Id / ZoneId (0 when the area is itself a zone) / Team (AREATEAM_*: 2 Alliance, 4 Horde,
// 6 both) / Name (char*); F ID / ParentAreaID (0 when it has no parent) / FactionGroupMask (the same 2 and 4 bits) / AreaName (DB2Structure.h:125).
inline uint32 AreaIdOfCompat(AreaTableEntry const* area) { return area->ID; }
inline uint32 AreaTeamCompat(AreaTableEntry const* area) { return area->FactionGroupMask; }
// A map's name. T MapEntry::name (char*), F MapName (DB2Structure.h:2911).
inline std::string MapNameOrEmptyCompat(MapEntry const* map)
{
    char const* name = map->MapName[LOCALE_enUS];
    return name ? name : "";
}

// Guid type tag of a map object transport (a boat or zeppelin, a `Transport`). T HIGHGUID_MO_TRANSPORT, F HighGuid::Transport (ObjectGuid.h:
// `IsMOTransport()` is that tag). T's HIGHGUID_TRANSPORT (a game object based transport) has no ForeverCore tag and is NOT defined.
#define HIGHGUID_MO_TRANSPORT HighGuid::Transport

// ---- Spawn data -----------------------------------------------------------------------------------------------
// Creature and game object spawn records by their database id, and the walk over all of them. T ObjectMgr::DoCreatureData /
// DoGOData stop when the worker answers true (ObjectMgr.h:1116-1122); the pair type is the shim's (key = spawn id).
// The id here is a DATABASE spawn id, never a guid counter.
template <class Worker>
inline void DoCreatureDataCompat(Worker& worker)
{
    for (auto const& pair : sObjectMgr.GetAllCreatureData())
        if (worker(pair))
            break;
}
template <class Worker>
inline void DoGameObjectDataCompat(Worker& worker)
{
    for (auto const& pair : sObjectMgr.GetAllGameObjectData())
        if (worker(pair))
            break;
}
// The game object a SPAWN record is, if it is loaded on the map. T builds the guid from entry and spawn id, F asks the map by spawn id.
inline GameObject* GetGameObjectBySpawnIdCompat(Map* map, uint32 /*entry*/, uint64 spawnId) { return map->GetGameObjectBySpawnId(spawnId); }
// The guid a creature spawn record stands for, built the way the core builds it for that spawn: a world-object guid of the
// spawn's map, its template entry and its database spawn id as the counter (`ObjectGuid::Create<HighGuid::Creature>`,
// ObjectGuid.h:405; the same value is the dbtable guid of Creature.cpp:810). The counter is the key of
// GetAllCreatureData(), so GetCreatureData(GetCounter()) reads this record back (ObjectMgr.h:1504). It names the SPAWN,
// not the live object: a loaded creature gets its own map-generated low guid, so this is not a live-object lookup key.
template <class DataPair>
inline ObjectGuid GuidOfCreatureDataCompat(DataPair const* dataPair)
{
    CreatureData const& data = dataPair->second;
    return ObjectGuid::Create<HighGuid::Creature>(uint16(data.mapId), data.id, dataPair->first);
}

// ---- Creature templates ---------------------------------------------------------------------------------------
// Whether the template is a civilian. T `CreatureInfo::civilian`, F the extra flag the core itself reads in `Creature::IsCivilian`
// (Creature.h:126, CREATURE_FLAG_EXTRA_CIVILIAN: "not aggro").
inline bool CreatureIsCivilianCompat(CreatureInfo const* info) { return (info->flags_extra & CREATURE_FLAG_EXTRA_CIVILIAN) != 0; }
// Whether the template is a guard. T flags_extra bit 0x400 (CREATURE_FLAG_EXTRA_GUARD, Creature.h:62), F the extra flag of the same
// name at 0x8000 (CreatureData.h:359): the number differs, the flag does not.
inline bool CreatureIsGuardFlagCompat(CreatureInfo const* info) { return (info->flags_extra & CREATURE_FLAG_EXTRA_GUARD) != 0; }
// Whether the template never aggroes by design: an invisible trigger (T INVISIBLE 0x80, F TRIGGER 0x80) or, on T only, a "no
// target" spawn (T NO_TARGET 0x20000; F's 0x20000 is NO_CRIT and it has no flag for an untargetable creature).
inline bool CreatureNeverAggrosCompat(CreatureInfo const* info) { return (info->flags_extra & CREATURE_FLAG_EXTRA_TRIGGER) != 0; }
// Whether the template is more than a normal creature (elite, rare elite, boss, rare). T `rank > CREATURE_ELITE_NORMAL`, F the
// classification (SharedDefines.h:5181): Elite, RareElite, Obsolete (T's world boss, 3) and Rare count; Trivial and MinusMob (5, 6) are
// below normal and do not. R4M: a template whose DIFFICULTY_NONE row carries the boss type flag counts too, which is the fact
// `CreatureRankCompat` (V, common) calls a world boss on F, so "above normal" and "world boss" cannot disagree about one creature.
inline bool CreatureIsAboveNormalCompat(CreatureInfo const* info)
{
    switch (info->Classification)
    {
        case CreatureClassifications::Elite:
        case CreatureClassifications::RareElite:
        case CreatureClassifications::Obsolete:
        case CreatureClassifications::Rare:
            return true;
        default:
            break;
    }
    CreatureDifficulty const* difficulty = info->GetDifficulty(DIFFICULTY_NONE);
    return difficulty && (difficulty->TypeFlags & CREATURE_TYPE_FLAG_BOSS_MOB) != 0;
}
// Whether one faction template is hostile to another. T takes the other by reference, F by pointer (DB2Structure.h:1726-).
inline bool FactionTemplateHostileToCompat(FactionTemplateEntry const* from, FactionTemplateEntry const* to) { return from->IsHostileTo(to); }

// ---- Randomness -----------------------------------------------------------------------------------------------
// A random float in [0, 1). T `rand_norm_f()`, F `rand_norm()` (Random.h:45).
inline float RandNormCompat() { return rand_norm(); }

// ---- Static line of sight and hit positions in the vmaps; map and navmesh loading -----------------------------------
// T `VMapFactory::createOrGetVMapManager()->isInLineOfSight(map, ...)`, F the same call with the ignore flags (nothing ignored).
bool VmapLineOfSightCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2);
// T `getObjectHitPos(map, from, to, hit, modifyDist)`, F the same call (VMapManager.h:158). True when something was hit.
bool VmapObjectHitPosCompat(uint32 mapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float& ry, float& rz, float modifyDist);
// Whether the vmap tile loaded (or already was). T `loadMap(dataPath, map, x, y)` is a bool, F's returns LoadResult (VMapManager.h:147).
bool VmapLoadTileCompat(char const* dataPath, uint32 mapId, int x, int y);
// Whether the navmesh of a map is loaded. T `MMapManager::GetNavMesh(map) != nullptr` (one mesh per map), F per instance id
// (MMapManager.h:72); the callers pass 0 for the continents and the instance id for the rest.
bool NavMeshLoadedCompat(uint32 mapId, uint32 instanceId);
// Load one navmesh tile. T `loadMap(map, x, y)`, F `loadMap(basePath, map, instanceId, x, y)` returning LoadResult; loaded or already loaded is true.
bool NavMeshLoadTileCompat(uint32 mapId, uint32 instanceId, int x, int y);

// ---- Grid and cell coordinates --------------------------------------------------------------------------------
// T's CoordPair steps with operators (`>> n` x up, `<< n` x down, `+= n` y up, `-= n` y down, all clamped to [0, LIMIT-1],
// Maps/GridDefines.h:105-135); F names the same four steps inc_x, dec_x, inc_y, dec_y (GridDefines.h:106-130).
template <class Pair> inline void CoordIncXCompat(Pair& pair, uint32 value) { pair.inc_x(value); }
template <class Pair> inline void CoordIncYCompat(Pair& pair, uint32 value) { pair.inc_y(value); }

// ---- Transports -----------------------------------------------------------------------------------------------
// The transport (boat, zeppelin, elevator object) a unit stands on. T `Unit::GetTransport()`, F returns the TransportBase of any
// carrier (a vehicle too), so only a Transport counts.
inline GenericTransport* TransportOfCompat(WorldObject const* object) { return dynamic_cast<GenericTransport*>(object->GetTransport()); }
// Every transport on a map id, all of them or the ones of one entry. T walks the transport holder, F the transports of each loaded map.
inline std::set<GenericTransport*> TransportsOnMapCompat(uint32 mapId, uint32 entry)
{
    std::set<GenericTransport*> transports;
    sMapMgr.DoForAllMapsWithMapId(mapId, [&](Map* map)
    {
        for (Transport* transport : map->GetTransports())
            if (transport && (!entry || transport->GetEntry() == entry))
                transports.insert(transport);
    });
    return transports;
}
// Local offset to world position and back, for a passenger. T `Transport::CalculatePassengerPosition(x, y, z, &o)` turns a position
// relative to the transport into a world one and `CalculatePassengerOffset` does the reverse (in place); F has the same pair as
// `GetPositionWithOffset` / `GetPositionOffsetTo` (VehicleDefines.h:176-179), which return the result.
inline void PassengerPositionCompat(GenericTransport* transport, float& x, float& y, float& z, float* o = nullptr)
{
    Position const world = transport->GetPositionWithOffset(Position(x, y, z, o ? *o : 0.0f));
    x = world.GetPositionX();
    y = world.GetPositionY();
    z = world.GetPositionZ();
    if (o)
        *o = world.GetOrientation();
}
inline void PassengerOffsetCompat(GenericTransport* transport, float& x, float& y, float& z, float* o = nullptr)
{
    Position const local = transport->GetPositionOffsetTo(Position(x, y, z, o ? *o : 0.0f));
    x = local.GetPositionX();
    y = local.GetPositionY();
    z = local.GetPositionZ();
    if (o)
        *o = local.GetOrientation();
}
