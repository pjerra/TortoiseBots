// Slice 6, host package C6: see TbRunCommands.h.
#include "host/run/TbRunCommands.h"

#include "Chat.h"

#include <cctype>
#include <string>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "ai/playerbot/DungeonPlanMgr.h"
#include "host/TbH3.h"
#include "host/run/TbRun.h"
#include "host/run/TbRunFacts.h"
#include "host/run/TbRunStands.h"

#include "GameTime.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <cstdlib>

namespace TortoiseBots
{
namespace run
{
using namespace h3;

namespace
{
ai::DungeonPlan const* FindPlan(ai::DungeonPlanTable const& table, std::string const& typed)
{
    if (ai::DungeonPlan const* plan = table.Find(typed))
        return plan;
    return table.Find(Lower(typed));
}

// A run by its leader's name, in the table (an ended run stays there until the leader starts another; the leader need not be in the world).
RunRec* RunByName(std::string const& typed)
{
    std::string const wanted = Lower(typed);
    for (RunRec* rec : All())
        if (Lower(rec->leaderName) == wanted)
            return rec;
    return nullptr;
}

// The one-record reply of a command that changes a run: the run record as it stands after the change, then the end line.
void ReplyRun(ChatHandler* handler, Words const& args, RunRec const& rec, uint32 nowMs, Rec end)
{
    Reply reply(handler, args, "run");
    reply.Begin();
    reply.Line(RunRecord(rec, nowMs));
    reply.End(std::move(end));
}

bool ParseSeq(std::string const& text, uint32& out)
{
    if (text.empty() || text.size() > 9)
        return false;
    for (char c : text)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    out = uint32(std::strtoul(text.c_str(), nullptr, 10));
    return true;
}
}

bool HandleRun(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "run"))
        return false;
    static char const* const usage = ".tb run start <leader> <plan> [from step <N> | from leg <leg>] | stop <leader> [reason] | pause <leader> | resume <leader> | status <leader|all> | events <leader> [since=<seq>]";
    if (args.w.empty())
        return Fail(handler, args, "run", "missing-argument", usage);
    std::string const verb = Lower(args.w[0]);

