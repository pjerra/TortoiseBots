// cmangos names on ForeverCore (TrinityCore master). The counterpart of ai/cmangos-compat-shim.h.
//
// Read after every core header (see botpch_forever.h). Four kinds of entry, cheapest first. Add to the first kind
// that fits; a difference none of them can carry is a call-site change through the host API, not a new trick here.
//
//   1. TYPE NAMES     a type that is the same thing under another name: `using Old = New;`
//   2. SINGLETONS     cmangos writes `sObjectMgr.Foo()`, TrinityCore `sObjectMgr->Foo()`: the macro is redefined to
//                     the object itself, so the old spelling compiles.
//   3. MEMBER NAMES   a member function that only changed its name: `#define OldName NewName`. Only for names
//                     the core does not use for anything else.
//   4. FREE FUNCTIONS helpers cmangos had as free functions or macros, written here as inline functions.
//
// Every entry says what it stands for. Keep each section sorted by name.
#pragma once

#include <cstdio>
#include <tuple>

// ---------------------------------------------------------------------------------------------------------------
// 1. TYPE NAMES
// ---------------------------------------------------------------------------------------------------------------
using BattleGroundTypeId = BattlegroundTypeId;   // the battleground id enum; AV/WS/AB are 1/2/3 in both cores
using CellPair = CellCoord;                      // CoordPair<TOTAL_NUMBER_OF_CELLS_PER_MAP>, x_coord/y_coord fields
using Config = ConfigMgr;                        // the config store; cmangos names the class Config
using CreatureInfo = CreatureTemplate;           // the creature template record
using GameObjectInfo = GameObjectTemplate;       // the game object template record
using GenericTransport = Transport;              // a ship/zeppelin the player can stand on
using GridPair = GridCoord;                      // CoordPair<MAX_NUMBER_OF_GRIDS>, x_coord/y_coord fields
using GuidVector = std::vector<ObjectGuid>;      // a list of guids
using ObjectGuidSet = GuidSet;                   // a set of guids
using Movement::PointsArray;                     // the polyline of a path (std::vector<Vector3> in both cores)
using Movement::Vector3;                         // G3D::Vector3, the point type cmangos exposes unqualified

// The value type of the spawn stores: cmangos iterates `CreatureDataPair`/`GameObjectDataPair`, ForeverCore's
// GetAllCreatureData()/GetAllGameObjectData() hold the same pair keyed by the spawn id. The key is wider than
// cmangos's 32-bit one: it is ObjectGuid::LowType, 64 bit, the spawn id of the guid-based data model.
using CreatureDataPair = std::pair<ObjectGuid::LowType const, CreatureData>;
using GameObjectDataPair = std::pair<ObjectGuid::LowType const, GameObjectData>;

// A macro, not `using`: bot headers forward-declare `struct SpellEntry;`, and a type alias cannot be redeclared as a
// class. The macro renames the forward declaration with the rest (a class-keyed redeclaration of a defined class is
// legal). ForeverCore never uses the old name.
#define SpellEntry SpellInfo

// ItemPrototype is not an alias: the bot brain reads item data as public fields (`proto->Class`, `proto->ItemStat[i]`),
// which ItemTemplate hides behind accessors. It is our own struct with the old field names, filled from ItemTemplate once
// per item (TbItemPrototype.h/.cpp). Everything that FETCHES an item template hands back the mirror.
#include "TbItemPrototype.h"

// cmangos keeps its grid searchers and its singleton template in `MaNGOS`. The searchers are TrinityCore's
// (`Trinity::`), the singleton template is ours: one instance per type, made on first use.
namespace MaNGOS
{
    using namespace Trinity;

    template <typename T>
    class Singleton
    {
    public:
        static T& Instance()
        {
            static T instance;
            return instance;
        }

    protected:
        Singleton() = default;
    };

    // cmangos's names for the two coordinate computations; the formula and the result type are the core's.
    inline GridCoord ComputeGridPair(float x, float y) { return Trinity::ComputeGridCoord(x, y); }
    inline CellCoord ComputeCellPair(float x, float y) { return Trinity::ComputeCellCoord(x, y); }
}
#define INSTANTIATE_SINGLETON_1(TYPE)

