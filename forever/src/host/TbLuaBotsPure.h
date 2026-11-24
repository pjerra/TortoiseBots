// The game-free half of the module's ForeverLua bot provider (TBF-B1, SPEC-forever-lua-bot-api.md sections 3, 5 and 7):
// what the module knows (a BotMgr bot, h3::LastOrder, a run's record and event ring) mapped to the fields the engine's
// BotProvider.h names, plus the run-event cursor. No core header and no engine header here: TbLuaBots.cpp reads the game
// and hands these functions its plain state, and forever/tools/test_lua_bots_check.cpp drives them directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace luabots
{
// The engine's fl::Guid is two 64-bit halves of ForeverCore's 128-bit ObjectGuid: raw value 1 is `hi`, raw value 0 `lo`.
struct GuidHalves
{
    uint64_t hi = 0;
    uint64_t lo = 0;
    bool empty() const { return hi == 0 && lo == 0; }
};
inline bool operator==(GuidHalves const& a, GuidHalves const& b) { return a.hi == b.hi && a.lo == b.lo; }
inline bool operator!=(GuidHalves const& a, GuidHalves const& b) { return !(a == b); }
inline bool operator<(GuidHalves const& a, GuidHalves const& b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }

// ---- the bot's role/spec words (TBF-B1 review finding 1): h3::RoleText and h3::SpecText scan the bot's talents
// (RandomItemMgr::GetPlayerSpecId copies every weight scale per call and scans the tabs), so the provider keeps the last
// values per bot and recomputes them at most once a second, and at once on the hooks that can change them (level up,
// free talent points, talent reset). The snapshot only reads this cache. World thread only; no lock.
constexpr uint32_t kSpecRoleRefreshMs = 1000;

struct SpecRoleEntry
{
    std::string role;   // h3::RoleText
    std::string spec;   // h3::SpecText
    uint32_t refreshedMs = 0;
};

class SpecRoleCache
{
public:
    // True when the bot has an entry stored inside the window that ends at nowMs. The subtraction is unsigned, so the
    // test survives getMSTime's ~49-day wrap; a clock that went backwards reads as stale and refetches.
    bool Fresh(GuidHalves const& bot, uint32_t nowMs) const
    {
        auto const it = _rows.find(bot);
        return it != _rows.end() && nowMs - it->second.refreshedMs < kSpecRoleRefreshMs;
    }

    SpecRoleEntry const* Find(GuidHalves const& bot) const
    {
        auto const it = _rows.find(bot);
        return it == _rows.end() ? nullptr : &it->second;
    }

    void Store(GuidHalves const& bot, std::string role, std::string spec, uint32_t nowMs)
    {
        SpecRoleEntry& entry = _rows[bot];
        entry.role = std::move(role);
        entry.spec = std::move(spec);
        entry.refreshedMs = nowMs;
    }

    // A level or talent event: the next snapshot recomputes this bot's words.
    void Invalidate(GuidHalves const& bot) { _rows.erase(bot); }

    // Drops the entries of bots that left the world, so the cache stays as small as the live bot list.
    void Retain(std::vector<GuidHalves> const& live)
    {
        std::set<GuidHalves> const keep(live.begin(), live.end());
        for (auto it = _rows.begin(); it != _rows.end();)
        {
            if (keep.count(it->first))
                ++it;
            else
                it = _rows.erase(it);
        }
    }

    size_t Size() const { return _rows.size(); }
    void Clear() { _rows.clear(); }

private:
    std::map<GuidHalves, SpecRoleEntry> _rows;
};

// The engine's HandleData without its ObjKind/map fields: the .cpp sets the kind, this carries where the object is.
struct HandleRow
{
    GuidHalves guid;
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    bool empty() const { return guid.empty(); }
};

// TBF-B1 review finding 1: whether a bot stays in the snapshot this tick. A cross-map teleport (Player::TeleportTo's far teleport)
// takes the player off its map for a tick: IsInWorld() is false and IsBeingTeleportedFar() is true, and the worldport is
// acknowledged on the next tick. The bot stays while its character object exists and its session has not asked for a logout, so a
// teleport is never a BOT_REMOVED/BOT_ADDED pair for the engine.
inline bool KeepInSnapshot(bool havePlayer, bool inWorld, bool teleportingFar, bool logoutRequested)
{
    return havePlayer && !logoutRequested && (inWorld || teleportingFar);
}

// Review finding 4: a player handle is the guid AND the place. A character that is offline or out of the world has no place, so the
// handle is EMPTY: a guid at map 0 (and instance 0) would read as Eastern Kingdoms and alias every offline character onto one spot.
inline HandleRow PlayerHandle(GuidHalves const& guid, bool havePlayer, bool inWorld, uint32_t mapId, uint32_t instanceId)
{
    HandleRow row;
    if (!havePlayer || !inWorld)
        return row;
    row.guid = guid;
    row.mapId = mapId;
    row.instanceId = instanceId;
    return row;
}

// Review finding 6: the role word is never empty. IsTank/IsHeal read the forced role, the engine's strategies and the bot's talents
// and need no PlayerbotAI object, so the word is right before the brain is built and while it is away (an entry walk, a master
// away); dps is the fallback.
inline std::string RoleWord(bool isTank, bool isHealer)
{
    return isTank ? "tank" : isHealer ? "healer" : "dps";
}

// h3::LastOrder's result word mapped to the engine's OrderInfo (BotProvider.h):
//   seq 0             -> None    (no order yet)
//   "ok"              -> Done    (result "ok")
//   "fail:<code>"     -> Failed  (code = <code>, message = the TBF result text, which is all the module keeps)
//   anything else     -> Pending ("queued", "pending", "pending:walking", any unknown text): result stays empty
// An unknown text is never reported as a success or a failure: the engine ends a ticket still pending after 30 s as `lost`.
enum class OrderWord : uint8_t
{
    None = 0,
    Pending = 1,
    Done = 2,
    Failed = 3
};

struct OrderView
{
    OrderWord state = OrderWord::None;
    std::string result;   // "" while None/Pending
    std::string code;     // the failure code only
    std::string message;  // the TBF result text, only when Failed
};

inline OrderView ReadOrder(uint32_t seq, std::string const& result)
{
    OrderView out;
    if (seq == 0)
        return out;
    if (result == "ok")
    {
        out.state = OrderWord::Done;
        out.result = "ok";
        return out;
    }
    static constexpr char kFail[] = "fail:";
    constexpr size_t kFailLen = sizeof(kFail) - 1;
    if (result.compare(0, kFailLen, kFail) == 0)
    {
        out.state = OrderWord::Failed;
        out.result = result;
        out.message = result;
        out.code = result.substr(kFailLen);
        if (out.code.empty())
            out.code = "failed";
        return out;
    }
    out.state = OrderWord::Pending;
    return out;
}

// RunInfo.why is the pause/stop reason and "" on a running or done run: the run's own note on a pause, the stop reason
// on a stop.
inline std::string RunWhy(std::string const& state, std::string const& stopReason, std::string const& note)
{
    if (state == "stopped")
        return stopReason;
    if (state == "paused")
        return note;
    return std::string();
}

// One group member as the player table has it. WIPE is computed by the engine over the in-world members of a group that
// holds a bot, humans included, so both flags travel for every member.
struct MemberState
{
    GuidHalves guid;
    bool isBot = false;
    bool alive = true;
    bool inWorld = true;
};

// A group member row from what the player table says of its character: a member that is not in the world is alive=false
// too (the hub skips it, and WIPE counts only in-world members), so a WIPE needs every in-world member dead.
inline MemberState MemberFrom(GuidHalves guid, bool isBot, bool online, bool inWorld, bool alive)
{
    MemberState member;
    member.guid = guid;
    member.isBot = isBot;
    member.inWorld = online && inWorld;
    member.alive = member.inWorld && alive;
    return member;
}

struct GroupState
{
    GuidHalves guid;
    std::vector<MemberState> members;
};

// The run a bot leads (`run::All`'s record whose leader is the bot): RunInfo in BotProvider.h.
struct RunState
{
    HandleRow leader;
    std::string plan;
    std::string state;       // running | paused | done | stopped (run::StateName)
    uint32_t step = 0;
    uint32_t steps = 0;
    std::string stopReason;  // RunRec::stopReason
    std::string note;        // RunRec::run.note (the pause word)
};

// One bot as TbLuaBots.cpp reads it: every BotInfo field, still game-free.
struct BotFacts
{
    HandleRow player;    // guid, map and instance of the bot's character
    std::string name;
    uint32_t level = 0;
    uint32_t cls = 0;
    std::string role;         // h3::RoleText: tank | healer | dps | ""
    bool roleForced = false;
    std::string spec;         // h3::SpecText
    bool hasEngine = false;
    std::string engineState;  // h3::EngineStateText: combat | non-combat | dead
    bool alive = true;
    bool inCombat = false;
    HandleRow master;     // empty = none (BotMgr::MasterOf)
    HandleRow commander;  // empty = none (h3::CommanderGuid)
    HandleRow group;      // empty = none
    uint64_t owner = 0;   // the Battle.net account memoised at login (0 = unknown)
    uint32_t orderSeq = 0;
    std::string orderVerb;
    std::string orderResult;
    bool hasRun = false;
    RunState run;
};

// The mapped rows the .cpp copies into fl::BotInfo / fl::GroupInfo (same field names, one struct per engine type).
struct OrderRow
{
    uint32_t seq = 0;
    std::string verb;
    std::string result;
    std::string code;
    std::string message;
    OrderWord state = OrderWord::None;
};

struct RunRow
{
    HandleRow leader;
    std::string plan;
    std::string state;
    std::string why;
    uint32_t step = 0;
    uint32_t steps = 0;
};

struct BotRow
{
    HandleRow player;
    std::string name;
    uint32_t level = 0;
    uint32_t cls = 0;
    std::string role;
    bool roleForced = false;
    std::string spec;
    bool hasEngine = false;
    std::string engineState;
    bool alive = true;
    bool inCombat = false;
    HandleRow master;
    HandleRow commander;
    HandleRow group;
    uint64_t owner = 0;
    OrderRow lastOrder;
    bool hasRun = false;
    RunRow run;
};

struct MemberRow
{
    GuidHalves guid;
    bool isBot = false;
    bool alive = true;
    bool inWorld = true;
};

struct GroupRow
{
    HandleRow group;
    std::vector<MemberRow> members;
};

struct SnapshotRows
{
    std::vector<BotRow> bots;
    std::vector<GroupRow> groups;
};

// Every bot the module reads, every group that holds one (a group with no bot is not needed by any B1 global), every
// member of those groups with both flags. Order is the module's; the engine sorts.
inline SnapshotRows BuildSnapshot(std::vector<BotFacts> const& bots, std::vector<GroupState> const& groups)
{
    SnapshotRows out;
    out.bots.reserve(bots.size());
    for (BotFacts const& bot : bots)
    {
        BotRow row;
        row.player = bot.player;
        row.name = bot.name;
        row.level = bot.level;
        row.cls = bot.cls;
        row.role = bot.role;
        row.roleForced = bot.roleForced;
        row.spec = bot.spec;
        row.hasEngine = bot.hasEngine;
        row.engineState = bot.engineState;
        row.alive = bot.alive;
        row.inCombat = bot.inCombat;
        row.master = bot.master;
        row.commander = bot.commander;
        row.group = bot.group;
        row.owner = bot.owner;
        OrderView const order = ReadOrder(bot.orderSeq, bot.orderResult);
        row.lastOrder.seq = bot.orderSeq;
        row.lastOrder.verb = bot.orderSeq ? bot.orderVerb : std::string();
        row.lastOrder.result = order.result;
        row.lastOrder.code = order.code;
        row.lastOrder.message = order.message;
        row.lastOrder.state = order.state;
        row.hasRun = bot.hasRun;
        if (bot.hasRun)
        {
            row.run.leader = bot.run.leader;
            row.run.plan = bot.run.plan;
            row.run.state = bot.run.state;
            row.run.why = RunWhy(bot.run.state, bot.run.stopReason, bot.run.note);
            row.run.step = bot.run.step;
            row.run.steps = bot.run.steps;
        }
        out.bots.push_back(std::move(row));
    }
    out.groups.reserve(groups.size());
    for (GroupState const& group : groups)
    {
        bool holdsBot = false;
        for (MemberState const& member : group.members)
            if (member.isBot)
            {
                holdsBot = true;
                break;
            }
        if (!holdsBot)
            continue;
        GroupRow row;
        row.group.guid = group.guid;
        row.members.reserve(group.members.size());
        for (MemberState const& member : group.members)
            row.members.push_back(MemberRow{member.guid, member.isBot, member.alive, member.inWorld});
        out.groups.push_back(std::move(row));
    }
    return out;
}

// ---- the run ring (events 8/9): the run table's events are not consumed (`.tb run events` keeps them), so the provider
// remembers, per run, the newest seq it has taken and reports only what is newer. Kinds the API does not name are skipped
// but their seq still advances the cursor. ----
enum : int
{
    RunKindStep = 0,   // RUN_STEP
    RunKindDone = 1,   // RUN_STOPPED with `done`
    RunKindStop = 2,   // RUN_STOPPED with `stopped`
    RunKindSkip = -1
};

inline int RunKind(std::string const& kind)
{
    if (kind == "step")
        return RunKindStep;
    if (kind == "done")
        return RunKindDone;
    if (kind == "stop")
        return RunKindStop;
    return RunKindSkip;
}

struct RunEventRow
{
    int kind = RunKindSkip;
    HandleRow leader;
    std::string plan;
    int32_t step = -1;
    int32_t leg = -1;
    std::string why;
};

// One cursor per run. A run is identified by its leader and the record's monotonic `runId` (RunRec::runId): a stop and a restart in
// the same tick create a new record with a new id, so its events are new whatever the world clock says (the start clock is cached per
// tick, so the old start clock test could not tell the runs apart). `seq` is the newest event taken for that record.
class RunCursor
{
public:
    // True when this ring entry is new for its run, and advances the cursor past it (whether or not the caller reports its kind).
    // Call once per ring entry, in ring order.
    bool Admit(GuidHalves const& leader, uint32_t runId, uint32_t eventSeq)
    {
        Row* row = Find(leader);
        if (!row)
        {
            _rows.push_back(Row{leader, runId, 0});
            row = &_rows.back();
        }
        else if (row->runId != runId)
        {
            row->runId = runId;
            row->seq = 0;
        }
        if (eventSeq <= row->seq)
            return false;
        row->seq = eventSeq;
        return true;
    }

    // Drops the cursors of runs that are no longer in the table. A key is (leader, runId): a replaced run's cursor goes with it.
    void Prune(std::vector<std::pair<GuidHalves, uint32_t>> const& live)
    {
        for (auto it = _rows.begin(); it != _rows.end();)
        {
            bool keep = false;
            for (auto const& key : live)
                if (key.first == it->leader && key.second == it->runId)
                {
                    keep = true;
                    break;
                }
            it = keep ? it + 1 : _rows.erase(it);
        }
    }

    // The newest seq taken for that record (0 when the cursor has none for it).
    uint32_t Seen(GuidHalves const& leader, uint32_t runId) const
    {
        Row const* row = Find(leader);
        return row && row->runId == runId ? row->seq : 0;
    }

    void Clear() { _rows.clear(); }

private:
    struct Row
    {
        GuidHalves leader;
        uint32_t runId = 0;
        uint32_t seq = 0;
    };

    Row* Find(GuidHalves const& leader)
    {
        for (Row& row : _rows)
            if (row.leader == leader)
                return &row;
        return nullptr;
    }
    Row const* Find(GuidHalves const& leader) const
    {
        for (Row const& row : _rows)
            if (row.leader == leader)
                return &row;
        return nullptr;
    }

    std::vector<Row> _rows;
};
}  // namespace luabots
}  // namespace TortoiseBots