    // ---- syntax first: nothing is read or changed before the words are known to be well formed ----
    if (verb != "start" && verb != "stop" && verb != "pause" && verb != "resume" && verb != "status" && verb != "events")
        return Fail(handler, args, "run", "unknown-verb", args.w[0]);
    if (args.w.size() < 2)
        return Fail(handler, args, "run", "missing-argument", usage);
    std::string const leaderName = args.w[1];
    uint32 since = 0;
    bool haveSince = false;
    std::string reason;
    if (verb == "start")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "run", "missing-argument", ".tb run start <leader> <plan> [from step <N> | from leg <leg>]");
        // A placed start (WC run 9: a short live proof starts at the failing leg): `from step <N>` (the step number the status line shows, from 1)
        // or `from leg <number|name>`, the words and the resolution of the Turtle `.bot run start ... from` (DungeonRun::ParseStartArgs).
        if (args.w.size() > 3)
        {
            std::string joined;
            for (size_t i = 2; i < args.w.size(); ++i)
                joined += (joined.empty() ? "" : " ") + Lower(args.w[i]);
            DungeonRun::StartArgs const parsed = DungeonRun::ParseStartArgs(joined);
            if (!parsed.ok || parsed.from == DungeonRun::StartFrom::Beginning || parsed.plan != Lower(args.w[2]))
                return Fail(handler, args, "run", "unexpected-argument", args.w[3]);
        }
    }
    else if (verb == "stop")
    {
        for (size_t i = 2; i < args.w.size(); ++i)
            reason += (reason.empty() ? "" : " ") + args.w[i];
    }
    else if (verb == "events")
    {
        for (size_t i = 2; i < args.w.size(); ++i)
        {
            std::string const word = Lower(args.w[i]);
            if (word.compare(0, 6, "since=") != 0 || haveSince)
                return Fail(handler, args, "run", "unexpected-argument", args.w[i]);
            if (!ParseSeq(word.substr(6), since))
                return Fail(handler, args, "run", "bad-argument", "since= takes the number of an event (`.tb run events <leader>` shows them)");
            haveSince = true;
        }
    }
    else if (args.w.size() > 2)
        return Fail(handler, args, "run", "unexpected-argument", args.w[2]);

    uint32 const nowMs = GameTime::GetGameTimeMS();

    // ---- start ----
    if (verb == "start")
    {
        Player* leader = FindOnline(leaderName);
        if (!leader)
            return Fail(handler, args, "run", "not-bot", leaderName + " is not in the world");
        if (!IsHostBot(leader) || !MayLeadBots(handler, leader->GetGUID()))
            return Fail(handler, args, "run", "not-bot", leaderName + " is not a host bot: a run is led by a bot");
        {
            std::string code, detail;
            if (!h3::CheckPartyLeadAccess(handler, leader, code, detail))
                return Fail(handler, args, "run", code, detail);
            if (Group* group = leader->GetGroup())
            {
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    Player* m = ObjectAccessor::FindPlayer(slot.guid);
                    if (m && m->IsInWorld() && h3::IsHostBot(m) && !h3::CheckBotAccess(handler, m, code, detail))
                        return Fail(handler, args, "run", code, detail);
                }
            }
        }
        ai::DungeonPlanTable& table = ai::GetDungeonPlans();
        if (!table.Sealed())
            return Fail(handler, args, "run", "not-loaded", "the dungeon plans are not loaded (the plan loader has not run, or the module is off)");
        ai::DungeonPlan const* plan = FindPlan(table, args.w[2]);
        if (!plan)
            return Fail(handler, args, "run", "no-plan", args.w[2] + " is not a loaded dungeon plan (.tb plan list)");
        size_t fromStep = 0;
        if (args.w.size() > 3)
        {
            std::string joined;
            for (size_t i = 2; i < args.w.size(); ++i)
                joined += (joined.empty() ? "" : " ") + Lower(args.w[i]);
            DungeonRun::StartArgs const parsed = DungeonRun::ParseStartArgs(joined);
            if (DungeonRun::ResolveStartStep(*plan, parsed.from, parsed.value, fromStep) != DungeonRun::FromResult::Ok)
                return Fail(handler, args, "run", "no-step", args.w[args.w.size() - 1] + " is not a step or leg of " + plan->name + " (.tb plan " + plan->name + " check)");
        }
        StartOutcome const started = StartRun(leader, plan, nowMs, fromStep);
        if (!started.code.empty())
            return Fail(handler, args, "run", started.code, started.detail);
        Rec end("run");
        end.S("verb", "start");
        ReplyRun(handler, args, *started.rec, nowMs, end);
        return true;
    }

    // ---- status all ----
    if (verb == "status" && Lower(leaderName) == "all")
    {
        std::vector<RunRec*> const runs = All();
        Reply reply(handler, args, "run");
        reply.Begin();
        for (RunRec const* rec : runs)
            reply.Line(RunRecord(*rec, nowMs));
        Rec end("run");
        end.S("verb", "status").N("runs", int64(runs.size()));
        TickTotals const& tick = TickCost();
        FactsTotals const& facts = Totals();
        end.N("ticks", int64(tick.ticks)).N("tick_avg_us", tick.ticks ? int64(tick.totalUs / tick.ticks) : 0).N("tick_max_us", tick.maxUs);
        end.N("facts_reads", int64(facts.reads)).N("facts_avg_us", facts.reads ? int64(facts.totalUs / facts.reads) : 0).N("facts_max_us", facts.maxUs)
            .N("facts_unreadable", int64(facts.unreadable));
        // N6: the stand searches (once per request of the policy): ring points tested, navmesh queries, the worst single search.
        StandCost const& stands = StandTotals();
        end.N("stand_candidates", int64(stands.candidates)).N("stand_paths", int64(stands.pathQueries)).N("stand_max_us", stands.costUs);
        reply.End(end);
        return true;
    }

    // ---- the verbs that name an existing run ----
    RunRec* rec = RunByName(leaderName);
    if (!rec)
        return Fail(handler, args, "run", "no-run", leaderName + " has no run (.tb run start " + leaderName + " <plan>; .tb run status all)");

    if (verb == "status")
    {
        Rec end("run");
        end.S("verb", "status");
        ReplyRun(handler, args, *rec, nowMs, end);
        return true;
    }
    if (verb == "events")
    {
        Reply reply(handler, args, "run");
        reply.Begin();
        for (RunEvent const& event : rec->events)
            if (event.seq > since)
                reply.Line(EventRecord(event));
        Rec end("run");
        end.S("verb", "events").S("lead", rec->leaderName);
        if (!rec->events.empty())
            end.N("oldest", rec->events.front().seq);
        if (rec->eventSeq)
            end.N("last", rec->eventSeq);
        reply.End(end);
        return true;
    }
    if (verb == "stop")
    {
        std::string code, detail;
        if (!h3::CheckBotAccess(handler, rec->leader, code, detail))
            return Fail(handler, args, "run", code, detail);
        StopOutcome const stopped = StopRun(*rec, nowMs, reason.empty() ? std::string("by .tb run stop") : reason);
        if (!stopped.code.empty())
            return Fail(handler, args, "run", stopped.code, stopped.detail);
        Rec end("run");
        end.S("verb", "stop").B("pending", stopped.pending);
        if (!stopped.pending)
            end.N("stayed", stopped.stayed);
        ReplyRun(handler, args, *rec, nowMs, end);
        return true;
    }
    {
        std::string code, detail;
        if (!h3::CheckBotAccess(handler, rec->leader, code, detail))
            return Fail(handler, args, "run", code, detail);
    }
    StateOutcome const changed = verb == "pause" ? PauseRun(*rec, nowMs) : ResumeRun(*rec, nowMs);
    if (!changed.code.empty())
        return Fail(handler, args, "run", changed.code, changed.detail);
    Rec end("run");
    end.S("verb", verb);
    ReplyRun(handler, args, *rec, nowMs, end);
    return true;
}
}
}

#else

namespace TortoiseBots
{
namespace run
{
namespace
{
std::string TokenOf(std::string_view tail)
{
    size_t end = tail.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tail[start - 1])))
        --start;
    std::string_view const word = tail.substr(start, end - start);
    if (word.size() < 2 || word.size() > 33 || word[0] != '#')
        return {};
    for (size_t i = 1; i < word.size(); ++i)
    {
        char const c = word[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != ':' && c != '-')
            return {};
    }
    return std::string(word);
}
}

// The default (option-OFF) build: the same refusal the slice-3, slice-4 and `.tb plan` stubs give.
bool HandleRun(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = "error code=engine-not-built cmd=run msg=" + msg;
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}
}
}

#endif
