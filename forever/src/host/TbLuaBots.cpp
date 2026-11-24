// TBF-B1: the module's provider for the ForeverLua bot API (SPEC-forever-lua-bot-api.md section 7, item 2). It fills the
// engine's snapshot once per world tick (the bots in the world, their owner/group/role/spec/master/order/run and the group
// members the WIPE edge reads) and reports the run ring's step/done/stop events. Nothing here gives orders: B2.
//
// Compiled only when both modules are built (TBF_WITH_FOREVER_LUA from BUILD_FOREVER_LUA) and only in the slice build that
// owns the bot engine (the CMake filter compiles this file for TORTOISEBOTS_SLICE1 only). Registered once by
// AddSC_tortoisebots, static lifetime, never unregistered; available() follows TortoiseBots.Enable (set at every config load)
// and answers false for good after TBF's OnShutdown, once the bots have been logged out.
//
// Threading: available() is read from map threads, so it is one atomic word the world thread writes (no game read).
// snapshot()/takeRunEvents() run on the world thread only (ForeverLua's WorldScript::OnUpdate).
#include "host/TbLuaBots.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA

#include "TbBotMgr.h"
#include "host/TbH3.h"
#include "host/TbLuaBotsPure.h"
#include "host/run/TbRun.h"

#include "compat/botpch_forever.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Timer.h"
#include "playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"

