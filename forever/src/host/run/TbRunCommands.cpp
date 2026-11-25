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
#include "host/run/TbRunFactsPure.h"
#include "host/run/TbRunPlayerLedPure.h"
#include "host/run/TbRunStands.h"
#include "runtime/DecisionCapture.h"

#include "GameTime.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

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

// #307: the run a `<leader|run>` word names: the leader's name as every other
// verb reads it, else a run id (all digits, the `run=` `.tb run status all`
// shows) when one has it.
RunRec* RunByArg(std::string const& typed)
{
    if (RunRec* byName = RunByName(typed))
        return byName;
    uint32 id = 0;
    if (ParseSeq(typed, id))
        for (RunRec* rec : All())
            if (rec->runId == id)
                return rec;
    return nullptr;
}
}

bool HandleRun(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "run"))
        return false;
    static char const* const usage = ".tb run start <leader> <plan> [from step <N> | from leg <leg>] [lead=player] | stop <leader> [reason] | pause <leader> | resume <leader> | status <leader|all> | events <leader> [since=<seq>] | decisions <leader|run> [since=<seq>] [limit=<n>]";
    if (args.w.empty())
        return Fail(handler, args, "run", "missing-argument", usage);
    std::string const verb = Lower(args.w[0]);
    // Issue #139: `lead=player` (the opt-in player-led run) is taken out of a start's words; every other verb keeps its words as typed.
    std::vector<std::string> startWords = args.w;
    playerled::LeadWord const leadWord = verb == "start" ? playerled::TakeLeadWord(startWords) : playerled::LeadWord::None;
    if (leadWord == playerled::LeadWord::Bad)
        return Fail(handler, args, "run", "bad-argument", "lead= takes player or bot (once): .tb run start <tank> <plan> lead=player");

    // ---- syntax first: nothing is read or changed before the words are known to be well formed ----
    if (verb != "start" && verb != "stop" && verb != "pause" && verb != "resume" && verb != "status" && verb != "events" &&
        verb != "decisions")
        return Fail(handler, args, "run", "unknown-verb", args.w[0]);
    if (args.w.size() < 2)
        return Fail(handler, args, "run", "missing-argument", usage);
    std::string const leaderName = args.w[1];
    uint32 since = 0;
    bool haveSince = false;
    // #307: `limit=` of `.tb run decisions`, already clamped to the read path's
    // 1..200 (the default is the policy's kDefaultRows).
    int limit = DecisionCapture::kDefaultRows;
    bool haveLimit = false;
    std::string reason;
    if (verb == "start")
    {
        if (startWords.size() < 3)
            return Fail(handler, args, "run", "missing-argument", ".tb run start <leader> <plan> [from step <N> | from leg <leg>] [lead=player]");
        // A placed start (WC run 9: a short live proof starts at the failing leg) is bot-led only: `lead=player` is not combined with it
        // (RUN-KNOBS-139-NOTES.md, Part 2).
        if (leadWord == playerled::LeadWord::Player && startWords.size() > 3)
            return Fail(handler, args, "run", "unexpected-argument", startWords[3]);
        // `from step <N>` (the step number the status line shows, from 0) or `from leg <number|name>`, the words and the resolution of
        // the Turtle `.bot run start ... from` (DungeonRun::ParseStartArgs).
        if (startWords.size() > 3)
        {
            std::string joined;
            for (size_t i = 2; i < startWords.size(); ++i)
                joined += (joined.empty() ? "" : " ") + Lower(startWords[i]);
            DungeonRun::StartArgs const parsed = DungeonRun::ParseStartArgs(joined);
            if (!parsed.ok || parsed.from == DungeonRun::StartFrom::Beginning || parsed.plan != Lower(startWords[2]))
                return Fail(handler, args, "run", "unexpected-argument", startWords[3]);
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
    else if (verb == "decisions")
    {
        for (size_t i = 2; i < args.w.size(); ++i)
        {
            std::string const word = Lower(args.w[i]);
            if (word.compare(0, 6, "since=") == 0 && !haveSince)
            {
                if (!ParseSeq(word.substr(6), since))
                    return Fail(handler, args, "run", "bad-argument", "since= takes the seq of a decision row (`.tb run decisions <leader>` shows them as seq=)");
                haveSince = true;
            }
            else if (word.compare(0, 6, "limit=") == 0 && !haveLimit)
            {
                std::string const value = word.substr(6);
                std::int64_t typed = 0;
                bool digits = !value.empty() && value.size() <= 9;
                for (char c : value)
                    if (!std::isdigit(static_cast<unsigned char>(c)))
                        digits = false;
                if (!digits)
                    return Fail(handler, args, "run", "bad-argument", "limit= takes a row count, 1 to 200 (the default is 50)");
                typed = std::strtoll(value.c_str(), nullptr, 10);
                limit = DecisionCapture::ClampRows(typed);
                haveLimit = true;
            }
            else
                return Fail(handler, args, "run", "unexpected-argument", args.w[i]);
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
            // proof-1 A: a GM observer (TortoiseBots.Run.GmObserver) may start a run the ownership guard refuses; the party-lead rule stands for everyone else.
            std::string code, detail;
            if (!h3::CheckRunObserverAccess(handler, leader, code, detail) && !h3::CheckPartyLeadAccess(handler, leader, code, detail))
                return Fail(handler, args, "run", code, detail);
            if (Group* group = leader->GetGroup())
            {
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                {
                    Player* m = ObjectAccessor::FindPlayer(slot.guid);
                    if (m && m->IsInWorld() && h3::IsHostBot(m) && !h3::CheckRunObserverAccess(handler, m, code, detail))
                        return Fail(handler, args, "run", code, detail);
                }
            }
        }
        ai::DungeonPlanTable& table = ai::GetDungeonPlans();
        if (!table.Sealed())
            return Fail(handler, args, "run", "not-loaded", "the dungeon plans are not loaded (the plan loader has not run, or the module is off)");
        ai::DungeonPlan const* plan = FindPlan(table, startWords[2]);
        if (!plan)
            return Fail(handler, args, "run", "no-plan", startWords[2] + " is not a loaded dungeon plan (.tb plan list)");
        size_t fromStep = 0;
        if (startWords.size() > 3)
        {
            std::string joined;
            for (size_t i = 2; i < startWords.size(); ++i)
                joined += (joined.empty() ? "" : " ") + Lower(startWords[i]);
            DungeonRun::StartArgs const parsed = DungeonRun::ParseStartArgs(joined);
            if (facts::ResolvePlacedStart(*plan, parsed.from, parsed.value, fromStep) != DungeonRun::FromResult::Ok)
                return Fail(handler, args, "run", "no-step", startWords[startWords.size() - 1] + " is not a step or leg of " + plan->name + " (.tb plan " + plan->name + " check)");
        }
        // `leader` is the tank bot either way; with `lead=player` the sender is the player who leads and walks (a bot-led run is the default, D1).
        StartOutcome const started = leadWord == playerled::LeadWord::Player ? StartPlayerLedRun(SenderPlayer(handler), leader, plan, nowMs)
                                                                             : StartRun(leader, plan, nowMs, fromStep);
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
    RunRec* rec = verb == "decisions" ? RunByArg(leaderName) : RunByName(leaderName);
    if (!rec)
    {
        // #307 keeps its own message: the word may be a run id, not a name.
        if (verb == "decisions")
            return Fail(handler, args, "run", "no-run", leaderName + " is not a leader with a run or a run id (.tb run status all shows both; .tb run start <leader> <plan>)");
        return Fail(handler, args, "run", "no-run", leaderName + " has no run (.tb run start " + leaderName + " <plan>; .tb run status all)");
    }

    // ---- decisions (#307): the read path over the capture the run's tick writes ----
    if (verb == "decisions")
    {
        // The same shared check the run's controlling verbs use (console, a GM,
        // the run's owner or observer): a reader may not see more than the run.
        std::string code, detail;
        if (!h3::CheckRunObserverAccess(handler, rec->leader, code, detail))
            return Fail(handler, args, "run", code, detail);
        // The capture's own toggle is the feature's: no second key, and off is
        // a refusal that names it.
        if (!DecisionCaptureService::Instance().Enabled())
            return Fail(handler, args, "run", "capture-off", "the decision capture is off: AiPlayerbot.DecisionCapture.Enabled = 0 (set it to 1 and restart the server: the keys are read when the config loads)");
        // A player-led run asks for no capture at all (TbRun.h, Create): say
        // that, rather than a capture that never had a row.
        if (IsPlayerLed(*rec))
            return Fail(handler, args, "run", "no-capture", "this is a player-led run: the capture records the leader policy's ticks, and a player-led run does not tick it (a bot-led run does)");
        std::uint64_t const captureKey = CaptureKey(rec->leader);
        DecisionCapture::RowLog const* rows = DecisionCaptureService::Instance().Rows(captureKey);
        if (!rows)
        {
            std::string const& captureError = DecisionCaptureService::Instance().lastError();
            return Fail(handler, args, "run", "no-capture", captureError.empty()
                ? std::string("this run has no live capture: a player-led run is not captured, and a stopped run's window leaves with its capture (the capture folder keeps its rows)")
                : "the capture could not start: " + captureError);
        }
        DecisionCapture::Page const page = DecisionCapture::Select(rows->Rows(), since, std::size_t(limit));
        Reply reply(handler, args, "run");
        reply.Begin();
        {
            // The one header line: which capture, how many rows, and the cursor.
            Rec head("decisions");
            head.N("run", int64(rec->runId)).S("lead", rec->leaderName).S("plan", rec->planName)
                .S("capture", DecisionCaptureService::Instance().CaptureId(captureKey))
                .N("count", int64(page.count)).N("since", int64(since)).N("next", int64(page.next)).B("more", page.more);
            reply.Line(head);
        }
        // One row per line, oldest first, the keys in one fixed order
        // (docs/play/dungeon-run-leader.md): the row is never split over
        // continuation lines, a long value is cut and the row says trunc=yes.
        for (std::size_t i = 0; i < page.count; ++i)
        {
            DecisionCapture::Row const& row = rows->Rows()[page.first + i];
            Rec out("decision");
            out.N("seq", int64(row.seq)).N("t", int64(row.unixMs)).N("dt", int64(row.dtMs))
                .S("state", DungeonRun::StateName(static_cast<DungeonRun::State>(row.state)))
                .S("phase", DungeonRun::PhaseName(static_cast<DungeonRun::Phase>(row.phase)))
                .N("step", int64(row.step))
                .S("record", DecisionCapture::RecordName(row.record));
            for (DecisionCapture::TextField const& field : DecisionCapture::TextFields(row))
                out.T(field.key, *field.value, DecisionCapture::kRowTextBytes);
            reply.Line(out);
        }
        Rec end("run");
        end.S("verb", "decisions").N("run", int64(rec->runId)).N("count", int64(page.count))
            .N("next", int64(page.next)).B("more", page.more);
        reply.End(end);
        return true;
    }

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
        if (!h3::CheckRunObserverAccess(handler, rec->leader, code, detail))
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
        if (!h3::CheckRunObserverAccess(handler, rec->leader, code, detail))
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