// ---------------------------------------------------------------------------------------------------------------
// 2. SINGLETONS
// ---------------------------------------------------------------------------------------------------------------
#undef sObjectMgr
#define sObjectMgr (*ObjectMgr::instance())                 // templates, spawn data, quest store
#undef sWorld
#define sWorld (*World::instance())                         // server settings and rates
#undef sSpellMgr
#define sSpellMgr (*SpellMgr::instance())                   // spell chains, target positions, training
#undef sGuildMgr
#define sGuildMgr (*GuildMgr::instance())                   // guilds by id
#undef sMapMgr
#define sMapMgr (*MapManager::instance())                   // maps and instances
#undef sConfig
#define sConfig (*ConfigMgr::instance())                    // the configuration file store
#undef sBattleGroundMgr
#define sBattleGroundMgr (*BattlegroundMgr::instance())     // battleground templates, queues, instances

// sObjectAccessor: cmangos's accessor object; ForeverCore has the same lookups as free functions in the
// `ObjectAccessor` namespace. Each member below is the one call with the same reach:
//   FindPlayer            in-world player by guid            ObjectAccessor::FindPlayer
//   FindPlayerNotInWorld  player even while loading/teleporting ObjectAccessor::FindConnectedPlayer
//   FindPlayerByName      in-world player by name            ObjectAccessor::FindPlayerByName
//   FindPlayerPointer     as FindPlayerNotInWorld; there is no MasterPlayer on ForeverCore, so no second fallback
//   GetUnit / GetPlayers  same names, same reach             ObjectAccessor::GetUnit / GetPlayers
struct CmangosObjectAccessorProxy
{
    Player* FindPlayer(ObjectGuid guid) const { return ObjectAccessor::FindPlayer(guid); }
    Player* FindPlayerNotInWorld(ObjectGuid guid) const { return ObjectAccessor::FindConnectedPlayer(guid); }
    Player* FindPlayerByName(char const* name) const { return ObjectAccessor::FindPlayerByName(name); }
    Player* FindPlayerPointer(ObjectGuid guid) const { return ObjectAccessor::FindConnectedPlayer(guid); }
    Unit* GetUnit(WorldObject const& obj, ObjectGuid guid) const { return ObjectAccessor::GetUnit(obj, guid); }
    HashMapHolder<Player>::MapType const& GetPlayers() const { return ObjectAccessor::GetPlayers(); }
};
#undef sObjectAccessor
#define sObjectAccessor (CmangosObjectAccessorProxy())

// ---------------------------------------------------------------------------------------------------------------
// 3. MEMBER NAMES
// ---------------------------------------------------------------------------------------------------------------
#define GetCharmerGuid GetCharmerGUID                // guid of the unit that charmed this one
#define GetCreatureInfo GetCreatureTemplate          // ObjectMgr's creature template by entry
#define GetDeathState getDeathState                  // ALIVE/DEAD/CORPSE state
#define GetGameObjectInfo GetGameObjectTemplate      // ObjectMgr's game object template by entry
#define GetGOData GetGameObjectData                  // ObjectMgr's spawn record by spawn id
#define GetHealthPercent GetHealthPct                // health as a percentage of max, 0..100
#define GetLeaderGuid GetLeaderGUID                   // the group's leader
#define GetObjectBoundingRadius GetBoundingRadius    // the unit's size, added to reach computations
#define GetObjectGuid GetGUID                        // the object's guid
#define GetOwnerGuid GetOwnerGUID                    // guid of the summoner/owner
#define GetSelectionGuid GetTarget                   // the selected target guid (UF::UnitData::Target)
#define GetSpellProto GetSpellInfo                   // the spell an aura or a cast belongs to
#define GetTargetGuid GetTarget                      // the selected target guid (same field)
#define InBattleGround InBattleground                // player is inside a battleground instance
#define IsDead isDead                                // dead or a corpse
#define IsNonMeleeSpellCasted IsNonMeleeSpellCast    // a non-melee spell is being cast/channelled
#define IsSittingDown IsSitState                     // the five sitting stand states
#define SetSelectionGuid SetSelection                // set the selected target guid (Player only)

