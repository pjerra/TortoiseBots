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
using CreatureInfo = CreatureTemplate;
using GameObjectInfo = GameObjectTemplate;
using GenericTransport = Transport;
using GuidVector = std::vector<ObjectGuid>;
using ItemPrototype = ItemTemplate;
using ObjectGuidSet = GuidSet;
using SpellEntry = SpellInfo;
using UnitAI = CreatureAI;

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

// ---------------------------------------------------------------------------------------------------------------
// 3. MEMBER NAMES
// ---------------------------------------------------------------------------------------------------------------
#define GetObjectGuid GetGUID
#define getLevel GetLevel

// ---------------------------------------------------------------------------------------------------------------
// 4. FREE FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------
#ifndef M_PI_F
#define M_PI_F float(M_PI)
#endif

// cmangos WorldTimer: milliseconds since the server started.
struct WorldTimer
{
    static uint32 getMSTime() { return ::getMSTime(); }
    static uint32 getMSTimeDiff(uint32 oldTime, uint32 newTime) { return ::getMSTimeDiff(oldTime, newTime); }
};
