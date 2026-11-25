// Slice 5, host package H5: see TbRun.h. Built with TORTOISEBOTS_SLICE1 only.
#include "host/run/TbRun.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "ai/playerbot/BossTacticsMgr.h"
#include "ai/playerbot/DungeonPlanMgr.h"
#include "host/TbCc.h"
#include "host/TbCcPure.h"
#include "host/run/TbPlanUnits.h"
#include "host/run/TbRunBindingPure.h"
#include "host/run/TbRunFacts.h"
#include "host/run/TbRunOrders.h"
#include "host/run/TbRunPlayerLed.h"
#include "host/run/TbRunPlayerLedPure.h"
#include "host/TbOrdersApi.h"
#include "host/run/TbRunStands.h"
#include "host/run/TbRunTasks.h"
#include "runtime/DecisionCapture.h"
#include "runtime/SystemRunPolicy.h"
#include "slice/SliceHybridPolicy.h"

#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"

#include "TbBotMgr.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/Event.h"
#include "PlayerbotAIStorage.h"
#include "runtime/BotManager.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>

namespace TortoiseBots
{
namespace run
{
namespace
{
std::map<ObjectGuid, std::unique_ptr<RunRec>>& Table()
{
    static std::map<ObjectGuid, std::unique_ptr<RunRec>> table;
    return table;
}

// pulls = the pull steps of the plan; done = the pull steps the run has passed (an index below the current step, all of them once the run is done by reaching the plan's end, not when it was stopped) that
// it did not record as skipped, deferred or given up (the Run's own lists), so a pull the run left uncleared is not counted done.
void CountPulls(RunRec const& rec, uint32& pulls, uint32& done)
{
    pulls = 0;
    done = 0;
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan)
        return;
    size_t const passed = (rec.run.state == DungeonRun::State::Done && !rec.stopped) ? plan->steps.size() : std::min(rec.run.step, plan->steps.size());
    auto listed = [](std::vector<size_t> const& list, size_t index) { return std::find(list.begin(), list.end(), index) != list.end(); };
    for (size_t i = 0; i < plan->steps.size(); ++i)
    {
        if (plan->steps[i].kind != ai::PlanStepKind::Pull)
            continue;
        ++pulls;
        if (i >= rec.run.startStep && i < passed && !rec.run.notCleared.count(i) && !listed(rec.run.unreachableSkips, i) && !listed(rec.run.noPathSkips, i)
            && !listed(rec.run.deferredPulls, i) && !listed(rec.run.deferGivenUp, i))
            ++done;
    }
}

// `.tb run start <tank>` names the run's tank, so the order carries the role too: every admission test and every read of the run's
// tank answers from it, and the kit (Defensive Stance for a warrior, bear form for a druid) is rebuilt from it. Without it the
// automatic pick could give the role to another warrior in the party and refuse the named one. A class that cannot lead a run is
// left alone: StartCheck names that refusal. Returns true when the role was committed by this start.
bool PrepareRunTank(Player* tank)
{
    if (!tank || PlayerbotAIStorage::Instance().GetPlayerForcedRole(tank->GetGUID()) == BOT_ROLE_TANK)
        return false;
    if (!TortoiseBots::slice::hybrid::CanLeadRun(unsigned(tank->GetClass())))
        return false;
    std::string code, detail;
    return h3::SetBotRole(tank, BOT_ROLE_TANK, code, detail);
}

void LogRunTank(Player* tank, bool roleSetNow)
{
    if (!tank)
        return;
    sLog.outString("TortoiseBots run: tank %s (role %s, AI IsTank %s)", tank->GetName().c_str(),
        roleSetNow ? "set by run start" : "already tank", PlayerbotAI::IsTank(tank) ? "yes" : "no");
}
}

RunRec* Find(ObjectGuid leader)
{
    auto const it = Table().find(leader);
    return it == Table().end() ? nullptr : it->second.get();
}

namespace
{
EndListener EndHook = nullptr;
FlushListener FlushHook = nullptr;

// TBF-B1 review finding 3: a monotonic id for every record this table creates (the provider's run cursor keys on it).
uint32 NextRunId()
{
    static uint32 id = 0;
    return ++id;
}
}

void SetEndListener(EndListener listener)
{
    EndHook = listener;
}

void SetFlushListener(FlushListener listener)
{
    FlushHook = listener;
}

RunRec* Create(ObjectGuid leader, std::string const& leaderName, ai::DungeonPlan const* plan, uint32 nowMs, size_t fromStep,
    bool captureTicks)
{
    if (!plan || leader.IsEmpty() || Table().count(leader))
        return nullptr;
    auto rec = std::make_unique<RunRec>();
    rec->leader = leader;
    rec->leaderName = leaderName;
    rec->planName = plan->name;
    rec->run = DungeonRun::Start(plan, fromStep);
    rec->run.startStep = fromStep;      // a placed start: a walk back never goes before it (DungeonRun::ReplayFloor)
    rec->startedMs = nowMs;
    rec->runId = NextRunId();
    RunRec* raw = rec.get();
    Table()[leader] = std::move(rec);
    // Decision capture (opt-in, test servers): the run leader's policy facts for this run, one JSONL capture. A
    // player-led run asks for none: nothing ticks the policy, so the capture would stay empty (#307).
    if (captureTicks)
        DecisionCaptureService::Instance().Begin(CaptureKey(leader), leaderName, plan->name, DecisionCaptureNowUnixMs());
    return raw;
}

bool Remove(ObjectGuid leader)
{
    auto const it = Table().find(leader);
    if (it == Table().end())
        return false;
    // TBF-B1 review finding 3: the record's unread events go to the provider BEFORE the record dies (the listener copies what it
    // needs), so a stop + restart in the same tick loses nothing. A removal without a listener (the option-off build has none) is
    // the same as before.
    if (FlushHook)
        FlushHook(*it->second);
    DecisionCaptureService::Instance().End(CaptureKey(leader), "run removed");
    Table().erase(it);
    return true;
}

std::vector<RunRec*> All()
{
    std::vector<RunRec*> out;
    for (auto const& [guid, rec] : Table())
        out.push_back(rec.get());
    std::sort(out.begin(), out.end(), [](RunRec const* a, RunRec const* b) { return a->leaderName < b->leaderName; });
    return out;
}

std::vector<ai::DungeonPlan const*> PlansInUse()
{
    std::vector<ai::DungeonPlan const*> out;
    for (auto const& [guid, rec] : Table())
        if (rec->run.plan && std::find(out.begin(), out.end(), rec->run.plan) == out.end())
            out.push_back(rec->run.plan);
    return out;
}

std::vector<std::string> PlanRunLeaders()
{
    std::vector<std::string> out;
    for (RunRec const* rec : All())
        if (rec->run.plan)
            out.push_back(rec->leaderName);
    return out;
}

uint32 AddEvent(RunRec& rec, uint32 nowMs, std::string kind, int32 step, int32 leg, int32 unit, std::string why)
{
    RunEvent event;
    event.seq = ++rec.eventSeq;
    event.tMs = nowMs >= rec.startedMs ? nowMs - rec.startedMs : 0;
    event.kind = std::move(kind);
    event.step = step;
    event.leg = leg;
    event.unit = unit;
    event.why = std::move(why);
    rec.events.push_back(std::move(event));
    while (rec.events.size() > kEventRing)
        rec.events.pop_front();
    return rec.eventSeq;
}

std::string StateName(RunRec const& rec)
{
    return rec.stopped ? "stopped" : DungeonRun::StateName(rec.run.state);
}

// #307: the decision capture's key (TbRun.h). One definition, so the tick, the
// command and the end all name the same capture.
std::uint64_t CaptureKey(ObjectGuid leader)
{
    return GuidKeyCompat(leader);
}

h3::Rec RunRecord(RunRec const& rec, uint32 nowMs)
{
    using h3::Rec;
    ai::DungeonPlan const* plan = rec.run.plan;
    size_t const steps = plan ? plan->steps.size() : 0;
    std::string const state = StateName(rec);
    Rec out("run");
    // `run` is the record's own id (#307): `.tb run decisions <id>` takes it in place of the leader's name, so a reader
    // that keeps the id across a restart of the run (a new run of the same leader gets a new id) names the run it means.
    out.S("lead", rec.leaderName).S("plan", rec.planName).N("run", int64(rec.runId)).S("state", state);
    if (IsPlayerLed(rec))
        out.S("mode", "player").S("commander", rec.commanderName);
    out.N("step", int64(std::min(rec.run.step, steps))).N("steps", int64(steps));
    // Where the run stands: the leg and kind of the step it is on (absent once it is past the last step: nothing is "current" then).
    if (plan && rec.run.step < steps)
    {
        ai::PlanStep const& step = plan->steps[rec.run.step];
        out.N("leg", step.leg).S("legname", plan->LegName(step.leg)).S("kind", ai::PlanStepKindName(step.kind));
    }
    out.S("phase", DungeonRun::PhaseName(rec.run.phase)).T("note", rec.run.note);
    uint32 pulls = 0, done = 0;
    CountPulls(rec, pulls, done);
    out.N("pull", done).N("pulls", pulls);
    std::vector<std::vector<std::string>> bosses;
    for (auto const& [entry, dead] : rec.bossDead)
        bosses.push_back({ std::to_string(entry), dead ? "dead" : "alive" });
    out.LP("boss", bosses);
    // One total under two names: `deaths` is what existing readers count, `member_deaths` says what it counts (every
    // member death edge, bots and real players included; the done note's wipe and lead-death counters are narrower).
    out.N("deaths", rec.deaths).N("member_deaths", rec.deaths).N("wipes", rec.wipes).N("pauses", rec.pauses);
    uint32 const until = rec.endedMs ? rec.endedMs : nowMs;
    out.N("elapsed", until >= rec.startedMs ? (until - rec.startedMs) / 1000u : 0u);
    // why= only when the run is paused (the run's own note says why) or stopped (the reason it was given), as addendum A5 does for `ready`.
    if (state == "stopped")
        out.T("why", rec.stopReason);
    else if (state == "paused")
        out.T("why", rec.run.note);
    if (rec.eventSeq)
        out.N("ev", rec.eventSeq);
    // S9 (C9): the bot of a loot / open / use_item step, its item and whether the carrier holds it, the object a step waits on and its state (plan words
    // ready | active | active_alt | none): absent on any other step, the grammar writes no empty value.
    if (!rec.carrier.empty())
        out.S("carrier", rec.carrier);
    if (!rec.itemKey.empty())
        out.S("item", rec.itemKey);
    if (!rec.objKey.empty())
        out.S("obj", rec.objKey);
    return out;
}

h3::Rec EventRecord(RunEvent const& event)
{
    using h3::Rec;
    Rec out("event");
    out.N("seq", event.seq).N("t", event.tMs / 1000u).S("kind", event.kind);
    if (event.step >= 0)
        out.N("step", event.step);
    if (event.leg >= 0)
        out.N("leg", event.leg);
    if (event.unit >= 0)
        out.N("unit", event.unit);
    out.T("why", event.why);
    return out;
}

// ======================================================================================================================================================
// S6 (package C6): the tick, the binding to the tank, stop / pause / resume. The decisions are the Turtle run leader's (DungeonRun::Tick); this part
// feeds it the facts of F6 (TbRunFacts.h) once a second, hands its one order a tick to the executor (package O6) and keeps the run's record.
// ======================================================================================================================================================
namespace
{
using DungeonRun::Phase;
using DungeonRun::State;

constexpr uint32 kMaxTickDtMs = 5000;   // a hitching server gives the policy at most this much time at once (its timers are in milliseconds of ticks)

TickTotals& TickCostRw()
{
    static TickTotals totals;
    return totals;
}

Player* InWorld(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

bool InCombatWithPet(Player* bot)
{
    if (bot->IsInCombat())
        return true;
    Pet* pet = bot->GetPet();
    return pet && pet->IsAlive() && pet->IsInWorld() && pet->IsInCombat();
}

// The members the run owns: the leader and every bound member (by guid, so a member that left the party is still looked at once).
std::vector<ObjectGuid> PartyGuids(RunRec const& rec)
{
    std::vector<ObjectGuid> out{ rec.leader };
    for (auto const& entry : rec.bound)
        out.push_back(entry.first);
    return out;
}

bool PartyInCombat(RunRec const& rec)
{
    for (ObjectGuid guid : PartyGuids(rec))
        if (Player* bot = InWorld(guid))
            if (bot->IsAlive() && InCombatWithPet(bot))
                return true;
    return false;
}

// Issue #139: the live run of a party. A bot-led run is keyed by the party leader (the tank bot); a player-led run by its tank bot too, but the party
// leader is the player, so it is found by the run's commander.
RunRec* RunOfGroupLeader(ObjectGuid groupLeader)
{
    if (RunRec* rec = Find(groupLeader))
        return rec;
    for (RunRec* rec : All())
        if (IsPlayerLed(*rec) && !Ended(*rec) && rec->commander == groupLeader)
            return rec;
    return nullptr;
}

// ---- what the run leader's order executor (package O6, TbRunOrders.h) does not cover: the end of a run and the run's own skull mark ----

// `stay` for the leader and every member at the end of a run, by the action `.tb order <bot> stay` runs (TbOrders.cpp `Run`: "stay chat shortcut"), called here
// at once on the world thread (the engines tick in BotMgr::Update before this tick, never inside it). Returns how many bots it worked on: the reply says so.
uint32 StayParty(RunRec const& rec)
{
    // A player-led run ends with the bots following the player again (their commander), not standing where the run left them.
    bool const playerLed = IsPlayerLed(rec);
    Player* const owner = InWorld(playerLed ? rec.commander : rec.leader);
    char const* const action = playerLed ? "follow chat shortcut" : "stay chat shortcut";
    uint32 stayed = 0;
    for (ObjectGuid guid : PartyGuids(rec))
    {
        Player* bot = InWorld(guid);
        PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
        if (!ai || !ai->GetAiObjectContext())
            continue;
        if (ai->DoSpecificAction(action, Event("tb order", std::string(), owner ? owner : bot), true))
            ++stayed;
    }
    return stayed;
}

// R6 (review item 1): everything the run still has in flight dies with it. The run's orders are queued (the executor's call) and run by the bots' engines at
// their next tick, up to 15 s later; a stop or a pause that came between the call and that tick must not be followed by a pull, a move, a focus attack or a
// rest. For the leader and every bound member: the queued orders of the run (fromRun) are removed (a hand-typed order stays), and a pull the run started (the
// tank's pull strategy, every member's join window and hold) is ended. Called BEFORE the final stay of a stop, on a pause and when a stop is pending.
void CancelRunWork(RunRec const& rec, bool endPulls)
{
    // Review fix 2: the executor's list of orders still waiting for their result goes with the queued orders. CancelRunOrders leaves the bot's result at
    // `fail:cancelled`; a pending entry left behind would be settled by the first tick after `.tb run resume` (TakeLateFailures) as a refusal of an order
    // the pause itself cancelled, and the policy would pause the run again for it.
    ForgetRunOrders(rec.leader);
    tasks::Cancel(rec.leader);              // S9 (O9): an open / loot task of the run ends with it (its answer is dropped, its loot watch given back)
    for (ObjectGuid guid : PartyGuids(rec))
    {
        h3::CancelRunOrders(guid);
        if (endPulls)
            if (Player* bot = InWorld(guid))
                h3::EndRunPull(bot);
    }
}

// The run's own skull mark on a unit (P's focus order, `DropFocus`) comes off, only where it still is (T: EndRunIcon).
void DropFocusMark(RunRec const& rec, uint64 handle)
{
    ObjectGuid unit;
    Player* lead = InWorld(rec.leader);
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (!group || !ResolveHandle(rec.leader, handle, unit))
        return;
    uint8 const icon = DungeonRun::kFocusIcon;
    if (group->GetTargetIcon(icon) == unit)
        group->SetTargetIcon(icon, ObjectGuid::Empty, rec.leader);
}

// G8 (review): the marks the run could not take off at its end. A finished run is not ticked any more and its record is replaced by the leader's next run, so
// the teardown cannot rely on either: this table outlives the record, and every world tick retries what is still pending.
struct PendingCc
{
    std::string mark;           // the run's recorded mark (CcMarkOwned still guards it on every attempt)
    std::string leaderName;     // the run whose cc order set it, for the log line
    bool reported = false;      // the first failure was logged (a bot that stays away does not repeat it every second)
};

std::map<std::string, PendingCc>& PendingCcTable()
{
    static std::map<std::string, PendingCc> table;
    return table;
}

void RetainPendingCc(RunRec const& rec, std::string const& bot, std::string const& mark)
{
    PendingCc& entry = PendingCcTable()[bot];
    entry.mark = mark;
    entry.leaderName = rec.leaderName;
    entry.reported = false;
}

// One pass of the pending clears: a bot that is not in the world keeps its entry, a clear the host refuses keeps it, and only a settled attempt drops it. The
// first failure is logged once; a retry that takes is logged too, so both ends of the wait are in the server log.
void RetryPendingCc()
{
    for (auto it = PendingCcTable().begin(); it != PendingCcTable().end();)
    {
        std::string const botName = it->first;
        PendingCc& entry = it->second;
        Player* bot = ObjectAccessor::FindPlayerByName(botName.c_str());
        bool const inWorld = bot && bot->IsInWorld();
        bool ownedMark = false, cleared = false;
        std::string code, detail;
        if (inWorld)
        {
            std::string mark, family;
            h8::CcOrderValues(bot, mark, family);
            ownedMark = h8::CcMarkOwned(entry.mark, mark);
            if (ownedMark)
            {
                cleared = h8::ClearCcOrder(nullptr, bot, entry.mark, ObjectGuid::Empty, code, &detail);
                if (!cleared)
                    detail = code.empty() ? std::string("the clear was refused") : code;
            }
        }
        else
            detail = "the bot is not in the world";
        if (h8::CcTeardownSettles(inWorld, ownedMark, cleared))
        {
            if (cleared)
                sLog.outString("TortoiseBots run: cc clear for %s (run of %s) took on retry", botName.c_str(), entry.leaderName.c_str());
            it = PendingCcTable().erase(it);
            continue;
        }
        if (!entry.reported)
        {
            sLog.outString("TortoiseBots run: cc clear for %s (run of %s) is pending: %s", botName.c_str(), entry.leaderName.c_str(), detail.c_str());
            entry.reported = true;
        }
        ++it;
    }
}

void Note(RunRec& rec, uint32 nowMs, char const* kind, int32 step, std::string why = {});   // defined below (the ring and the server log)

// G8: the crowd control the run set comes off when the run ends (T: ClearRunCcIcons): each raid icon of Run::ccIcons only while it is still on the unit the run
// put it on (an icon a player moved since stays), and every bot a run `cc` order marked gets its `rti cc` and `run cc family` back to none. The bots come from
// RunRec::ccOwned, not Run::ccHolds: a step advance, a resume or a regroup clears ccHolds and leaves the bots' values set. A bot the clear could not reach keeps
// its entry in the pending table (RetryPendingCc) and the run's event ring says so.
void ClearRunCc(RunRec& rec, uint32 nowMs)
{
    Player* lead = InWorld(rec.leader);
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (group)
        for (auto const& icon : DungeonRun::CcIconsToClear(rec.run))
        {
            ObjectGuid unit;
            if (icon.first >= 0 && icon.first < 8 && ResolveHandle(rec.leader, icon.second, unit) && group->GetTargetIcon(uint8(icon.first)) == unit)
                group->SetTargetIcon(uint8(icon.first), ObjectGuid::Empty, rec.leader);
        }
    for (auto const& owned : rec.ccOwned)
    {
        Player* bot = nullptr;
        for (ObjectGuid guid : PartyGuids(rec))
        {
            Player* member = InWorld(guid);
            if (member && member->GetName() == owned.first)
            {
                bot = member;
                break;
            }
        }
        if (!bot)
            bot = ObjectAccessor::FindPlayerByName(owned.first.c_str());
        bool const inWorld = bot && bot->IsInWorld();
        bool ownedMark = false, cleared = false;
        std::string failure = "not in the world";
        if (inWorld)
        {
            std::string mark, family;
            h8::CcOrderValues(bot, mark, family);
            ownedMark = h8::CcMarkOwned(owned.second, mark);
            if (ownedMark)
            {
                // The mark is named so that only the bot's own values reset: the icons were settled above (an empty unit takes no icon down), and a values-only
                // clear needs no party, so a bot that left the group is taken off the run's mark too.
                std::string code;
                cleared = h8::ClearCcOrder(lead, bot, owned.second, ObjectGuid::Empty, code);
                if (!cleared)
                    failure = code.empty() ? "failed" : code;
            }
            else
                failure = "another mark";
        }
        if (h8::CcTeardownSettles(inWorld, ownedMark, cleared))
            continue;   // nothing of the run's is left on the bot
        RetainPendingCc(rec, owned.first, owned.second);
        Note(rec, nowMs, "cc-clear", -1, owned.first + " " + failure);
    }
    rec.ccOwned.clear();
}

int32 UnitOfStep(ai::PlanStep const& step)
{
    if (step.entry)
        return int32(step.entry);
    return step.entries.empty() ? -1 : int32(step.entries.front());
}

// An event: the ring (`.tb run events`) and one server log line (`TortoiseBots run: ...`, which replaces the legacy run line).
void Note(RunRec& rec, uint32 nowMs, char const* kind, int32 step, std::string why)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    int32 leg = -1, unit = -1;
    if (plan && step >= 0 && size_t(step) < plan->steps.size())
    {
        leg = plan->steps[step].leg;
        ai::PlanStepKind const k = plan->steps[step].kind;
        if (k == ai::PlanStepKind::Pull || k == ai::PlanStepKind::Boss)
            unit = UnitOfStep(plan->steps[step]);
    }
    uint32 const seq = AddEvent(rec, nowMs, kind, step, leg, unit, why);
    sLog.outString("TortoiseBots run: %s #%u %s step=%d leg=%d unit=%d %s", rec.leaderName.c_str(), seq, kind, step, leg, unit, why.c_str());
}

// ---- the binding (decision D1): the tank is every other member's commander for the run; `.tb run stop` and the end of the run restore the old one ----
bool BindMember(RunRec& rec, Player* bot, Player* tank)
{
    ObjectGuid const guid = bot->GetGUID();
    RunRec::Binding previous;
    previous.master = BotMgr::Instance().MasterOf(guid);
    previous.commanded = !h3::CommanderGuid(guid).IsEmpty();
    if (!BotManager::Instance().BindBotMaster(guid, tank->GetGUID()))
        return false;
    h3::SetCommander(guid, tank->GetGUID());
    rec.bound[guid] = previous;
    rec.orderSeen[guid] = h3::LastOrder(guid).seq;
    return true;
}

void ReleaseMember(RunRec& rec, ObjectGuid guid)
{
    auto const it = rec.bound.find(guid);
    if (it == rec.bound.end())
        return;
    RunRec::Binding const previous = it->second;
    rec.bound.erase(it);
    rec.orderSeen.erase(guid);
    // Someone else rebound the bot meanwhile (`.tb master`, a re-login, `.tb group`): that is the newer decision, leave it.
    if (h3::CommanderGuid(guid) != rec.leader)
        return;
    if (previous.master.IsEmpty())
        BotManager::Instance().ClearBotMaster(guid);
    else
        BotManager::Instance().BindBotMaster(guid, previous.master);
    h3::SetCommander(guid, previous.commanded ? previous.master : ObjectGuid::Empty);
}

void ReleaseAll(RunRec& rec)
{
    std::vector<ObjectGuid> guids;
    for (auto const& entry : rec.bound)
        guids.push_back(entry.first);
    for (ObjectGuid guid : guids)
        ReleaseMember(rec, guid);
}

// A bot that joined the party mid-run is bound too (only when it is commanded already: its kit needs no rebuild); one that left is given back.
// A member the run bound keeps its binding through a pause and a regroup: the engine's own master logic can drop the commander entry or move the host
// master under the run's entry (SyncBindingAction), and the run puts its binding back; a commander somebody else set (`.tb master`) stays their decision.
BindingEntry BindingEntryOf(ObjectGuid stored, ObjectGuid lead)
{
    if (stored.IsEmpty())
        return BindingEntry::Missing;
    return stored == lead ? BindingEntry::Lead : BindingEntry::Other;
}

void SyncBinding(RunRec& rec, Group* group, Player* lead)
{
    std::set<ObjectGuid> inGroup;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        inGroup.insert(slot.guid);
        if (slot.guid == rec.leader || !BotMgr::Instance().IsBot(slot.guid))
            continue;
        bool const boundByRun = rec.bound.count(slot.guid) != 0;
        // A member whose master is away is waiting out its grace (BotMgr's detach): binding it again would clear that wait.
        bool away = false, blocked = false, hasBrain = false;
        uint32 awaySeconds = 0;
        std::string blockedWhy;
        BotMgr::Instance().FollowStatus(slot.guid, away, awaySeconds, blocked, blockedWhy, hasBrain);
        bool const masterIsLead = BotMgr::Instance().MasterOf(slot.guid) == rec.leader;
        if (SyncBindingAction(boundByRun, away, masterIsLead, BindingEntryOf(h3::StoredCommander(slot.guid), rec.leader)) == BindingSync::Reassert)
        {
            // The same host call BindMember makes (the host master and the engine's pointer), then the entry itself: the bind first, so a refused bind
            // leaves the commander table alone. Both agree afterwards, so the next tick is Keep and a member's away mark is not reset every tick.
            RepairBinding([&] { return BotManager::Instance().BindBotMaster(slot.guid, rec.leader); },
                          [&] { h3::SetCommander(slot.guid, rec.leader); });
            continue;
        }
        if (boundByRun)
            continue;
        if (Player* bot = InWorld(slot.guid))
            if (!h3::CommanderGuid(slot.guid).IsEmpty())
                BindMember(rec, bot, lead);
    }
    std::vector<ObjectGuid> left;
    for (auto const& entry : rec.bound)
        if (!inGroup.count(entry.first))
            left.push_back(entry.first);
    for (ObjectGuid guid : left)
        ReleaseMember(rec, guid);
}

void BaselineOrders(RunRec& rec)
{
    for (ObjectGuid guid : PartyGuids(rec))
        rec.orderSeen[guid] = h3::LastOrder(guid).seq;
}

// A `.tb order` typed between two ticks bumps the order seq of its bot (the run's own orders are all queued inside the executor call, after which the
// baseline is taken again): that is an order from outside, which pauses the run as T's PauseDungeonRunForPlayer does. No hook in the order command is needed.
bool OutsideOrder(RunRec const& rec, std::string& who, std::string& verb)
{
    for (auto const& [guid, seen] : rec.orderSeen)
    {
        h3::OrderStatus const status = h3::LastOrder(guid);
        if (status.seq == seen)
            continue;
        Player* bot = ObjectAccessor::FindPlayer(guid);
        who = bot ? bot->GetName() : h3::GuidText(guid);
        verb = status.verb;
        return true;
    }
    return false;
}

// Issue #139: a player-led run takes the player's own orders as his (PlayerLed::NotePlayerOrder): a move order is obeyed, a fight order is his pull, `stay`
// holds the next pull until his `follow`; none of them pauses the run (only `.tb run pause` and `.tb run stop` do). The baseline moves past each one.
void NotePlayerOrders(RunRec& rec)
{
    for (auto& [guid, seen] : rec.orderSeen)
    {
        h3::OrderStatus const status = h3::LastOrder(guid);
        if (status.seq == seen)
            continue;
        seen = status.seq;
        PlayerLed::NotePlayerOrder(rec.pl, rec.run, status.verb);
    }
}

// A player-led run orders only the roster it accepted at the start. A bot that left the party, or that somebody else commands now, drops out of the roster and is
// never ordered again (the Turtle rule: a later joiner is a party member the run never orders). Each drop is one `roster` event.
void SyncPlayerLedRoster(RunRec& rec, Group* group, uint32 nowMs)
{
    std::vector<std::pair<ObjectGuid, char const*>> dropped;
    for (auto const& entry : rec.bound)
    {
        if (!group->IsMember(entry.first))
            dropped.emplace_back(entry.first, "left the party");
        else if (h3::CommanderGuid(entry.first) != rec.commander)
            dropped.emplace_back(entry.first, "is commanded by somebody else now");
    }
    for (auto const& [guid, why] : dropped)
    {
        Player* bot = InWorld(guid);
        std::string const name = bot ? std::string(bot->GetName()) : h3::GuidText(guid);
        h3::CancelRunOrders(guid);         // what the run queued for it before the change dies with the roster entry (CancelRunWork cannot reach it after)
        if (bot)
            h3::EndRunPull(bot);
        rec.bound.erase(guid);
        rec.orderSeen.erase(guid);
        Note(rec, nowMs, "roster", -1, name + " " + why + ": the run no longer orders it");
    }
}

void NoteStepEnd(RunRec& rec, uint32 nowMs, size_t stepIndex);     // S9 (C9), defined with the other step-event helpers below

// The summary's boss time: the first time the run read a boss dead, in ms since the run started. The dead flag is
// kept as the policy and `.tb run status` read it.
void NoteBossKilled(RunRec& rec, uint32 nowMs, uint32 entry)
{
    rec.bossDead[entry] = true;
    rec.bossKilledMs.emplace(entry, nowMs - rec.startedMs);
}

char const* SpotRole(Player* bot)
{
    if (PlayerbotAI::IsTank(bot))
        return "tank";
    if (PlayerbotAI::IsHeal(bot))
        return "heal";
    return "dps";
}

void NoteSpot(std::vector<RunSpot>& list, RunRec const& rec, uint32 nowMs, float x, float y, float z, char const* role)
{
    if (list.size() >= kMaxSummarySpots)
        return;
    RunSpot spot;
    spot.tMs = nowMs - rec.startedMs;
    spot.x = x;
    spot.y = y;
    spot.z = z;
    spot.role = role ? role : "";
    list.push_back(std::move(spot));
}

// ---- events and counters from what a tick saw ----
void Observe(RunRec& rec, uint32 nowMs, DungeonRun::Facts const* f)
{
    DungeonRun::Run const& run = rec.run;
    ai::DungeonPlan const* plan = run.plan;
    size_t const steps = plan ? plan->steps.size() : 0;
    if (!rec.seenInit)
    {
        rec.seenInit = true;
        rec.seenState = run.state;
        rec.seenPhase = run.phase;
        rec.seenStep = run.step;
    }
    if (run.step != rec.seenStep)
    {
        for (size_t i = rec.seenStep; i < run.step && i < steps; ++i)
        {
            if (plan->steps[i].kind == ai::PlanStepKind::Boss && !run.notCleared.count(i) && plan->steps[i].entry)
                NoteBossKilled(rec, nowMs, plan->steps[i].entry);
            NoteStepEnd(rec, nowMs, i);             // S9 (C9)
        }
        if (run.step < steps)
            Note(rec, nowMs, "step", int32(run.step));
        rec.seenStep = run.step;
    }
    int32 const current = run.step < steps ? int32(run.step) : -1;
    if (run.state != rec.seenState)
    {
        if (run.state == State::Paused)
        {
            ++rec.pauses;
            CancelRunWork(rec, true);       // R6: a paused run has no order in flight and no pull hold (the order of this very tick included)
            Note(rec, nowMs, "pause", current, rec.pauseCode.empty() ? run.note : rec.pauseCode);
        }
        else if (run.state == State::Running)
            Note(rec, nowMs, "resume", current);
        rec.seenState = run.state;
        rec.pauseCode.clear();
    }
    if (!f)
        return;
    if (run.phase == Phase::Rest && rec.seenPhase != Phase::Rest)
        Note(rec, nowMs, "rest", current);
    rec.seenPhase = run.phase;
    if (rec.seenCombat && !f->anyInCombat)
        Note(rec, nowMs, "fight-end", current);
    rec.seenCombat = f->anyInCombat;
    std::set<std::string> dead(f->deadNames.begin(), f->deadNames.end());
    for (std::string const& name : dead)
        if (!rec.seenDead.count(name))
        {
            ++rec.deaths;
            Note(rec, nowMs, "death", current, name);
            if (Player* fallen = ObjectAccessor::FindPlayerByName(name))
                NoteSpot(rec.deathSpots, rec, nowMs, fallen->GetPositionX(), fallen->GetPositionY(),
                    fallen->GetPositionZ(), SpotRole(fallen));
        }
    rec.seenDead = std::move(dead);
    bool const wipe = f->total > 0 && f->alive == 0;
    if (wipe && !rec.seenWipe)
    {
        ++rec.wipes;
        Note(rec, nowMs, "wipe", current);
        // The spot is the centroid of the party's dead, and the role of the first one in the run's own order.
        float x = 0.0f, y = 0.0f, z = 0.0f;
        uint32 fallen = 0;
        char const* firstRole = nullptr;
        for (ObjectGuid guid : PartyGuids(rec))
            if (Player* member = InWorld(guid))
                if (!member->IsAlive())
                {
                    x += member->GetPositionX();
                    y += member->GetPositionY();
                    z += member->GetPositionZ();
                    if (!firstRole)
                        firstRole = SpotRole(member);
                    ++fallen;
                }
        if (fallen)
            NoteSpot(rec.wipeSpots, rec, nowMs, x / fallen, y / fallen, z / fallen, firstRole);
    }
    rec.seenWipe = wipe;
    // The boss the run stands at: read dead or alive only from what the facts really read (a boss never read is not listed).
    if (current >= 0 && plan->steps[current].kind == ai::PlanStepKind::Boss && plan->steps[current].entry)
    {
        uint32 const entry = plan->steps[current].entry;
        if (f->bossOwnDead || f->bossDeadNearSpot)
            NoteBossKilled(rec, nowMs, entry);
        else if (f->bossSearchRead && f->bossSearchAlive)
        {
            auto const it = rec.bossDead.find(entry);
            if (it == rec.bossDead.end() || !it->second)
                rec.bossDead[entry] = false;
        }
    }
}

// The run ends (stopped or done): the party stays, the binding goes back, the facts glue forgets the run. The record stays for `status` and `events`.
uint32 Finish(RunRec& rec, uint32 nowMs, bool stopped, std::string const& why)
{
    Observe(rec, nowMs, nullptr);
    CancelRunWork(rec, true);               // R6: queued run orders and a run pull die before the final stay
    h3::HoldRunLead(rec.leader, false);
    uint32 const stayed = StayParty(rec);
    if (rec.run.focusIconClear)
    {
        DropFocusMark(rec, rec.run.focusIconClear);
        rec.run.focusIconClear = 0;
    }
    ClearRunCc(rec, nowMs);
    ReleaseAll(rec);
    rec.stopPending = false;
    rec.stopped = stopped;
    if (stopped)
        rec.stopReason = why;
    rec.endedMs = nowMs ? nowMs : 1;
    DecisionCaptureService::Instance().End(CaptureKey(rec.leader), why);
    ForgetRun(rec.leader);
    ForgetRunOrders(rec.leader);
    tasks::Forget(rec.leader);              // S9 (O9)
    ai::DungeonPlan const* plan = rec.run.plan;
    int32 const current = plan && rec.run.step < plan->steps.size() ? int32(rec.run.step) : -1;
    Note(rec, nowMs, stopped ? "stop" : "done", current, why);
    if (EndHook)
        EndHook(rec);
    return stayed;
}

uint32 EndEarly(RunRec& rec, uint32 nowMs, std::string const& note)
{
    DungeonRun::EndRunEarly(rec.run, note);
    return Finish(rec, nowMs, true, note);
}

void PauseWith(RunRec& rec, char const* code)
{
    rec.pauseCode = code;
}

// ---- S9 (package C9): the keys and the step-end events of the object, wait and loot steps ----
char const* ObjectWord(int planState)
{
    switch (planState)
    {
        case 0: return "ready";
        case 1: return "active";
        case 2: return "active_alt";
        default: return "none";
    }
}

// One event of the ring and the log, on a given step (the step that just ended, not the one the run is on now).
void NoteStepEvent(RunRec& rec, uint32 nowMs, char const* kind, size_t stepIndex, int32 unit, std::string const& why)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    int32 step = -1, leg = -1;
    if (plan && stepIndex < plan->steps.size())
    {
        step = int32(stepIndex);
        leg = plan->steps[stepIndex].leg;
    }
    uint32 const seq = AddEvent(rec, nowMs, kind, step, leg, unit, why);
    sLog.outString("TortoiseBots run: %s #%u %s step=%d leg=%d unit=%d %s", rec.leaderName.c_str(), seq, kind, step, leg, unit, why.c_str());
}

