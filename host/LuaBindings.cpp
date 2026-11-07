#include "LuaBindings.h"

#include "../commands/BotCommands.h"
#include "../runtime/BotManager.h"
#include "../runtime/LuaApiPolicy.h"
#include "../runtime/ObservabilityEmitter.h"
#include "Chat.h"
#include "ModuleLog.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#ifdef ENABLE_ELUNA
#include "ElunaCustomRegistry.h"
#include "ElunaIncludes.h"
#include "ElunaTemplate.h"
#include "LuaEngine.h"
#include "World.h"
#endif

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace LuaBindings {
namespace {

// A reply that is ready for Flush.
struct Completed
{
    uint64_t id = 0;
    uint32_t requester = 0;   // guid counter
    std::vector<std::string> lines;
    bool truncated = false;
};

// Everything below is touched by TB.Command (the world Lua state, on the world thread) and by the queue drain
// and Flush (the world thread too). The mutex is belt and braces: Eluna hands the world state to the world
// thread only, but nothing here should depend on that.
std::mutex g_mutex;
std::vector<Completed> g_completed;
LuaApi::CallbackLedger g_ledger;
std::atomic<uint64_t> g_nextId{0};

// The queue drain builds a plain ChatHandler for a player's command; a Lua command needs the reply lines, so this
// one keeps them. The command layer answers through SendSysMessage alone (human text and TBM: protocol lines
// alike), so overriding the one virtual sender redirects every reply, as the addon transport does.
class LuaReplyHandler final : public ChatHandler
{
public:
    LuaReplyHandler(Player* requester, LuaApi::ReplyBuffer& buffer) : ChatHandler(requester), m_buffer(buffer) {}
    using ChatHandler::SendSysMessage;
    void SendSysMessage(char const* str) override { m_buffer.Append(str); }

private:
    LuaApi::ReplyBuffer& m_buffer;
};

void LogReply(Completed const& done, char const* why)
{
    if (done.lines.empty())
        TB_LOG_DETAIL("TortoiseBots: Lua command #%llu for %u: no reply%s", static_cast<unsigned long long>(done.id),
            done.requester, why);
    for (std::string const& line : done.lines)
        TB_LOG_DETAIL("TortoiseBots: Lua command #%llu for %u%s: %s", static_cast<unsigned long long>(done.id),
            done.requester, why, line.c_str());
}

#ifdef ENABLE_ELUNA

// Where the world state keeps the callbacks of commands still waiting: a table in the Lua registry, key = the
// command id. The state owns it, so a reload drops every callback with the state and TB keeps no Lua reference.
char const kCallbackKey = 0;

void PushCallbackTable(lua_State* L, bool create)
{
    lua_pushlightuserdata(L, const_cast<char*>(&kCallbackKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_istable(L, -1) && create)
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, const_cast<char*>(&kCallbackKey));
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
}

// Stack: function at `index`. Keeps it under `id`.
void StoreCallback(lua_State* L, int index, uint64_t id)
{
    PushCallbackTable(L, true);
    lua_pushnumber(L, static_cast<lua_Number>(id));
    lua_pushvalue(L, index < 0 ? index - 2 : index);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

// Pushes the callback stored under `id` (nil when none) and forgets it.
void TakeCallback(lua_State* L, uint64_t id)
{
    PushCallbackTable(L, false);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_pushnil(L);
        return;
    }
    lua_pushnumber(L, static_cast<lua_Number>(id));
    lua_rawget(L, -2);
    lua_pushnumber(L, static_cast<lua_Number>(id));
    lua_pushnil(L);
    lua_rawset(L, -4);
    lua_remove(L, -2); // the table; the callback (or nil) stays on top
}

void PushFields(lua_State* L, LuaApi::Fields const& fields)
{
    lua_createtable(L, 0, static_cast<int>(fields.size()));
    for (LuaApi::Field const& field : fields)
    {
        switch (field.kind)
        {
            case LuaApi::Field::Kind::Number:
                lua_pushnumber(L, static_cast<lua_Number>(field.number));
                break;
            case LuaApi::Field::Kind::Bool:
                lua_pushboolean(L, field.boolean ? 1 : 0);
                break;
            case LuaApi::Field::Kind::String:
                lua_pushlstring(L, field.text.data(), field.text.size());
                break;
        }
        lua_setfield(L, -2, field.key.c_str());
    }
}

void PushLines(lua_State* L, std::vector<std::string> const& lines)
{
    lua_createtable(L, static_cast<int>(lines.size()), 0);
    int index = 0;
    for (std::string const& line : lines)
    {
        lua_pushlstring(L, line.data(), line.size());
        lua_rawseti(L, -2, ++index);
    }
}

// nil, "<code>": the way every binding below says no. They never call luaL_error: a longjmp over C++ frames is
// the one thing a binding must not do, and every argument is checked before TB code runs.
int Fail(Eluna* E, char const* code)
{
    lua_pushnil(E->L);
    lua_pushstring(E->L, code);
    return 2;
}

bool CounterArg(Eluna* E, int index, uint32_t& out)
{
    if (lua_type(E->L, index) != LUA_TNUMBER)
        return false;
    return LuaApi::ParseGuidCounter(static_cast<double>(lua_tonumber(E->L, index)), out);
}

ObjectGuid PlayerGuid(uint32_t counter)
{
    return ObjectGuid(HIGHGUID_PLAYER, counter);
}

// TB.GetBots([masterGuid]) -> { guid, ... }: the in-world bots, or those of one master.
int GetBots(Eluna* E)
{
    std::vector<Player*> bots;
    if (lua_type(E->L, 1) != LUA_TNONE && lua_type(E->L, 1) != LUA_TNIL)
    {
        uint32_t master = 0;
        if (!CounterArg(E, 1, master))
            return Fail(E, "bad_guid");
        bots = BotManager::Instance().GetBotsForMaster(PlayerGuid(master));
    }
    else
        bots = BotManager::Instance().GetAllBots();
    lua_createtable(E->L, static_cast<int>(bots.size()), 0);
    int index = 0;
    for (Player* bot : bots)
    {
        lua_pushnumber(E->L, static_cast<lua_Number>(bot->GetGUIDLow()));
        lua_rawseti(E->L, -2, ++index);
    }
    return 1;
}

// TB.GetBot(guid) -> table, or nil when that guid is not an in-world bot.
int GetBot(Eluna* E)
{
    uint32_t counter = 0;
    if (!CounterArg(E, 1, counter))
        return Fail(E, "bad_guid");
    Player* bot = sObjectAccessor.FindPlayer(PlayerGuid(counter));
    BotTelemetrySnapshot snapshot;
    if (!bot || !BotManager::Instance().IsBot(bot->GetObjectGuid()) ||
        !ObservabilityEmitter::Instance().SnapshotBot(bot, snapshot))
    {
        lua_pushnil(E->L);
        return 1;
    }
    PushFields(E->L, LuaApi::BotFields(snapshot));
    return 1;
}

// TB.GetRun(leaderGuid) -> { active = false } or the run the player owns.
int GetRun(Eluna* E)
{
    uint32_t counter = 0;
    if (!CounterArg(E, 1, counter))
        return Fail(E, "bad_guid");
    LuaApi::RunView view;
    BotCommands::ReadRunForLua(PlayerGuid(counter).GetRawValue(), view);
    PushFields(E->L, LuaApi::RunFields(view));
    return 1;
}

// TB.Command(requesterGuid, text [, callback]) -> id, or nil + code. The command is queued like a typed `.bot`
// and runs on the world thread at the next drain, as the requester, through TB's own handlers and permission
// checks. The reply goes to callback(id, requesterGuid, lines, truncated), else the server log.
int Command(Eluna* E)
{
    lua_State* L = E->L;
    uint32_t requester = 0;
    if (!CounterArg(E, 1, requester))
        return Fail(E, "bad_guid");
    if (lua_type(L, 2) != LUA_TSTRING)
        return Fail(E, "bad_text");
    size_t length = 0;
    char const* raw = lua_tolstring(L, 2, &length);
    if (length > LuaApi::kMaxCommandLength * 4)
        return Fail(E, "too_long");
    bool const wantsCallback = lua_type(L, 3) != LUA_TNONE && lua_type(L, 3) != LUA_TNIL;
    if (wantsCallback && lua_type(L, 3) != LUA_TFUNCTION)
        return Fail(E, "bad_callback");

    LuaApi::CommandRequest const request = LuaApi::ParseCommand(std::string(raw, length));
    if (!request.ok)
        return Fail(E, request.error);

    // The requester must be a player in the world now; the drain looks it up again and drops the command if it
    // logged out in between.
    Player* player = ObjectAccessor::FindPlayerNotInWorld(PlayerGuid(requester));
    if (!player || !player->GetSession())
        return Fail(E, "no_such_player");

    uint64_t const id = ++g_nextId;
    if (wantsCallback)
    {
        bool added;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            added = g_ledger.Add(id, WorldTimer::getMSTime());
        }
        if (!added)
            return Fail(E, "too_many_callbacks");
        StoreCallback(L, 3, id);
    }
    if (!BotCommandQueue::Instance().Enqueue(PlayerGuid(requester).GetRawValue(), BotCommandReply::Lua, request.args,
            request.topLevel, id))
    {
        if (wantsCallback)
        {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_ledger.Take(id);
            }
            TakeCallback(L, id);
            lua_pop(L, 1);
        }
        return Fail(E, "queue_full");
    }
    lua_pushnumber(L, static_cast<lua_Number>(id));
    return 1;
}