#include "engine/BotProvider.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace luabots
{
namespace
{
// Static assert: the engine's interface the module was written against (BotProvider.h says the provider checks the
// version once at register time; both come from one tree, so a mismatch is a build mistake).
static_assert(fl::kBotApiVersion == 1, "ForeverLua bot API version changed: TbLuaBots.cpp must be updated");
static_assert(static_cast<uint8_t>(OrderWord::None) == static_cast<uint8_t>(fl::OrderState::None) &&
                  static_cast<uint8_t>(OrderWord::Pending) == static_cast<uint8_t>(fl::OrderState::Pending) &&
                  static_cast<uint8_t>(OrderWord::Done) == static_cast<uint8_t>(fl::OrderState::Done) &&
                  static_cast<uint8_t>(OrderWord::Failed) == static_cast<uint8_t>(fl::OrderState::Failed),
              "OrderWord and fl::OrderState must name the same states in the same order");
static_assert(RunKindStep == static_cast<int>(fl::RunEventKind::Step) &&
                  RunKindDone == static_cast<int>(fl::RunEventKind::Done) &&
                  RunKindStop == static_cast<int>(fl::RunEventKind::Stop),
              "RunKind and fl::RunEventKind must name the same kinds in the same order");

std::atomic<bool> g_available{false};
std::atomic<bool> g_shutdown{false};   // finding 8: a shutdown is final, later config loads cannot re-enable the provider
std::atomic<bool> g_registered{false};
// The spec and role words per bot (finding 1: recomputing them scans the talents, so the snapshot reads them here).
// World thread only (readSnapshot); the player script's level/talent hooks reach it through the guarded g_invalid queue below.
SpecRoleCache g_specRole;
// The level/talent hooks fire on whichever thread changed the bot (a map worker when MapUpdate.Threads > 1), so they never touch
// g_specRole: Invalidate() only queues the guid here, and the snapshot (world thread) applies the queue before it reads the cache.
// Bounded by the number of distinct bot guids, and only filled while the API is available.
std::mutex g_invalidMutex;
std::set<GuidHalves> g_invalid;
constexpr size_t kMaxPendingFlush = 4096;   // the flushed run events no reader has taken yet

GuidHalves halves(ObjectGuid const& guid)
{
    return GuidHalves{guid.GetRawValue(1), guid.GetRawValue(0)};
}

fl::Guid engineGuid(HandleRow const& handle)
{
    return fl::Guid{handle.guid.hi, handle.guid.lo};
}

// A player handle: the guid plus where the character is in the world now (the engine's access rule compares both). A character that
// is offline or out of the world has no place, so the handle is EMPTY (finding 4: a guid at map 0 read as Eastern Kingdoms).
HandleRow playerAt(ObjectGuid const& guid)
{
    if (guid.IsEmpty())
        return HandleRow{};
    Player* player = ObjectAccessor::FindPlayer(guid);
    return PlayerHandle(halves(guid), player != nullptr, player && player->IsInWorld(), player ? player->GetMapId() : 0,
                        player ? player->GetInstanceId() : 0);
}

// The commander the API shows (finding 4): a run binds every follower to the tank BOT, so a bot's bound commander can itself be a
// bot; the handle is the human commander behind it (one hop, h3::CheckBotAccess's rule, TbH3.cpp:472-478). The bound guid is
// returned unchanged for a real player, and an empty binding stays empty.
ObjectGuid commanderGuid(ObjectGuid const& botGuid)
{
    ObjectGuid const bound = h3::CommanderGuid(botGuid);
    if (bound.IsEmpty() || !BotMgr::Instance().IsBot(bound))
        return bound;
    return h3::CommanderGuid(bound);
}

fl::HandleData toHandle(HandleRow const& row, fl::ObjKind kind)
{
    fl::HandleData handle;
    handle.kind = kind;
    handle.guid = engineGuid(row);
    handle.mapId = row.mapId;
    handle.instanceId = row.instanceId;
    return handle;
}

void toEngine(OrderRow const& in, fl::OrderInfo& out)
{
    out.seq = in.seq;
    out.verb = in.verb;
    out.result = in.result;
    out.state = static_cast<fl::OrderState>(static_cast<uint8_t>(in.state));
    out.code = in.code;
    out.message = in.message;
}

void toEngine(RunRow const& in, fl::RunInfo& out)
{
    out.leader = toHandle(in.leader, fl::ObjKind::Player);
    out.plan = in.plan;
    out.state = in.state;
    out.step = in.step;
    out.steps = in.steps;
    out.why = in.why;
}

void toEngine(BotRow const& in, fl::BotInfo& out)
{
    out.player = toHandle(in.player, fl::ObjKind::Player);
    out.name = in.name;
    out.level = in.level;
    out.cls = in.cls;
    out.role = in.role;
    out.roleForced = in.roleForced;
    out.spec = in.spec;
    out.hasEngine = in.hasEngine;
    out.engineState = in.engineState;
    out.alive = in.alive;
    out.inCombat = in.inCombat;
    out.master = toHandle(in.master, fl::ObjKind::Player);
    out.commander = toHandle(in.commander, fl::ObjKind::Player);
    out.group = toHandle(in.group, fl::ObjKind::Group);
    out.owner = in.owner;
    toEngine(in.lastOrder, out.lastOrder);
    out.hasRun = in.hasRun;
    if (in.hasRun)
        toEngine(in.run, out.run);
}

void toEngine(GroupRow const& in, fl::GroupInfo& out)
{
    out.group = toHandle(in.group, fl::ObjKind::Group);
    out.members.reserve(in.members.size());
    for (MemberRow const& member : in.members)
    {
        fl::GroupMemberInfo info;
        info.guid = fl::Guid{member.guid.hi, member.guid.lo};
        info.isBot = member.isBot;
        info.alive = member.alive;
        info.inWorld = member.inWorld;
        out.members.push_back(info);
    }
}

// The bots in the world now, their groups (only groups that hold a bot, every member with both flags) and the run each
// bot leads. World thread, from the host tables only: no database, no Lua. The role/spec words come from `specRole`: a
// bot's entry is refreshed at most once a second here (the hooks invalidate it sooner), so the per-tick cost per bot is
// one map lookup instead of a talent scan.
SnapshotRows readSnapshot(SpecRoleCache& specRole)
{
    BotMgr& bots = BotMgr::Instance();
    uint32 const nowMs = getMSTime();
    {
        std::set<GuidHalves> queued;
        {
            std::lock_guard<std::mutex> lock(g_invalidMutex);
            queued.swap(g_invalid);
        }
        for (GuidHalves const& key : queued)
            specRole.Invalidate(key);
    }

    std::set<ObjectGuid> groupGuids;
    std::vector<GroupState> groups;
    std::map<ObjectGuid, run::RunRec const*> runs;
    for (run::RunRec* rec : run::All())
        runs[rec->leader] = rec;

    std::vector<BotFacts> rows;
    std::vector<GuidHalves> liveBots;
    std::vector<ObjectGuid> const guids = bots.BotGuids();
    rows.reserve(guids.size());
    liveBots.reserve(guids.size());
    for (ObjectGuid const& guid : guids)
    {
        // Finding 1: in the world OR on a far teleport, and no logout asked: a cross-map teleport leaves IsInWorld() false for a
        // tick, and the bot must not leave the snapshot (a BOT_REMOVED/BOT_ADDED pair for a teleport).
        Player* bot = bots.SnapshotBot(guid);
        if (!bot)
            continue;

        GuidHalves const key = halves(guid);
        liveBots.push_back(key);
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!specRole.Fresh(key, nowMs))
            specRole.Store(key, h3::RoleText(bot, ai), h3::SpecText(bot), nowMs);
        SpecRoleEntry const& cached = *specRole.Find(key);

        BotFacts state;
        state.player = HandleRow{halves(guid), bot->GetMapId(), bot->GetInstanceId()};
        state.name = bot->GetName();
        state.level = bot->GetLevel();
        state.cls = uint32_t(bot->GetClass());
        state.hasEngine = ai != nullptr;
        state.role = cached.role;
        state.roleForced = h3::RoleForced(guid);
        state.spec = cached.spec;
        state.engineState = h3::EngineStateText(bot, ai);
        state.alive = bot->IsAlive();
        state.inCombat = bot->IsInCombat();
        state.master = playerAt(bots.MasterOf(guid));
        state.commander = playerAt(commanderGuid(guid));
        state.owner = bots.OwnerBnetOf(guid);

        // Finding 2: one unread order transition per snapshot (queued, then ok/fail), so an order overwritten by a newer one still
        // reaches the engine with its own outcome; with none left, the record's newest word is what `GetLastOrder` reads.
        h3::OrderStatus order;
        if (!h3::PollOrderEvent(guid, order))
            order = h3::LastOrder(guid);
        state.orderSeq = order.seq;
        state.orderVerb = order.seq ? order.verb : std::string();
        state.orderResult = order.result;

        if (Group* group = bot->GetGroup())
        {
            state.group = HandleRow{halves(group->GetGUID()), 0, 0};
            if (groupGuids.insert(group->GetGUID()).second)
            {
                GroupState members;
                members.guid = halves(group->GetGUID());
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    Player* online = ObjectAccessor::FindPlayer(slot.guid);
                    members.members.push_back(MemberFrom(halves(slot.guid), bots.IsBot(slot.guid), online != nullptr,
                                                         online && online->IsInWorld(), online && online->IsAlive()));
                }
                groups.push_back(std::move(members));
            }
        }

        auto const runRec = runs.find(guid);
        if (runRec != runs.end() && runRec->second->run.plan && !run::Ended(*runRec->second))
        {
            // Finding 7: an ENDED run is not reported as active (its terminal event already went out through the run ring), so a
            // relogged bot does not come back carrying the run it finished.
            run::RunRec const& rec = *runRec->second;
            state.hasRun = true;
            state.run.leader = playerAt(rec.leader);
            state.run.plan = rec.planName;
            state.run.state = run::StateName(rec);
            size_t const steps = rec.run.plan->steps.size();
            state.run.steps = uint32_t(steps);
            state.run.step = uint32_t(std::min<size_t>(rec.run.step, steps));
            state.run.stopReason = rec.stopReason;
            state.run.note = rec.run.note;
        }
        rows.push_back(std::move(state));
    }
    specRole.Retain(liveBots);   // a bot that left the world keeps no entry
    return BuildSnapshot(rows, groups);
}