// What the facts of this tick say about the step the policy is about to decide on: the run keys (`carrier=`, `item=`, `obj=`) and the numbers the
// step-end events read. The tick that ends a step read them BEFORE it decided, so Observe reads them for the step that ended.
void SnapshotStepFacts(RunRec& rec, DungeonRun::Facts const& f)
{
    rec.carrier.clear();
    rec.itemKey.clear();
    rec.objKey.clear();
    rec.preStep = size_t(-1);
    rec.preObjState = -1;
    rec.preClearNear = 0;
    rec.preHasItem = false;
    rec.preHolder.clear();
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan || rec.run.step >= plan->steps.size())
        return;
    ai::PlanStep const& step = plan->steps[rec.run.step];
    rec.preStep = rec.run.step;
    switch (step.kind)
    {
        case ai::PlanStepKind::WaitObject:
            rec.preObjState = f.objectState;
            rec.objKey = std::to_string(step.entry) + ":" + ObjectWord(f.objectState);
            break;
        case ai::PlanStepKind::WaitClear:
            rec.preClearNear = DungeonRun::WaitClearCount(step, f);
            break;
        case ai::PlanStepKind::Loot:
        case ai::PlanStepKind::Open:
        case ai::PlanStepKind::UseItem:
        {
            rec.carrier = f.carrier;
            bool const loot = step.kind == ai::PlanStepKind::Loot;
            if (step.item)
            {
                rec.preHasItem = f.carrierHasItem || (loot && f.partyHasItem);
                rec.preHolder = f.carrierHasItem ? f.carrier : (loot && f.partyHasItem ? f.itemHolder : std::string());
                rec.itemKey = std::to_string(step.item) + (rec.preHasItem ? ":held" : ":missing");
            }
            break;
        }
        default:
            break;
    }
}