// World state only (METHOD_REG_WORLD): a map state gets Eluna's own "attempt to call method ... for state"
// refusal. Global functions, wrapped into the TB table by RegisterTable below.
ElunaRegister<> Methods[] =
{
    { "TBGetBots", &GetBots, METHOD_REG_WORLD },
    { "TBGetBot", &GetBot, METHOD_REG_WORLD },
    { "TBGetRun", &GetRun, METHOD_REG_WORLD },
    { "TBCommand", &Command, METHOD_REG_WORLD },
};

// TB = { GetBots = TBGetBots, ... }: the names a script uses. The table points at the same (world-only) globals.
void RegisterTable(Eluna* E)
{
    struct Name { char const* shortName; char const* globalName; };
    static Name const names[] = { { "GetBots", "TBGetBots" }, { "GetBot", "TBGetBot" }, { "GetRun", "TBGetRun" },
        { "Command", "TBCommand" } };
    lua_State* L = E->L;
    lua_newtable(L);
    for (Name const& name : names)
    {
        lua_getglobal(L, name.globalName);
        lua_setfield(L, -2, name.shortName);
    }
    lua_setglobal(L, "TB");
}

// Runs for every Lua state at its creation, after Eluna's own methods (ElunaCustomRegistry).
void RegisterState(Eluna* E)
{
    ElunaTemplate<>::SetMethods(E, Methods);
    RegisterTable(E);
}