// Lower-case spellings the bot brain uses for core members (the same members, renamed by ForeverCore).
// (getName is not here: bot classes define both getName() and a GetName() forwarder, so a global rename would
// redeclare those members.)
#define getDistance2d GetDistance2d   // distance in the xy plane
#define getLevel GetLevel             // unit level
#define getObjectGuid GetGUID         // the object's guid
#define getOpcode GetOpcode           // a packet's opcode
#define getOrientation GetOrientation // facing, radians
#define getPositionX GetPositionX     // x coordinate
#define getPositionY GetPositionY     // y coordinate
#define getPositionZ GetPositionZ     // z coordinate
#define getThreat GetThreat           // a unit's threat on this target

// cmangos asked a unit for its database id; ForeverCore's guid keeps it in the counter. GetCounter() is 64 bit
// (ObjectGuid::LowType), the old value was 32 bit, so the number is the same but wider.
#define GetGUIDLow() GetGUID().GetCounter()

// TempSummonType: the same values under ForeverCore's TEMPSUMMON_ names (ObjectDefines.h:60-72).
#define TEMPSPAWN_CORPSE_DESPAWN TEMPSUMMON_CORPSE_DESPAWN                         // despawn right on death
#define TEMPSPAWN_CORPSE_TIMED_DESPAWN TEMPSUMMON_CORPSE_TIMED_DESPAWN             // despawn a while after death
#define TEMPSPAWN_DEAD_DESPAWN TEMPSUMMON_DEAD_DESPAWN                             // despawn when the corpse goes
#define TEMPSPAWN_MANUAL_DESPAWN TEMPSUMMON_MANUAL_DESPAWN                         // despawn on UnSummon()
#define TEMPSPAWN_TIMED_DESPAWN TEMPSUMMON_TIMED_DESPAWN                           // despawn after the given time
#define TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN       // ...or on death
#define TEMPSPAWN_TIMED_OR_DEAD_DESPAWN TEMPSUMMON_TIMED_OR_DEAD_DESPAWN           // ...or when it disappears

// AreaTable flags: the same DBC bits, named by ForeverCore's AreaFlags enum. Values are the client's.
#define AREA_FLAG_CAPITAL AreaFlags::LinkedChat              // 0x00000100, the capital-city bit
#define AREA_FLAG_CITY AreaFlags::LinkedChatSpecialArea      // 0x00000200
#define AREA_FLAG_SLAVE_CAPITAL AreaFlags::AllowTradeChannel // 0x00000008

// SpellMgr::GetSpellInfo needs the difficulty; cmangos asked for one spell id and got the single entry.
#define GetSpellEntry(spellId) GetSpellInfo((spellId), DIFFICULTY_NONE)

// The cooldown moved to SpellHistory on ForeverCore; the questions (is this spell cooling down / clear it now) are the
// same and take the same arguments.
#define HasSpellCooldown(spellId) GetSpellHistory()->HasCooldown(spellId)
#define RemoveSpellCooldown(...) GetSpellHistory()->ResetCooldown(__VA_ARGS__)

// ---------------------------------------------------------------------------------------------------------------
// 4. FREE FUNCTIONS AND PROXIES
// ---------------------------------------------------------------------------------------------------------------
// Bot files carry stand-in definitions behind these guards for tools that do not see the core headers (Lens). The
// real headers are in front of every file here, so the guard is set and the stand-ins stay out.
#define MANGOS_OBJECT_GUID_H 1
#define MANGOS_OBJECTACCESSOR_H 1

// ---- constants ----
// Guid type tags. ForeverCore's HighGuid has its own numbering, so these are the tags themselves, not numbers: every
// bot use compares them with ObjectGuid::GetHigh(), which returns HighGuid.
#define HIGHGUID_CORPSE HighGuid::Corpse                  // corpse guid tag
#define HIGHGUID_DYNAMICOBJECT HighGuid::DynamicObject    // dynamic object guid tag
#define HIGHGUID_GAMEOBJECT HighGuid::GameObject          // game object guid tag
#define HIGHGUID_ITEM HighGuid::Item                      // item guid tag
#define HIGHGUID_PET HighGuid::Pet                        // pet guid tag
#define HIGHGUID_PLAYER HighGuid::Player                  // player guid tag
#define HIGHGUID_UNIT HighGuid::Creature                  // creature guid tag