// The events of a step that ended on what the policy saw (never a step that was skipped or timed out): a wait_object that read its target state, a wait_clear
// that read no hostile, a loot / open / use_item whose item the party held.
void NoteStepEnd(RunRec& rec, uint32 nowMs, size_t stepIndex)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan || stepIndex >= plan->steps.size() || rec.preStep != stepIndex)
        return;
    ai::PlanStep const& step = plan->steps[stepIndex];
    switch (step.kind)
    {
        case ai::PlanStepKind::WaitObject:
            if (rec.preObjState >= 0 && uint32(rec.preObjState) == step.param)
                NoteStepEvent(rec, nowMs, "object", stepIndex, int32(step.entry), std::to_string(step.entry) + ":" + ObjectWord(rec.preObjState));
            break;
        case ai::PlanStepKind::WaitClear:
            if (rec.preClearNear == 0)
                NoteStepEvent(rec, nowMs, "clear", stepIndex, -1, "no hostile within " + std::to_string(int(step.radius)) + " yd of the spot");
            break;
        case ai::PlanStepKind::Loot:
        case ai::PlanStepKind::Open:
        case ai::PlanStepKind::UseItem:
            if (step.item && rec.preHasItem)
                NoteStepEvent(rec, nowMs, "item", stepIndex, int32(step.entry), "item " + std::to_string(step.item) + " is in " + rec.preHolder + "'s bags");
            break;
        default:
            break;
    }
}

