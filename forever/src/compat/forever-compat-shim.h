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

// ---------------------------------------------------------------------------------------------------------------
// 1. TYPE NAMES
// ---------------------------------------------------------------------------------------------------------------
using BattleGroundTypeId = BattlegroundTypeId;   // the battleground id enum; AV/WS/AB are 1/2/3 in both cores
using Config = ConfigMgr;                        // the config store; cmangos names the class Config
using CreatureInfo = CreatureTemplate;
using GameObjectInfo = GameObjectTemplate;
using GenericTransport = Transport;
using GridPair = GridCoord;                      // CoordPair<MAX_NUMBER_OF_GRIDS>, same layout with x_coord/y_coord
using CellPair = CellCoord;                      // CoordPair<TOTAL_NUMBER_OF_CELLS_PER_MAP>
using GuidVector = std::vector<ObjectGuid>;
using ObjectGuidSet = GuidSet;
using Movement::PointsArray;                     // the polyline of a path (std::vector<Vector3> in both cores)
using Movement::Vector3;                         // G3D::Vector3, the point type cmangos exposes unqualified

// The value type of the spawn stores: cmangos iterates `CreatureDataPair`/`GameObjectDataPair`, ForeverCore's
// GetAllCreatureData()/GetAllGameObjectData() hold the same pair keyed by the spawn id (ObjectGuid::LowType, 32 bit).
using CreatureDataPair = std::pair<uint32 const, CreatureData>;
using GameObjectDataPair = std::pair<uint32 const, GameObjectData>;

// A macro, not `using`: bot headers forward-declare `struct ItemPrototype;` / `struct SpellEntry;`, and a type alias
// cannot be redeclared as a class. The macro renames the forward declaration with the rest (a class-keyed
// redeclaration of a defined class is legal). ForeverCore never uses either old name.
#define ItemPrototype ItemTemplate
#define SpellEntry SpellInfo

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
#define sObjectMgr (*ObjectMgr::instance())
#undef sWorld
#define sWorld (*World::instance())
#undef sSpellMgr
#define sSpellMgr (*SpellMgr::instance())
#undef sGuildMgr
#define sGuildMgr (*GuildMgr::instance())
#undef sMapMgr
#define sMapMgr (*MapManager::instance())
#undef sConfig
#define sConfig (*ConfigMgr::instance())
#undef sBattleGroundMgr
#define sBattleGroundMgr (*BattlegroundMgr::instance())

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
#define GetDeathState getDeathState
#define GetGameObjectInfo GetGameObjectTemplate
#define GetGOData GetGameObjectData
#define GetHealthPercent GetHealthPct
#define GetItemPrototype GetItemTemplate
#define GetObjectGuid GetGUID
#define GetSelectionGuid GetTarget
#define InBattleGround InBattleground
#define IsDead isDead
#define IsNonMeleeSpellCasted IsNonMeleeSpellCast

// Lower-case spellings the bot brain uses for core members (the same members, renamed by ForeverCore).
// (getName is not here: bot classes define both getName() and a GetName() forwarder, so a global rename would
// redeclare those members.)
#define getDistance2d GetDistance2d
#define getLevel GetLevel
#define getObjectGuid GetGUID
#define getOrientation GetOrientation
#define getPositionX GetPositionX
#define getPositionY GetPositionY
#define getPositionZ GetPositionZ

// cmangos asked a unit for its database id; ForeverCore's guid keeps it in the counter. GetCounter() is 64 bit
// (ObjectGuid::LowType), the old value was 32 bit, so the number is the same but wider.
#define GetGUIDLow() GetGUID().GetCounter()