// Before main: the world Lua state is created before ScriptMgr::Initialize runs the module's RegisterScripts.
bool const g_registered = (ElunaCustomRegistry::Add(&RegisterState), true);

void Deliver(Eluna* E, Completed const& done)
{
    lua_State* L = E->L;
    TakeCallback(L, done.id);
    if (!lua_isfunction(L, -1))
    {
        // The state was reloaded since the command was queued.
        lua_pop(L, 1);
        LogReply(done, " (callback gone)");
        return;
    }
    lua_pushnumber(L, static_cast<lua_Number>(done.id));
    lua_pushnumber(L, static_cast<lua_Number>(done.requester));
    PushLines(L, done.lines);
    lua_pushboolean(L, done.truncated ? 1 : 0);
    E->ExecuteCall(4, 0);
}

#endif // ENABLE_ELUNA

} // namespace

void Register()
{
#ifdef ENABLE_ELUNA
    ElunaCustomRegistry::Add(&RegisterState);
    (void)g_registered;
#endif
}

void ExecuteCommand(Player* requester, PendingBotCommand const& command)
{
    LuaApi::ReplyBuffer buffer;
    LuaReplyHandler handler(requester, buffer);
    if (command.topLevel)
        BotCommands::TryHandleBotCommand(&handler, command.args.c_str());
    else
        BotCommands::HandleChatCommand(&handler, command.args.c_str());

    Completed done;
    done.id = command.luaId;
    done.requester = requester->GetGUIDLow();
    done.lines = buffer.Lines();
    done.truncated = buffer.Truncated();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_completed.push_back(std::move(done));
}

void ReportDropped(Player* requester, DroppedBotCommands const& dropped)
{
    TB_LOG_BASIC("TortoiseBots: %zu Lua command(s) of %s dropped: too many pending", dropped.count,
        requester ? requester->GetName() : "?");
}

void Flush()
{
    std::vector<Completed> ready;
    std::vector<uint64_t> expired;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_completed.empty() && g_ledger.Size() == 0)
            return;
        ready.swap(g_completed);
        g_ledger.Expire(WorldTimer::getMSTime(), expired);
    }

#ifdef ENABLE_ELUNA
    Eluna* E = sWorld.GetEluna();
#endif
    for (Completed const& done : ready)
    {
        bool waiting;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            waiting = g_ledger.Take(done.id);
        }
#ifdef ENABLE_ELUNA
        if (waiting && E)
        {
            Deliver(E, done);
            continue;
        }
#else
        (void)waiting;
#endif
        LogReply(done, "");
    }
    for (uint64_t id : expired)
    {
        TB_LOG_BASIC("TortoiseBots: Lua command #%llu: no reply in %u s, its callback is dropped",
            static_cast<unsigned long long>(id), LuaApi::kCallbackExpiryMs / 1000);
#ifdef ENABLE_ELUNA
        if (E)
        {
            TakeCallback(E->L, id);
            lua_pop(E->L, 1);
        }
#endif
    }
}

} // namespace LuaBindings
} // namespace TortoiseBots