// A loot step for an item, about to go out, with the carrier's bags at the floor: the carrier would leave the item (the bag floor of the loot code, spec 5d),
// so the run says so and pauses instead of looting and then finding the item missing. Free slots are read from the carrier's own bags.
bool BagsFullForStep(RunRec const& rec, DungeonRun::Facts const& f, std::string& why)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan || rec.run.step >= plan->steps.size() || rec.run.orderSent)
        return false;
    ai::PlanStep const& step = plan->steps[rec.run.step];
    if (step.kind != ai::PlanStepKind::Loot || !step.item || f.carrierHasItem || f.partyHasItem || f.carrier.empty())
        return false;
    Player* carrier = ObjectAccessor::FindPlayerByName(f.carrier);
    if (!carrier || !carrier->IsInWorld())
        return false;
    uint32 const freeSlots = carrier->GetFreeInventorySlotCount();
    if (freeSlots > tasks::kFreeSlotsFloor)
        return false;
    why = "bags-full: " + f.carrier + " has " + std::to_string(freeSlots) + " free bag slots (the loot code keeps " + std::to_string(tasks::kFreeSlotsFloor) +
        "): item " + std::to_string(step.item) + " would be left on the corpse. Free some slots, then .tb run resume";
    return true;
}

// ---- one second of one run ----
void TickRun(RunRec& rec, uint32 nowMs)
{
    DungeonRun::Run& run = rec.run;
    ai::DungeonPlan const* plan = run.plan;
    uint32 dt = rec.lastTickMs && nowMs > rec.lastTickMs ? nowMs - rec.lastTickMs : DungeonRun::kTickMs;
    dt = std::min(dt, kMaxTickDtMs);
    rec.lastTickMs = nowMs;
    ++rec.ticks;

    // The run's own time limit (AiPlayerbot.DungeonRunMaxMinutes): it ends like a stop, so a fight in progress is not cut.
    uint32 const maxMinutes = sPlayerbotAIConfig.dungeonRunMaxMinutes;
    if (!rec.stopPending && run.state != State::Done && DungeonRun::RunTimeLimitReached(nowMs - rec.startedMs, maxMinutes))
        StopRun(rec, nowMs, DungeonRun::RunTimeLimitNote(maxMinutes));
    if (Ended(rec))
        return;
    // A stop that came in a fight: no new order and no policy tick until the fight is over, then the party stays.
    if (rec.stopPending)
    {
        if (!PartyInCombat(rec))
            EndEarly(rec, nowMs, rec.stopWhy);
        else if (nowMs - rec.stopPendingSinceMs >= kStopPendingMaxMs)
            EndEarly(rec, nowMs, DungeonRun::RunStopNote("stopped after " + std::to_string(kStopPendingMaxMs / 1000) + " s in combat: " + rec.stopWhy));   // R6 item 6
        return;
    }
    if (run.state == State::Done)
    {
        Finish(rec, nowMs, false, run.note);
        return;
    }

    Player* lead = InWorld(rec.leader);
    Group* group = lead ? lead->GetGroup() : nullptr;
    bool const playerLed = IsPlayerLed(rec);
    // A player-led run: the player leads the party, and the mode ends when the key is switched off (a config reload), as the Turtle run does.
    if (lead && (!group || group->GetLeaderGUID() != (playerLed ? rec.commander : rec.leader)))
    {
        EndEarly(rec, nowMs, DungeonRun::RunStopNote(playerLed ? "the party no longer has you as its leader" : "the party no longer has this leader"));
        return;
    }
    if (playerLed && !PlayerLedKeyOn())
    {
        EndEarly(rec, nowMs, DungeonRun::RunStopNote("player-led runs were switched off (AiPlayerbot.DungeonRunPlayerLed)"));
        return;
    }
    // R6 (review item 4): the run belongs to ONE instance of the plan's map. A leader that stands on the plan's map in ANOTHER instance (a different copy: it
    // was moved, or it re-entered a new one) is not "back": its plan facts are those of another dungeon, so the run ends (do less), it never resumes there.
    if (lead && lead->IsInWorld() && !lead->IsBeingTeleported() && lead->GetMapId() == plan->map && rec.instanceId && lead->GetInstanceId() != rec.instanceId)
    {
        EndEarly(rec, nowMs, DungeonRun::RunStopNote("the lead bot is in another instance of the map"));
        return;
    }
    if (group && !playerLed)            // a player-led run binds nothing: the bots keep the player as their commander
        SyncBinding(rec, group, lead);
    if (group && playerLed)
    {
        if (h3::CommanderGuid(rec.leader) != rec.commander)
        {
            EndEarly(rec, nowMs, DungeonRun::RunStopNote("the tank bot is no longer commanded by you"));
            return;
        }
        SyncPlayerLedRoster(rec, group, nowMs);
    }

    // A real player in the group pauses a system run until the group is bots only again (T: L6; the binding stands meanwhile). A player-led run has one by design.
    bool realPlayer = false;
    if (group && !playerLed)
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (!BotMgr::Instance().IsBot(slot.guid))
                realPlayer = true;
    rec.pausedForPlayer = SystemRun::JoinFlagAfterScan(run.state == State::Running, realPlayer, rec.pausedForPlayer);
    switch (SystemRun::DecideJoinStep(run.state == State::Running, run.state == State::Paused, rec.pausedForPlayer, realPlayer))
    {
        case SystemRun::JoinStep::Pause:
            DungeonRun::PausePlayer(run, SystemRun::kPlayerJoinedNote);
            rec.pausedForPlayer = true;
            PauseWith(rec, "player-in-group");
            break;
        case SystemRun::JoinStep::Resume:
            rec.pausedForPlayer = false;
            DungeonRun::Resume(run);
            BaselineOrders(rec);
            break;
        case SystemRun::JoinStep::None:
            break;
    }

    // ---- paused: only the pauses that end by themselves are watched ----
    if (run.state == State::Paused)
    {
        if (run.leadOffMapPaused)
        {
            bool const back = lead && !lead->IsBeingTeleported() && lead->IsAlive() && lead->GetMapId() == plan->map;
            if (DungeonRun::LeadBackWhilePaused(run, back, dt))
                BaselineOrders(rec);
        }
        else if (run.ceilingPaused && lead && lead->GetMapId() == plan->map)
        {
            if (DungeonRun::CeilingFightOverWhilePaused(run, PartyInCombat(rec), dt))
                BaselineOrders(rec);
        }
        Observe(rec, nowMs, nullptr);
        return;
    }

    // ---- running ----
    // The leader is not in the world (between maps, logged out): the run waits, and goes on by itself once it is back (LeadBackWhilePaused).
    if (!lead || !lead->IsInWorld())
    {
        DungeonRun::PauseLeadOffMap(run, plan->map);
        PauseWith(rec, "lead-off-map");
        Observe(rec, nowMs, nullptr);
        return;
    }
    // A living leader off the plan's map pauses; a dead one is a ghost on the graveyard's map and the policy's regroup deals with it.
    if (lead->IsAlive() && lead->GetMapId() != plan->map)
    {
        DungeonRun::PauseLeadOffMap(run, plan->map);
        PauseWith(rec, "lead-off-map");
        Observe(rec, nowMs, nullptr);
        return;
    }
    std::string who, verb;
    if (playerLed)
        NotePlayerOrders(rec);
    else if (OutsideOrder(rec, who, verb))
    {
        DungeonRun::PausePlayer(run, "outside-order: " + who + " was ordered by hand (" + verb + "): .tb run resume goes on");
        PauseWith(rec, "outside-order");
        BaselineOrders(rec);
        Observe(rec, nowMs, nullptr);
        return;
    }

    // Player-led: where the player stands picks the step before its facts are read (PlayerLed::Pick); the anchor is what Tick reads too.
    PlayerLed::Anchor anchor;
    if (playerLed)
        anchor = PlayerLedPick(rec, lead);

    // A pull step none of whose members the unit rows matched to a spawn of this map is skipped as not cleared, never guessed (spec 5.3 "Pack lookup").
    if (ai::PlanStep const* step = DungeonRun::Current(run); step && step->kind == ai::PlanStepKind::Pull &&
        (rec.pullChecked != run.step || rec.pullCheckedPass != run.stepPass))
    {
        rec.pullChecked = run.step;
        rec.pullCheckedPass = run.stepPass;
        if (StepMatchedMembers(rec, run.step) == 0)
        {
            size_t const skipped = run.step;
            run.notCleared.insert(skipped);
            Note(rec, nowMs, "step", int32(skipped), "not-cleared: no member of the step matched a spawn of this map");
            if (playerLed)
            {
                // The picker's own cursor: the pack is done (not cleared), the player walks on to the next one.
                rec.pl.done.insert(skipped);
                rec.pl.picked = PlayerLed::kNone;
                rec.run.note = "step " + std::to_string(skipped + 1) + " skipped, not cleared: no member of it matched a spawn of this map";
            }
            else
                DungeonRun::NextStep(run, "pack skipped, not cleared: no member of step " + std::to_string(skipped + 1) + " matched a spawn of this map");
            rec.seenStep = run.step;
            if (run.state == State::Done)
                Finish(rec, nowMs, false, run.note);
            else
                Observe(rec, nowMs, nullptr);
            return;
        }
    }

    // ---- facts, decision, post-tick answers, the one order ----
    DungeonRun::Facts facts;
    if (!ReadFacts(rec, nowMs, facts))
    {
        DungeonRun::PauseLeadOffMap(run, plan->map);
        PauseWith(rec, "lead-off-map");
        Observe(rec, nowMs, nullptr);
        return;
    }
    DungeonRun::ApplyPlayerFacts(facts);
    // S9 (C9): group loot or need-before-greed is the run's rule for every tick, not only at the start. A method changed under a running run would let a
    // chest or a corpse pay out under rules the policy does not know (rolls it cannot vote, items to chance). The pending loot task dies before its act,
    // and the run pauses; the resume checks the method again, so group loot or need-before-greed has to be back first.
    if (std::string methodWhy; !tasks::RunLootMethodOk(facts.lootMethod, methodWhy))
    {
        tasks::Cancel(rec.leader);
        DungeonRun::Pause(run, methodWhy + ": the run waits for group loot or need-before-greed (set it, then .tb run resume)");
        PauseWith(rec, "loot-method");
        Observe(rec, nowMs, &facts);
        return;
    }
    SnapshotStepFacts(rec, facts);              // S9 (C9): the run keys and the step-end events read what the policy is about to decide on
    run.liveTactics = &ai::GetBossTactics();
    // An order the executor accepted earlier failed when the bot's engine ran it: the policy hears it as any refusal, before it decides this tick.
    std::vector<LateFailure> late;
    TakeLateFailures(rec, nowMs, late);
    for (LateFailure const& failure : late)
        DungeonRun::OrderRefused(run, failure.order, failure.text);
    if (run.state != State::Running)
    {
        Observe(rec, nowMs, &facts);
        return;
    }
    // S9 (C9): a loot step for an item whose carrier has no room for it pauses here with the reason (the loot code would leave the item, and the step would
    // end "done, but the item is not in the bags").
    if (std::string bagsWhy; BagsFullForStep(rec, facts, bagsWhy))
    {
        DungeonRun::Pause(run, bagsWhy);
        PauseWith(rec, "bags-full");
        Observe(rec, nowMs, &facts);
        return;
    }
    // Decision capture wraps the bot-led policy tick only: a player-led run is a different driver (TB's hook sites).
    uint64_t const captureKey = CaptureKey(rec.leader);
    if (!playerLed)
        DecisionCaptureService::Instance().BeforeTick(captureKey, run, facts, dt);
    std::string const order = playerLed ? PlayerLed::Tick(run, rec.pl, facts, anchor, dt) : DungeonRun::Tick(run, facts, dt);
    if (!playerLed)
        DecisionCaptureService::Instance().AfterTick(captureKey, run, order);
    // G8 (item 5): the regroup's gather ceiling ended in a pause. A revived member that is still walking back (or drinking first) is waited for a bounded time more
    // by the policy's own resume (a fresh regroup, its gather ceiling sized by the farthest bot), instead of a pause nobody asked for; the host has no `summon`
    // (decision D3), this is the choice that does less.
    if (run.state == State::Paused && gather::IsRegroupGatherPause(run.note))
    {
        gather::Decision const verdict = gather::Decide(rec.gatherExt, run.step, run.note, facts.maxMemberYd, facts.leadAlive);
        int32 const at = run.step < plan->steps.size() ? int32(run.step) : -1;
        if (verdict.extend && DungeonRun::Resume(run))
            Note(rec, nowMs, "gather-wait", at, verdict.why);
        else if (!verdict.why.empty())
            Note(rec, nowMs, "gather-stop", at, verdict.why);
    }
    // proof-1 B: the policy's own regroups at a step did not hold (its pause note says so, both sites). The host answers that pause itself, once per step and
    // at most kMaxRecoversPerRun per run, with what a player party does: the dead release their spirits (the core revives them at the entrance, D2) and the
    // policy's own resume runs a fresh regroup (revive wait, gather, rest, the wc-r9 walk back that pulls the respawned packs one at a time). No teleport,
    // no GM revive, no summon; the order goes out from the run, so it is not an outside order.
    if (run.state == State::Paused && recover::IsRegroupsDidNotHoldPause(run.note))
    {
        bool const optionOn = sConfigMgr->GetBoolDefault("TortoiseBots.Run.SelfRecover", true);
        recover::Decision const verdict = recover::Decide(rec.recover, run.step, run.note, facts.maxMemberYd, optionOn);
        int32 const at = run.step < plan->steps.size() ? int32(run.step) : -1;
        if (verdict.recover)
        {
            bool deadToRelease = false;
            if (Group* group = lead ? lead->GetGroup() : nullptr)
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    Player* member = ObjectAccessor::FindPlayer(slot.guid);
                    if (member && member->IsInWorld() && BotMgr::Instance().IsBot(member->GetGUID()) && !member->IsAlive() &&
                        !member->HasPlayerFlag(PLAYER_FLAGS_GHOST))
                    {
                        deadToRelease = true;
                        break;
                    }
                }
            if (deadToRelease)
            {
                Issue(rec, "revive party", nowMs);
                BaselineOrders(rec);
            }
            DungeonRun::Resume(run);
            Note(rec, nowMs, "self-recover", at, verdict.why);
        }
        else if (!verdict.why.empty())
            Note(rec, nowMs, "self-recover-stop", at, verdict.why);
    }
    // The searches the policy asks the glue for after its tick (Commander.cpp:13941-13971), once per request, here and never per tick (N6: ported in
    // TbRunStands.cpp; before N6 both answered "found nothing"). A stalled walk asks for a dry stand-in near the step's point, an approach whose hops
    // got no nearer to a pack or boss asks for a stand with sight of it.
    if (run.stallStandInWanted)
    {
        float dx = 0.0f, dy = 0.0f, dz = 0.0f;
        StandCost cost;
        bool const found = lead->IsAlive() && SearchStallStand(rec, dx, dy, dz, &cost);
        sLog.outString("TortoiseBots run: %s stall stand-in search: %s (%u points, %u paths, %u us)", rec.leaderName.c_str(), found ? "found" : "none", cost.candidates,
            cost.pathQueries, cost.costUs);
        DungeonRun::StallStandIn(run, found, dx, dy, dz);
    }
    if (run.approachStandWanted)
    {
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        StandCost cost;
        bool const found = lead->IsAlive() && SearchApproachStand(rec, facts.team, sx, sy, sz, &cost);
        sLog.outString("TortoiseBots run: %s approach stand search: %s (%u points, %u paths, %u rays, %u us)", rec.leaderName.c_str(),
            found ? "found" : "none", cost.candidates, cost.pathQueries, cost.losQueries, cost.costUs);
        DungeonRun::ApproachStandFound(run, facts, found, sx, sy, sz);
    }
    (void)DungeonRun::SpeedRateNow(run, facts, dt);     // test speed is off (1.0): this touches nothing, the policy keeps its own clock
    // What the policy leaves for the glue to do, and this host does not: dropped with the reason (a flag left set would only repeat).
    (void)DungeonRun::TakeLinkedEvents(run);
    if (std::string const say = DungeonRun::TakeSay(run); !say.empty())
        sLog.outString("TortoiseBots run: %s says: %s", rec.leaderName.c_str(), say.c_str());
    if (!run.deferLog.empty())
    {
        sLog.outString("TortoiseBots run: %s: %s", rec.leaderName.c_str(), run.deferLog.c_str());
        run.deferLog.clear();
    }
    run.leadWalkReset = false;
    run.stopReachDue = false;
    run.ccLandedDrop.clear();
    if (run.focusIconClear)
    {
        DropFocusMark(rec, run.focusIconClear);
        run.focusIconClear = 0;
    }

    if (!order.empty())
    {
        OrderAnswer const answer = Issue(rec, order, nowMs, DungeonRun::IsGatherOrder(run, order));
        // G8: a refused automatic cc order assigned nothing: the unit, bot, mark and budget go back for the next pick (the Turtle glue calls it after every order).
        DungeonRun::CcOrderTaken(run, order, answer.accepted);
        if (answer.accepted)
        {
            if (answer.movePathRead)
                DungeonRun::NoteMovePathType(run, order, answer.movePathType);
            int32 const at = run.step < plan->steps.size() ? int32(run.step) : -1;
            if (order.compare(0, 5, "pull ") == 0 || order.compare(0, 9, "pullback ") == 0)
                Note(rec, nowMs, "pull", at);
            else if (order.compare(0, 6, "revive") == 0)
                Note(rec, nowMs, "revive", at, order);
            else if (order.compare(0, 3, "cc ") == 0)
                Note(rec, nowMs, "cc", at, order);
        }
        else if (!DungeonRun::StallDirectRefused(run, order, answer.text))
            DungeonRun::OrderRefused(run, order, answer.text);
        // Everything the executor queued above is the run's own: the baseline of the outside-order check moves past it.
        BaselineOrders(rec);
    }
    Observe(rec, nowMs, &facts);
    if (run.state == State::Done)
        Finish(rec, nowMs, false, run.note);
}
}