// Item classes and subclasses: the same client values, ForeverCore spells "miscellaneous" out.
#define ITEM_CLASS_MISC ITEM_CLASS_MISCELLANEOUS                    // 15
#define ITEM_SUBCLASS_ARMOR_MISC ITEM_SUBCLASS_ARMOR_MISCELLANEOUS  // 0
#define ITEM_SUBCLASS_FOOD ITEM_SUBCLASS_FOOD_DRINK                 // 5
#define ITEM_SUBCLASS_WEAPON_FIST ITEM_SUBCLASS_WEAPON_FIST_WEAPON  // 13
#define ITEM_SUBCLASS_WEAPON_MISC ITEM_SUBCLASS_WEAPON_MISCELLANEOUS // 14

// Unit flag: cmangos's NOT_SELECTABLE is ForeverCore's UNINTERACTIBLE, both 0x02000000.
#define UNIT_FLAG_NOT_SELECTABLE UNIT_FLAG_UNINTERACTIBLE

// Race: the same race id 5.
#define RACE_UNDEAD RACE_UNDEAD_PLAYER

// Sizes: the nine classic locales. (MAX_ITEM_PROTO_SPELLS and MAX_ITEM_PROTO_DAMAGES are the mirror's array sizes,
// defined in TbItemPrototype.h: 5 slots in both cores.)
#define MAX_LOCALE OLD_TOTAL_LOCALES

// Config keys: ForeverCore's enum drops the type token from cmangos's name (same key, same value type).
#define CONFIG_BOOL_ADDON_CHANNEL CONFIG_ADDON_CHANNEL
#define CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION
#define CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP
#define CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD
#define CONFIG_BOOL_INSTANCE_IGNORE_LEVEL CONFIG_INSTANCE_IGNORE_LEVEL
#define CONFIG_BOOL_INSTANCE_IGNORE_RAID CONFIG_INSTANCE_IGNORE_RAID
#define CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS
#define CONFIG_FLOAT_LISTEN_RANGE_YELL CONFIG_LISTEN_RANGE_YELL
#define CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF
#define CONFIG_UINT32_CHARACTERS_PER_ACCOUNT CONFIG_CHARACTERS_PER_ACCOUNT
#define CONFIG_UINT32_CHARACTERS_PER_REALM CONFIG_CHARACTERS_PER_REALM
#define CONFIG_UINT32_MAX_PLAYER_LEVEL CONFIG_MAX_PLAYER_LEVEL
#define CONFIG_UINT32_MIN_PETITION_SIGNS CONFIG_MIN_PETITION_SIGNS
#define CONFIG_UINT32_SKILL_CHANCE_GREEN CONFIG_SKILL_CHANCE_GREEN
#define CONFIG_UINT32_SKILL_CHANCE_GREY CONFIG_SKILL_CHANCE_GREY
#define CONFIG_UINT32_SKILL_CHANCE_ORANGE CONFIG_SKILL_CHANCE_ORANGE
#define CONFIG_UINT32_SKILL_CHANCE_YELLOW CONFIG_SKILL_CHANCE_YELLOW
#define CONFIG_UINT32_SKILL_GAIN_CRAFTING CONFIG_SKILL_GAIN_CRAFTING

// ---- free functions ----
// The area record by id; ForeverCore keeps it in the area table store.
inline AreaTableEntry const* GetAreaEntryByAreaID(uint32 id) { return sAreaTableStore.LookupEntry(id); }