class TbfBotProvider final : public fl::BotProvider
{
public:
    bool available() override { return g_available.load(std::memory_order_relaxed); }

    void snapshot(fl::BotSnapshot& out) override
    {
        out.bots.clear();
        out.groups.clear();
        if (!available())
            return;
        SnapshotRows const rows = readSnapshot(g_specRole);
        out.bots.reserve(rows.bots.size());
        for (BotRow const& bot : rows.bots)
            toEngine(bot, out.bots.emplace_back());
        out.groups.reserve(rows.groups.size());
        for (GroupRow const& group : rows.groups)
            toEngine(group, out.groups.emplace_back());
    }

    void takeRunEvents(std::vector<fl::RunEventInfo>& out) override
    {
        if (!available())
        {
            _pending.clear();   // no reader can take them: the buffer never grows while the API is off
            return;
        }
        // Finding 3: events flushed from a record that was removed before this tick (a stop and a restart in the same tick, a
        // logout) go out first; a stop can never be lost to a restart.
        for (fl::RunEventInfo& info : _pending)
            out.push_back(std::move(info));
        _pending.clear();
        // The run table's ring is the user's (`.tb run events`), so it is read, not drained: the cursor holds the newest
        // seq taken per record (leader + RunRec::runId). Every tick walks the rings, whether a listener exists or not (the
        // engine drops what nobody listens for), so the cursors always catch up.
        std::vector<std::pair<GuidHalves, uint32_t>> live;
        std::vector<run::RunRec*> const records = run::All();
        live.reserve(records.size());
        for (run::RunRec* rec : records)
        {
            live.emplace_back(halves(rec->leader), rec->runId);
            // Finding 7: an ended run whose events are all read is not walked again (its ring holds up to 1024 entries and
            // nothing new can arrive): the per-tick cost falls back to one entry per live run.
            if (run::Ended(*rec) && _cursor.Seen(halves(rec->leader), rec->runId) == rec->eventSeq)
                continue;
            Take(*rec, out);
        }
        _cursor.Prune(live);
    }

private:
    // Takes the record's ring entries the cursor has not seen, in ring order. Skipped kinds still advance the cursor (the
    // engine does not need them, and a later pass must not re-read them).
    void Take(run::RunRec const& rec, std::vector<fl::RunEventInfo>& out)
    {
        GuidHalves const leader = halves(rec.leader);
        HandleRow leaderAt;
        bool haveLeader = false;
        for (run::RunEvent const& event : rec.events)
        {
            if (!_cursor.Admit(leader, rec.runId, event.seq))
                continue;
            int const kind = RunKind(event.kind);
            if (kind == RunKindSkip)
                continue;
            if (!haveLeader)
            {
                leaderAt = playerAt(rec.leader);
                haveLeader = true;
            }
            fl::RunEventInfo info;
            info.kind = static_cast<fl::RunEventKind>(kind);
            info.leader = toHandle(leaderAt, fl::ObjKind::Player);
            info.plan = rec.planName;
            info.step = event.step;
            info.leg = event.leg;
            info.why = event.why;
            out.push_back(std::move(info));
        }
    }

public:
    // Finding 3: the record is about to be removed (run::Remove calls the flush listener first). Its unread events are taken
    // now, while the record is whole, and handed out by the next takeRunEvents: nothing of the replaced run is lost.
    void Flush(run::RunRec const& rec)
    {
        if (!available())
            return;   // takeRunEvents drops what nobody can read while the API is off, so nothing is kept for it
        Take(rec, _pending);
        if (_pending.size() > kMaxPendingFlush)   // a reader that stopped ticking cannot make the buffer grow for good
            _pending.erase(_pending.begin(), _pending.begin() + static_cast<std::ptrdiff_t>(_pending.size() - kMaxPendingFlush));
    }

private:
    RunCursor _cursor;
    std::vector<fl::RunEventInfo> _pending;   // events of a removed record, until the next takeRunEvents (world thread only)
};