bool Ended(RunRec const& rec)
{
    return rec.endedMs != 0;
}

void OnBotLogout(ObjectGuid leader)
{
    // TBF-B1 review finding 7: an ended record of a bot that just left the world goes too (Remove flushes its unread events
    // first). A live run is the tick's: its own rule stops it when the lead is off the plan's map.
    RunRec* rec = Find(leader);
    if (rec && Ended(*rec))
        Remove(leader);
}

// A newer assignment (an accepted `cc` or `cc clear`) takes the bot over: the old run's pending clear must not touch it.
void DropPendingCc(std::string const& botName)
{
    PendingCcTable().erase(botName);
}

void NoteTaskEvent(RunRec& rec, uint32 nowMs, char const* kind, int32 unit, std::string const& why)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    int32 step = -1, leg = -1;
    if (plan && rec.run.step < plan->steps.size())
    {
        step = int32(rec.run.step);
        leg = plan->steps[rec.run.step].leg;
    }
    uint32 const seq = AddEvent(rec, nowMs, kind, step, leg, unit, why);
    sLog.outString("TortoiseBots run: %s #%u %s step=%d leg=%d unit=%d %s", rec.leaderName.c_str(), seq, kind, step, leg, unit, why.c_str());
}

TickTotals const& TickCost()
{
    return TickCostRw();
}