// cmangos had these as free functions over a SpellEntry; ForeverCore has the same answers as SpellInfo members.
inline uint32 GetSpellCastTime(SpellInfo const* spellInfo, Spell const* spell = nullptr)
{
    return spellInfo ? spellInfo->CalcCastTime(const_cast<Spell*>(spell)) : 0;
}
inline int32 GetSpellDuration(SpellInfo const* spellInfo) { return spellInfo ? spellInfo->GetDuration() : 0; }
inline uint32 GetSpellRecoveryTime(SpellInfo const* spellInfo) { return spellInfo ? spellInfo->GetRecoveryTime() : 0; }
inline SpellSchoolMask GetSpellSchoolMask(SpellInfo const* spellInfo)
{
    return spellInfo ? spellInfo->GetSchoolMask() : SPELL_SCHOOL_MASK_NONE;
}
inline bool IsChanneledSpell(SpellInfo const* spellInfo) { return spellInfo && spellInfo->IsChanneled(); }
inline bool IsPassiveSpell(SpellInfo const* spellInfo) { return spellInfo && spellInfo->IsPassive(); }
inline bool IsPositiveEffect(SpellInfo const* spellInfo, uint8 effIndex)
{
    return spellInfo && spellInfo->IsPositiveEffect(effIndex);
}
inline bool IsPositiveSpell(SpellInfo const* spellInfo) { return spellInfo && spellInfo->IsPositive(); }

// The item's template. cmangos asked the item (`item->GetProto()`); ForeverCore names the fetch after the template, and
// the bot brain then reads the old fields, so here the template is turned into the mirror (TbItemPrototype.h). The same
// spelling exists in ai/cmangos-compat-shim.h, so the call sites read the same on both cores. A null item was a crash
// before and is nullptr now.
inline ItemPrototype const* GetItemProtoCompat(Item const* item) { return item ? tbcompat::ItemProto(item->GetTemplate()) : nullptr; }

// Back to the core's record for the core functions that want an ItemTemplate const* and are handed a mirror.
inline ItemTemplate const* GetItemTemplateCompat(ItemPrototype const* proto) { return tbcompat::ItemTpl(proto); }

// ---- SQL store objects ----
// cmangos keeps items, creature templates and game object templates in store objects with LookupEntry/GetMaxEntry;
// ForeverCore asks ObjectMgr for the same records. GetMaxEntry is "one past the largest id used as a loop bound"
// (every bot caller loops `id < GetMaxEntry()` and takes nullptr for a hole); the bound is cached for the loaded
// store and refreshed when the number of records changes.
template <typename Container>
class CmangosMaxEntryCache
{
public:
    uint32 Get(Container const& store) const
    {
        if (store.size() != _size)
        {
            uint32 upper = 0;
            for (auto const& entry : store)
                upper = std::max(upper, entry.first + 1);
            _size = store.size();
            _upper = upper;
        }
        return _upper;
    }

private:
    mutable size_t _size = std::numeric_limits<size_t>::max();
    mutable uint32 _upper = 0;
};

struct CmangosItemStorageProxy
{
    // cmangos returned its own ItemPrototype; the bot brain reads the old fields, so the proxy hands back our mirror.
    template <typename T = ItemPrototype>
    T const* LookupEntry(uint32 id) const { return tbcompat::ItemProto(id); }
    uint32 GetMaxEntry() const { return _max.Get(sObjectMgr.GetItemTemplateStore()); }

private:
    mutable CmangosMaxEntryCache<ItemTemplateContainer> _max;
};
inline CmangosItemStorageProxy sItemStorage;

struct CmangosCreatureStorageProxy
{
    template <typename T = CreatureTemplate>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetCreatureTemplate(id); }
    uint32 GetMaxEntry() const { return _max.Get(sObjectMgr.GetCreatureTemplates()); }

private:
    mutable CmangosMaxEntryCache<CreatureTemplateContainer> _max;
};
inline CmangosCreatureStorageProxy sCreatureStorage;

struct CmangosGOStorageProxy
{
    template <typename T = GameObjectTemplate>
    T const* LookupEntry(uint32 id) const { return sObjectMgr.GetGameObjectTemplate(id); }
    uint32 GetMaxEntry() const { return _max.Get(sObjectMgr.GetGameObjectTemplates()); }

private:
    mutable CmangosMaxEntryCache<GameObjectTemplateContainer> _max;
};
inline CmangosGOStorageProxy sGOStorage;

#ifndef M_PI_F
#define M_PI_F float(M_PI)
#endif