// The one provider (static lifetime, like the engine's registration: never unregistered, so it must outlive the world).
TbfBotProvider& Provider()
{
    static TbfBotProvider provider;
    return provider;
}

// The run module's flush listener (TbRun.h): the provider takes a removed record's unread events before the record dies.
void FlushRun(run::RunRec const& rec)
{
    Provider().Flush(rec);
}
}  // namespace

void Register()
{
    bool expected = false;
    if (g_registered.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        // Static lifetime, never unregistered (BotProvider.h / docs/BOT-API-NOTES.md): a server shutdown only makes
        // available() false, and the engine empties its snapshot on the next tick. available() itself follows
        // TortoiseBots.Enable, set on the world thread at config load (SetEnabled): a disabled module has no bot API.
        fl::registerBotProvider(&Provider());
        run::SetFlushListener(&FlushRun);
    }
}

void SetEnabled(bool enabled)
{
    // Finding 8: one atomic word, read from map threads; the world thread writes it after every config load. A shutdown is
    // final: a later config load never makes the API available again.
    if (g_shutdown.load(std::memory_order_relaxed))
        return;
    g_available.store(enabled, std::memory_order_relaxed);
}

void Shutdown()
{
    g_shutdown.store(true, std::memory_order_relaxed);
    g_available.store(false, std::memory_order_release);
}

void Invalidate(ObjectGuid const& bot)
{
    if (!g_available.load(std::memory_order_relaxed))
        return;   // nothing reads the cache while the API is off
    std::lock_guard<std::mutex> lock(g_invalidMutex);
    g_invalid.insert(halves(bot));
}
}  // namespace luabots
}  // namespace TortoiseBots

#endif  // TORTOISEBOTS_SLICE1 && TBF_WITH_FOREVER_LUA