void Update(uint32 diffMs)
{
    static uint32 accumulatedMs = 0;
    accumulatedMs += diffMs;
    bool const tick = accumulatedMs >= DungeonRun::kTickMs;
    if (tick)
        accumulatedMs = 0;
    // An ended run has no tick of its own, so its unfinished cc clears retry here: before the empty-table return below, since the last
    // run leaving the table is exactly when a pending clear must still be driven.
    if (tick)
        RetryPendingCc();
    // S9 (O9) + Commander P6: tasks (open, loot, use, gossip) tick every 250 ms even when no dungeon run is active.
    tasks::Update(diffMs);
    if (Table().empty())
        return;
    // the `come` orders run tasks queue are the run's own, so the outside-order baseline moves past them.
    for (auto const& entry : Table())
        if (!Ended(*entry.second) && tasks::TakeQueuedOrders(entry.first))
            BaselineOrders(*entry.second);
    if (!tick)
        return;
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::vector<RunRec*> live;
    for (auto const& entry : Table())
        if (!Ended(*entry.second))
            live.push_back(entry.second.get());
    for (RunRec* rec : live)
    {
        auto const started = std::chrono::steady_clock::now();
        TickRun(*rec, nowMs);
        uint32 const us = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        TickTotals& totals = TickCostRw();
        ++totals.ticks;
        totals.totalUs += us;
        totals.maxUs = std::max(totals.maxUs, us);
    }
}