// cmangos WorldTimer: milliseconds since the server started.
struct WorldTimer
{
    static uint32 getMSTime() { return ::getMSTime(); }
    static uint32 getMSTimeDiff(uint32 oldTime, uint32 newTime) { return ::getMSTimeDiff(oldTime, newTime); }
};

// sSpellTemplate: cmangos's spell DBC store object. ForeverCore's SpellMgr answers the same question; the spell
// data for the base difficulty is the one entry cmangos had. (GetMaxEntry has no SpellMgr counterpart and is left
// to the call sites.)
struct CmangosSpellTemplateProxy
{
    template <typename T = SpellInfo>
    T const* LookupEntry(uint32 id) const { return sSpellMgr.GetSpellInfo(id, DIFFICULTY_NONE); }
};
#undef sSpellTemplate
#define sSpellTemplate (CmangosSpellTemplateProxy())

// sLog: cmangos logged with printf formats through `sLog.outX(...)`. ForeverCore logs through `sLog->OutMessage`
// with a named logger and std::format placeholders: "server" is the server log and "sql.sql" the DB-error log, the
// two files cmangos wrote those levels to. The proxy formats the old line here and hands it to the core logger as
// one argument, so sink, text and severity stay the same. Level: cmangos basic/string/info -> INFO, detail/debug
// -> DEBUG (both hidden at the shipped Logger.server=3, as cmangos's log level 2/3 is), error -> ERROR.
struct CmangosLogProxy
{
    void outBasic() const { LogAt(LOG_LEVEL_INFO, "server", ""); }
    template <typename... Args> void outBasic(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }
    void outDebug() const { LogAt(LOG_LEVEL_DEBUG, "server", ""); }
    template <typename... Args> void outDebug(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_DEBUG, "server", Format(fmt, args...)); }
    void outDetail() const { LogAt(LOG_LEVEL_DEBUG, "server", ""); }
    template <typename... Args> void outDetail(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_DEBUG, "server", Format(fmt, args...)); }
    void outError() const { LogAt(LOG_LEVEL_ERROR, "server", ""); }
    template <typename... Args> void outError(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_ERROR, "server", Format(fmt, args...)); }
    void outErrorDb() const { LogAt(LOG_LEVEL_ERROR, "sql.sql", ""); }
    template <typename... Args> void outErrorDb(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_ERROR, "sql.sql", Format(fmt, args...)); }
    void outInfo() const { LogAt(LOG_LEVEL_INFO, "server", ""); }
    template <typename... Args> void outInfo(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }
    void outString() const { LogAt(LOG_LEVEL_INFO, "server", ""); }
    template <typename... Args> void outString(char const* fmt, Args const&... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }

private:
    static void LogAt(LogLevel level, char const* logger, std::string const& message)
    {
        Log::instance()->OutMessage(logger, level, "{}", message);
    }

    // printf formatting of the old line. A std::string argument reaches %s as its characters: ForeverCore returns
    // std::string where cmangos returned char const*. The copies in the tuple keep them alive for both passes.
    template <typename... Args>
    static std::string Format(char const* fmt, Args const&... args)
    {
        if constexpr (sizeof...(Args) == 0)
            return fmt;
        else
        {
            auto values = std::make_tuple(LogArgument(args)...);
            int len = std::apply([&](auto const&... v) { return std::snprintf(nullptr, 0, fmt, AsCString(v)...); }, values);
            if (len <= 0)
                return fmt;
            std::string out(static_cast<size_t>(len), '\0');
            std::apply([&](auto const&... v) { std::snprintf(out.data(), out.size() + 1, fmt, AsCString(v)...); }, values);
            return out;
        }
    }

    template <typename T>
    static auto LogArgument(T const& value)
    {
        if constexpr (IsString<T>)
            return std::string(value);
        else
            return value;
    }

    template <typename T>
    static auto AsCString(T const& value)
    {
        if constexpr (std::is_same_v<T, std::string>)
            return value.c_str();
        else
            return value;
    }

    template <typename T>
    static constexpr bool IsString = std::is_same_v<std::decay_t<T>, std::string> ||
                                     std::is_same_v<std::decay_t<T>, std::string_view>;
};
#undef sLog
#define sLog (CmangosLogProxy())