// TempSummonType: the same values under ForeverCore's TEMPSUMMON_ names (ObjectDefines.h:60-72).
#define TEMPSPAWN_CORPSE_DESPAWN TEMPSUMMON_CORPSE_DESPAWN
#define TEMPSPAWN_CORPSE_TIMED_DESPAWN TEMPSUMMON_CORPSE_TIMED_DESPAWN
#define TEMPSPAWN_DEAD_DESPAWN TEMPSUMMON_DEAD_DESPAWN
#define TEMPSPAWN_MANUAL_DESPAWN TEMPSUMMON_MANUAL_DESPAWN
#define TEMPSPAWN_TIMED_DESPAWN TEMPSUMMON_TIMED_DESPAWN
#define TEMPSPAWN_TIMED_OR_CORPSE_DESPAWN TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN
#define TEMPSPAWN_TIMED_OR_DEAD_DESPAWN TEMPSUMMON_TIMED_OR_DEAD_DESPAWN

// AreaTable flags: the same DBC bits, named by ForeverCore's AreaFlags enum. Values are the client's.
#define AREA_FLAG_CAPITAL AreaFlags::LinkedChat              // 0x00000100, the capital-city bit
#define AREA_FLAG_CITY AreaFlags::LinkedChatSpecialArea      // 0x00000200
#define AREA_FLAG_SLAVE_CAPITAL AreaFlags::AllowTradeChannel // 0x00000008

// SpellMgr::GetSpellInfo needs the difficulty; cmangos asked for one spell id and got the single entry.
#define GetSpellEntry(spellId) GetSpellInfo((spellId), DIFFICULTY_NONE)

// ---------------------------------------------------------------------------------------------------------------
// 4. FREE FUNCTIONS AND PROXIES
// ---------------------------------------------------------------------------------------------------------------
// Bot files carry stand-in definitions behind these guards for tools that do not see the core headers (Lens). The
// real headers are in front of every file here, so the guard is set and the stand-ins stay out.
#define MANGOS_OBJECT_GUID_H 1
#define MANGOS_OBJECTACCESSOR_H 1

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
    template <typename... Args> void outBasic(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }
    void outDebug() const { LogAt(LOG_LEVEL_DEBUG, "server", ""); }
    template <typename... Args> void outDebug(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_DEBUG, "server", Format(fmt, args...)); }
    void outDetail() const { LogAt(LOG_LEVEL_DEBUG, "server", ""); }
    template <typename... Args> void outDetail(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_DEBUG, "server", Format(fmt, args...)); }
    void outError() const { LogAt(LOG_LEVEL_ERROR, "server", ""); }
    template <typename... Args> void outError(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_ERROR, "server", Format(fmt, args...)); }
    void outErrorDb() const { LogAt(LOG_LEVEL_ERROR, "sql.sql", ""); }
    template <typename... Args> void outErrorDb(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_ERROR, "sql.sql", Format(fmt, args...)); }
    void outInfo() const { LogAt(LOG_LEVEL_INFO, "server", ""); }
    template <typename... Args> void outInfo(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }
    void outString() const { LogAt(LOG_LEVEL_INFO, "server", ""); }
    template <typename... Args> void outString(char const* fmt, Args... args) const { LogAt(LOG_LEVEL_INFO, "server", Format(fmt, args...)); }

private:
    static void LogAt(LogLevel level, char const* logger, std::string const& message)
    {
        Log::instance()->OutMessage(logger, level, "{}", message);
    }

    // printf formatting of the old line. A std::string argument would read as garbage through %s, so it is refused
    // at compile time instead of logging nonsense: those call sites pass .c_str().
    template <typename... Args>
    static std::string Format(char const* fmt, Args... args)
    {
        if constexpr (sizeof...(Args) == 0)
            return fmt;
        else
        {
            static_assert((!IsStringArg<Args> && ...), "a log argument must not be a std::string: pass .c_str()");
            int len = std::snprintf(nullptr, 0, fmt, args...);
            if (len <= 0)
                return fmt;
            std::string out(static_cast<size_t>(len), '\0');
            std::snprintf(out.data(), out.size() + 1, fmt, args...);
            return out;
        }
    }

    template <typename T>
    static constexpr bool IsStringArg = std::is_same_v<std::decay_t<T>, std::string> ||
                                        std::is_same_v<std::decay_t<T>, std::string_view>;
};
#undef sLog
#define sLog (CmangosLogProxy())