StartOutcome StartRun(Player* leader, ai::DungeonPlan const* plan, uint32 nowMs, size_t fromStep)
{
    StartOutcome out;
    auto const refuse = [&out](char const* code, std::string detail)
    {
        out.code = code;
        out.detail = std::move(detail);
        return out;
    };
    if (!leader || !leader->IsInWorld() || !BotMgr::Instance().IsBot(leader->GetGUID()))
        return refuse("not-bot", "the leader must be a host bot in the world");
    if (!plan || plan->steps.empty())
        return refuse("no-plan", "the plan has no steps");
    // The named leader is the run's tank: commit the role (and its kit) before the admission tests read it.
    bool const tankRoleSet = PrepareRunTank(leader);
    // The executor's own question first (O6): a host bot with an engine that leads its party, the tank role set, every member a host bot with an engine.
    {
        std::string code, message;
        if (!StartCheck(leader->GetGUID(), code, message))
            return refuse(code.empty() ? "not-bot" : code.c_str(), message);
    }
    std::string const name = leader->GetName();
    Group* group = leader->GetGroup();
    if (!group || group->GetLeaderGUID() != leader->GetGUID())
        return refuse("not-leader", name + " does not lead a party");
    if (group->isRaidGroup())
        return refuse("group-raid", name + " leads a raid group: a run needs a party");
    if (RunRec* old = Find(leader->GetGUID()); old && !Ended(*old))
        return refuse("already-running", name + " already has a run (.tb run status " + name + ")");
    if (leader->GetMapId() != plan->map)
        return refuse("wrong-map", name + " is not on map " + std::to_string(plan->map) + " (the plan's map)");
    std::vector<Player*> others;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = InWorld(slot.guid);
        if (!member)
            return refuse("not-bot", slot.name + " is not in the world (an offline member left by an old group keeps the run from starting: `.tb ungroup " + slot.name +
                "` it once it is online, then form the party again)");
        if (!BotMgr::Instance().IsBot(slot.guid))
            return refuse("player-in-group", slot.name + " is not a host bot: a run is for bots only");
        if (member->GetMapId() != plan->map || member->GetInstanceId() != leader->GetInstanceId())
            return refuse("wrong-map", slot.name + " is not in the leader's instance of map " + std::to_string(plan->map));
        // S9 (C9, spec 5d): every member starts with room for the loot of a dungeon. Below it the loot code leaves items and a full carrier cannot take a plan
        // item, so the run refuses to start (a run that starts and then stops at the first chest is worse than a refusal that names the bot).
        if (member->GetFreeInventorySlotCount() < tasks::kStartFreeSlots)
            return refuse("bags", slot.name + " has " + std::to_string(member->GetFreeInventorySlotCount()) + " free bag slots: a run needs at least " +
                std::to_string(tasks::kStartFreeSlots) + " (the loot code leaves items when two or fewer are free): make room first");
        if (slot.guid == leader->GetGUID())
            continue;
        // A member that is not commanded yet would need its engine kit rebuilt when the run binds it (`.tb master` queues that); a run does not do it: bind the party first.
        if (h3::CommanderGuid(slot.guid).IsEmpty())
            return refuse("not-commanded", slot.name + " has no commander yet: bind the party first (.tb master party " + name + " <commander>)");
        others.push_back(member);
    }
    // S9 (C9): the group's loot method is group loot or need-before-greed: the bots vote both, so any other method would leave rolls un-voted
    // (master loot) or items to chance (free-for-all, round robin), and personal loot is a method the policy does not know. A new group is created in
    // group loot (Group.cpp:296), so a refusal here means someone changed it.
    {
        std::string methodWhy;
        if (!tasks::RunLootMethodOk(tasks::PolicyLootMethod(uint32(group->GetLootMethod())), methodWhy))
            return refuse("loot-method", name + ": " + methodWhy + " (a run needs group loot or need-before-greed)");
    }
    // Every check passed: nothing is changed before this line. A finished earlier run of the leader gives way to the new one.
    if (Find(leader->GetGUID()))
    {
        ForgetRun(leader->GetGUID());
        tasks::Forget(leader->GetGUID());   // S9 (O9)
        Remove(leader->GetGUID());
    }
    if (fromStep >= plan->steps.size())
        return refuse("no-step", "the plan has no such step");
    RunRec* rec = Create(leader->GetGUID(), name, plan, nowMs, fromStep);
    if (!rec)
        return refuse("no-plan", "the run could not be created");
    for (Player* member : others)
        if (!BindMember(*rec, member, leader))
        {
            std::string const failed = member->GetName();
            ReleaseAll(*rec);
            h3::HoldRunLead(leader->GetGUID(), false);
            Remove(leader->GetGUID());
            return refuse("bind-failed", "the host refused to bind " + failed + " to " + name);
        }
    rec->run.liveTactics = &ai::GetBossTactics();
    rec->instanceId = leader->GetInstanceId();     // R6 (review item 4)
    h3::HoldRunLead(leader->GetGUID(), true);      // R6 (review item 5)
    rec->lastTickMs = nowMs;
    BaselineOrders(*rec);
    rec->seenInit = true;
    rec->seenState = rec->run.state;
    rec->seenPhase = rec->run.phase;
    rec->seenStep = rec->run.step;
    Note(*rec, nowMs, "start", int32(fromStep), plan->name);
    LogRunTank(leader, tankRoleSet);
    out.rec = rec;
    return out;
}

StartOutcome StartPlayerLedRun(Player* commander, Player* tank, ai::DungeonPlan const* plan, uint32 nowMs)
{
    StartOutcome out;
    auto const refuse = [&out](char const* code, std::string detail)
    {
        out.code = code;
        out.detail = std::move(detail);
        return out;
    };
    if (!commander || !commander->IsInWorld())
        return refuse("no-commander", "a player-led run is started by the player who leads: type it in game, not on the console");
    if (!tank || !tank->IsInWorld() || !BotMgr::Instance().IsBot(tank->GetGUID()))
        return refuse("not-bot", "the tank must be a host bot in the world");
    if (!plan || plan->steps.empty())
        return refuse("no-plan", "the plan has no steps");
    Group* group = commander->GetGroup();
    playerled::StartObs obs;
    obs.keyOn = PlayerLedKeyOn();
    obs.starterIsBot = BotMgr::Instance().IsBot(commander->GetGUID());
    obs.grouped = group != nullptr;
    obs.raid = group && group->isRaidGroup();
    obs.starterLeads = group && group->GetLeaderGUID() == commander->GetGUID();
    obs.tankName = tank->GetName();
    std::vector<Player*> bots;                                 // the party's bots, the tank included
    if (group)
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (slot.guid == commander->GetGUID())
                continue;
            Player* member = InWorld(slot.guid);
            if (!member)
                return refuse("not-live", slot.name + " is not in the world (an offline member left by an old group keeps the run from starting: `.tb ungroup " + slot.name +
                    "` it once it is online, then form the party again)");
            playerled::MemberObs m;
            m.name = slot.name;
            m.bot = BotMgr::Instance().IsBot(slot.guid);
            ObjectGuid const commanded = h3::CommanderGuid(slot.guid);
            m.commanderIsStarter = commanded == commander->GetGUID();
            m.commanderOther = !commanded.IsEmpty() && !m.commanderIsStarter;
            m.owned = m.commanderIsStarter || BotMgr::Instance().MasterOf(slot.guid) == commander->GetGUID();
            obs.members.push_back(m);
            if (m.bot)
                bots.push_back(member);
        }
    playerled::StartVerdict const verdict = playerled::DecideStart(obs);
    if (!verdict.code.empty())
        return refuse(verdict.code.c_str(), verdict.text);
    // The named tank is the run's tank in this mode too: commit its role before the admission tests read it.
    bool const tankRoleSet = PrepareRunTank(tank);
    {
        std::string code, message;
        if (!PlayerLedStartCheck(tank->GetGUID(), code, message))
            return refuse(code.empty() ? "not-bot" : code.c_str(), message);
    }
    std::string const name = tank->GetName();
    if (RunRec* old = Find(tank->GetGUID()); old && !Ended(*old))
        return refuse("already-running", name + " already has a run (.tb run status " + name + ")");
    // One live run per party: a player has one player-led run, and no bot of this party is in another live run (a second tank in the same party would give two
    // runs competing orders to the same bots).
    for (RunRec* other : All())
    {
        if (Ended(*other))
            continue;
        bool busy = IsPlayerLed(*other) && other->commander == commander->GetGUID();
        for (Player* bot : bots)
            busy = busy || other->leader == bot->GetGUID() || other->bound.count(bot->GetGUID());
        if (busy)
            return refuse("already-running", "the party is in a run already (.tb run status " + other->leaderName + "): .tb run stop " + other->leaderName + " first");
    }
    if (commander->GetMapId() != plan->map)
        return refuse("wrong-map", std::string(commander->GetName()) + " is not on map " + std::to_string(plan->map) + " (the plan's map)");
    for (Player* bot : bots)
    {
        if (bot->GetMapId() != plan->map || bot->GetInstanceId() != commander->GetInstanceId())
            return refuse("wrong-map", std::string(bot->GetName()) + " is not in your instance of map " + std::to_string(plan->map));
        if (bot->GetFreeInventorySlotCount() < tasks::kStartFreeSlots)
            return refuse("bags", std::string(bot->GetName()) + " has " + std::to_string(bot->GetFreeInventorySlotCount()) + " free bag slots: a run needs at least " +
                std::to_string(tasks::kStartFreeSlots) + " (the loot code leaves items when two or fewer are free): make room first");
    }
    {
        std::string methodWhy;
        if (!tasks::RunLootMethodOk(tasks::PolicyLootMethod(uint32(group->GetLootMethod())), methodWhy))
            return refuse("loot-method", name + ": " + methodWhy + " (a run needs group loot or need-before-greed)");
    }
    // Every check passed: nothing is changed before this line. A finished earlier run of the tank gives way to the new one.
    if (Find(tank->GetGUID()))
    {
        ForgetRun(tank->GetGUID());
        tasks::Forget(tank->GetGUID());
        Remove(tank->GetGUID());
    }
    RunRec* rec = Create(tank->GetGUID(), name, plan, nowMs, 0, false);   // no capture: a player-led run's ticks are not the policy's (#307)
    if (!rec)
        return refuse("no-plan", "the run could not be created");
    rec->commander = commander->GetGUID();
    rec->commanderName = commander->GetName();
    rec->pl.on = true;
    rec->pl.tank = name;
    PlayerLedConfigure(rec->pl);
    // Nothing is bound: every bot keeps the player as its commander. The run only watches them (`bound` is the list the party-wide reads and the end-of-run
    // order walk; the release of an entry leaves the binding alone because the commander is not the tank).
    for (Player* bot : bots)
    {
        if (bot == tank)
            continue;
        RunRec::Binding entry;
        entry.master = commander->GetGUID();
        entry.commanded = true;
        rec->bound[bot->GetGUID()] = entry;
    }
    rec->run.liveTactics = &ai::GetBossTactics();
    rec->instanceId = tank->GetInstanceId();
    rec->lastTickMs = nowMs;
    BaselineOrders(*rec);
    rec->seenInit = true;
    rec->seenState = rec->run.state;
    rec->seenPhase = rec->run.phase;
    rec->seenStep = rec->run.step;
    Note(*rec, nowMs, "start", 0, plan->name + " (player-led: " + rec->commanderName + ")");
    LogRunTank(tank, tankRoleSet);
    out.rec = rec;
    return out;
}

StopOutcome StopRun(RunRec& rec, uint32 nowMs, std::string const& reason)
{
    StopOutcome out;
    if (Ended(rec))
    {
        out.code = "not-running";
        out.detail = "the run of " + rec.leaderName + " has ended already (" + StateName(rec) + ")";
        return out;
    }
    std::string const note = DungeonRun::RunStopNote(reason);
    if (rec.stopPending)
    {
        out.pending = true;
        return out;
    }
    // A stop never interrupts a fight: no new order from here on, the stop takes effect at the fight's end (the tick looks every second).
    if (PartyInCombat(rec))
    {
        rec.stopPending = true;
        rec.stopPendingSinceMs = nowMs ? nowMs : 1;
        rec.stopWhy = note;
        CancelRunWork(rec, false);          // R6: no queued order of the run runs while the stop waits (the pull that is fighting stays: ending it mid-fight is no help)
        out.pending = true;
        return out;
    }
    out.stayed = EndEarly(rec, nowMs, note);
    return out;
}

StateOutcome PauseRun(RunRec& rec, uint32 nowMs)
{
    StateOutcome out;
    if (Ended(rec) || rec.run.state != State::Running)
    {
        out.code = "not-running";
        out.detail = Ended(rec) ? "the run has ended" : "the run is not running (it is " + StateName(rec) + ")";
        return out;
    }
    if (rec.stopPending)
    {
        out.code = "stopping";
        out.detail = "a stop is waiting for the fight to end";
        return out;
    }
    DungeonRun::PausePlayer(rec.run, "paused by .tb run pause");
    PauseWith(rec, "command");
    CancelRunWork(rec, true);               // R6: at once, not at the next tick's Observe: an order queued a moment ago must not run
    Observe(rec, nowMs, nullptr);
    return out;
}

StateOutcome ResumeRun(RunRec& rec, uint32 nowMs)
{
    StateOutcome out;
    auto const refuse = [&out](char const* code, std::string detail)
    {
        out.code = code;
        out.detail = std::move(detail);
        return out;
    };
    if (Ended(rec))
        return refuse("not-running", "the run has ended");
    if (rec.stopPending)
        return refuse("stopping", "a stop is waiting for the fight to end");
    if (rec.run.state != State::Paused)
        return refuse("not-paused", "only a paused run can be resumed (it is " + StateName(rec) + ")");
    Player* lead = InWorld(rec.leader);
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (!IsPlayerLed(rec) && !BotMgr::Instance().IsBot(slot.guid))
                return refuse("player-in-group", slot.name + " is in the group: the run waits until the group is bots only again");
        // The pause reason of a method changed under the run ends only on group loot or need-before-greed: a resume with the method still wrong would pause again at once.
        std::string methodWhy;
        if (!tasks::RunLootMethodOk(tasks::PolicyLootMethod(uint32(group->GetLootMethod())), methodWhy))
            return refuse("loot-method", methodWhy + " (a run needs group loot or need-before-greed)");
    }
    DungeonRun::LeadSpot spot;
    if (lead)
    {
        spot.read = true;
        spot.alive = lead->IsAlive();
        spot.onPlanMap = rec.run.plan && lead->GetMapId() == rec.run.plan->map;
        spot.x = lead->GetPositionX();
        spot.y = lead->GetPositionY();
        spot.z = lead->GetPositionZ();
    }
    std::string refusal;
    if (!DungeonRun::ResumeChecked(rec.run, spot, refusal))
        return refuse("refused", refusal.empty() ? std::string("the run has no step to resume at") : refusal);
    rec.pausedForPlayer = false;
    rec.lastTickMs = nowMs;
    BaselineOrders(rec);
    Observe(rec, nowMs, nullptr);
    return out;
}

namespace
{
RunRec* RunOfBot(Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    RunRec* rec = group ? RunOfGroupLeader(group->GetLeaderGUID()) : nullptr;
    if (!rec || Ended(*rec))
        return nullptr;
    // A player-led run belongs to its roster: a bot that joined after the start, or dropped out of it, gets no run policy (kill-first, armed triggers, plan map).
    if (IsPlayerLed(*rec) && bot->GetGUID() != rec->leader && !rec->bound.count(bot->GetGUID()))
        return nullptr;
    return rec;
}
}

bool BotInRunningRun(Player* bot)
{
    RunRec* rec = RunOfBot(bot);
    return rec && rec->run.state == State::Running;
}

bool BotRunKillFirst(Player* bot, uint32_t entry)
{
    RunRec* rec = RunOfBot(bot);
    if (!rec || rec->run.state != State::Running || !rec->run.plan)
        return false;
    std::vector<uint32> const& list = rec->run.plan->killFirst;
    return std::find(list.begin(), list.end(), entry) != list.end();
}

std::vector<uint32_t> const* BotRunArmedTriggers(Player* bot)
{
    RunRec* rec = RunOfBot(bot);
    return rec && rec->run.state == State::Running ? &rec->run.armedTriggers : nullptr;
}

uint32_t BotRunPlanMap(Player* bot)
{
    RunRec* rec = RunOfBot(bot);
    return rec && rec->run.plan ? rec->run.plan->map : 0;
}

std::string LootMethodWord(Group const* group)
{
    int const method = tasks::PolicyLootMethod(uint32(group->GetLootMethod()));
    return method < 0 ? std::string("none") : std::string(RemoteCommander::LootMethodName(method));
}

void LoadPlans()
{
    ai::LoadDungeonPlans();
    ai::LoadDungeonStepUnits();
}

void AnnotateStateEnd(h3::Rec& end, Player* member)
{
    if (!member)
        return;
    Group* group = member->GetGroup();
    if (!group)
        return;
    if (RunRec const* rec = RunOfGroupLeader(group->GetLeaderGUID()))
        end.S("run", rec->leaderName).S("runstate", StateName(*rec));
}

// ---- `.tb grammar selftest`: one record of each kind this package writes, built by the real writers from a made-up plan and run ----
void SelfTestRecords(h3::Reply& reply)
{
    using h3::Rec;
    ai::DungeonPlan plan;
    plan.name = "selftest_plan";
    plan.map = 389;
    plan.legs = { { 0, "tunnel" }, { 1, "trogg cave" } };
    auto addStep = [&plan](uint8 leg, uint16 seq, ai::PlanStepKind kind, uint32 entry, std::vector<uint32> entries)
    {
        ai::PlanStep step;
        step.leg = leg;
        step.seq = seq;
        step.kind = kind;
        step.entry = entry;
        step.entries = std::move(entries);
        plan.steps.push_back(std::move(step));
    };
    plan.ccPriority = { 11319, 11324 };
    plan.killFirst = { 11319 };
    std::string const source = "turtle-agent 5c7b6ca dungeons/ragefire_chasm.json";
    addStep(0, 0, ai::PlanStepKind::Move, 0, {});
    addStep(0, 1, ai::PlanStepKind::Pull, 0, { 11320 });
    addStep(0, 2, ai::PlanStepKind::Pull, 0, { 11321 });
    addStep(1, 0, ai::PlanStepKind::Move, 0, {});
    addStep(1, 1, ai::PlanStepKind::Boss, 11517, {});

    reply.Line(PlanRecord(plan, 6, &source));
    reply.Line(LegRecord(plan, 0));
    UnitCheck matched;
    matched.leg = 0; matched.seq = 1; matched.idx = 0; matched.entry = 11320; matched.role = "pack";
    matched.result = UnitResult::Matched; matched.yd = 0.42f; matched.spawnId = 125331;
    reply.Line(UnitRecord(matched, 1));
    UnitCheck optional;
    optional.leg = 0; optional.seq = 2; optional.idx = 1; optional.entry = 8996; optional.role = "pack"; optional.optional = true;
    reply.Line(UnitRecord(optional, -1));

    RunRec rec;
    rec.leaderName = "Mtank";
    rec.planName = plan.name;
    rec.run.plan = &plan;
    rec.run.step = 3;
    rec.run.state = DungeonRun::State::Paused;
    rec.run.phase = DungeonRun::Phase::Walk;
    rec.run.note = "waiting for Mdpsa (dead)";
    rec.startedMs = 1000;
    rec.pauses = 1;
    rec.deaths = 1;
    rec.bossDead[11517] = false;
    AddEvent(rec, 65000, "pull", 2, 0, 11321);
    reply.Line(RunRecord(rec, 126000));
    reply.Line(EventRecord(rec.events.back()));
}
}
}

#endif
