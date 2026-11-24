// `.tb trace` and `.tb why`: see TbCmdTrace.h. The recorder and the policy are the shared Turtle sources
// (runtime/RunTrace.cpp, runtime/WhyTrace.cpp); this file only parses, resolves the bots and replies.
//
// The trace's context (who drives the run, the plan whose bosses the outcome is judged against) comes from the
// ported run leader (host/run/TbRun.h) instead of the legacy commander's DungeonRuns table.
#include "host/cmd/TbCmdTrace.h"

#include "Chat.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "TbBotMgr.h"
#include "host/TbH3.h"
#include "host/run/TbRun.h"
#include "runtime/RunTrace.h"
#include "runtime/WhyTrace.h"

#include "ai/playerbot/DungeonPlanMgr.h"
#include "ai/playerbot/PlayerbotAI.h"
#include "ai/playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAIStorage.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

namespace TortoiseBots
{
namespace trace
{
using namespace h3;

namespace
{
bool Say(ChatHandler* handler, std::string const& text)
{
    handler->PSendSysMessage("TortoiseBots: %s", text.c_str());
    return true;
}

// A GameMaster may trace any bot (`.tb` rows need RBAC_PERM_COMMAND_GM), and the console passes every permission:
// the same rule the Turtle command glue applies to a GM. No per-bot ownership check is needed on this surface.
bool MayRecordBot(uint64_t /*requesterGuid*/, Player* /*bot*/)
{
    return true;
}

// What the run trace's summary needs from the command side (runtime/RunTraceSummary.h): who drives the run and, while
// `wantPlan`, the plan whose bosses the outcome is judged against. The ported run leader keeps one run per leader
// (host/run/TbRun.h); a requester who commands the leader's party is the same run through its commander guid.
bool TraceContext(uint64_t requesterGuid, Player* anchor, bool wantPlan, RunTrace::Context& out)
{
    out.mode = RunTrace::RunMode::Observe;
    if (!anchor)
        return false;
    run::RunRec* rec = run::Find(anchor->GetGUID());
    if (!rec)
        if (Player* requester = PlayerOfGuidKeyCompat(requesterGuid))
            rec = run::Find(requester->GetGUID());
    if (!rec)
        for (run::RunRec* candidate : run::All())
            if (!run::Ended(*candidate) &&
                GuidKeyCompat(h3::CommanderGuid(candidate->leader)) == requesterGuid)
            {
                rec = candidate;
                break;
            }
    if (rec && !run::Ended(*rec))
    {
        out.mode = RunTrace::RunMode::RunLeader;
        if (ai::DungeonPlan const* plan = rec->run.plan)
        {
            uint32 const team = anchor->GetTeam() == ALLIANCE ? ai::kPlanFlagAlliance : ai::kPlanFlagHorde;
            if (wantPlan)
            {
                out.planName = plan->name;
                out.bossEntries = RunTrace::PlanBossEntries(*plan, team);
            }
            if (ai::PlanStep const* step = DungeonRun::Current(rec->run))
            {
                out.hasStep = true;
                out.leg = step->leg;
                out.seq = step->seq;
            }
        }
    }
    if (!wantPlan || !out.planName.empty())
        return true;
    // No run of this party: the one plan on the party's map, if there is exactly one. With several, only their
    // bosses' entries (a kill is still seen, the outcome has no list). In-memory reads only.
    ai::DungeonPlanTable& table = ai::GetDungeonPlans();
    std::vector<std::string> const here = table.PlansOnMap(anchor->GetMapId());
    uint32 const team = anchor->GetTeam() == ALLIANCE ? ai::kPlanFlagAlliance : ai::kPlanFlagHorde;
    if (here.size() == 1)
        if (ai::DungeonPlan const* plan = table.Find(here.front()))
        {
            out.planName = plan->name;
            out.bossEntries = RunTrace::PlanBossEntries(*plan, team);
            return true;
        }
    for (std::string const& name : here)
        if (ai::DungeonPlan const* other = table.Find(name))
            for (uint32 entry : RunTrace::PlanBossEntries(*other, team))
                out.knownBosses.push_back(entry);
    return true;
}

// A bot named on the command: in the world, a host bot, with an AI controller.
Player* ResolveBot(std::string const& name, std::string& why)
{
    Player* bot = FindOnline(name);
    if (!bot)
    {
        why = "No online character named " + name + ".";
        return nullptr;
    }
    if (!IsHostBot(bot))
    {
        why = std::string(bot->GetName()) + " is not a host bot.";
        return nullptr;
    }
    if (!PlayerbotAIStorage::Instance().GetAI(bot))
    {
        why = std::string(bot->GetName()) + " has no AI controller.";
        return nullptr;
    }
    return bot;
}

// How many host bots with an AI the trace would record in the anchor's group.
size_t CountPartyBots(Player* anchor)
{
    Group* group = anchor ? anchor->GetGroup() : nullptr;
    if (!group)
        return 0;
    size_t count = 0;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (Player* member = ObjectAccessor::FindPlayer(slot.guid))
            if (member->IsInWorld() && BotMgr::Instance().IsBot(slot.guid) &&
                PlayerbotAIStorage::Instance().GetAI(member))
                ++count;
    return count;
}
} // namespace

bool HandleTrace(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    static char const* const usage =
        ".tb trace start <run_id> [party [<leader>] | bot <Name>] [driver=<model>:<effort>:<run>] | stop [<run_id>] | status";
    Words const args = Split(tail);
    if (args.w.empty())
        return Say(handler, usage);
    std::string const verb = Lower(args.w[0]);
    if (verb != "start" && verb != "stop" && verb != "status")
        return Say(handler, usage);

    RunTraceService& service = RunTraceService::Instance();
    if (verb == "status")
        return Say(handler, service.Status(true));

    if (verb == "stop")
    {
        if (!service.Active())
            return Say(handler, "No trace is running.");
        std::string const asked = args.w.size() > 1 ? args.w[1] : std::string();
        if (!asked.empty() && !RunTrace::ValidRunId(asked))
            return Say(handler, "Invalid run id: use 1-64 letters, digits, '.', '_' or '-'.");
        if (char const* refusal = RunTrace::StopRunIdRefusal(service.RunId(), asked))
            return Say(handler, std::string("The running trace is ") + service.RunId() + ", not " + asked +
                "; nothing was stopped.");
        std::string message;
        service.Stop("stop", message);
        return Say(handler, message);
    }

    // start
    if (char const* off = RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace))
        return Say(handler, off);
    // `driver=<model>:<effort>:<run>` may stand anywhere after the run id.
    std::vector<std::string> words(args.w);
    std::string driver;
    bool haveDriver = false;
    for (size_t i = 2; i < words.size();)
    {
        if (!RunTrace::IsDriverToken(words[i]))
        {
            ++i;
            continue;
        }
        std::string const value = words[i].substr(7);
        char const* refusal = haveDriver ? "driver= was given twice." : RunTrace::DriverRefusal(value);
        if (refusal)
            return Say(handler, refusal);
        driver = value;
        haveDriver = true;
        words.erase(words.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (words.size() < 2)
        return Say(handler, usage);
    std::string const runId = words[1];
    if (!RunTrace::ValidRunId(runId))
        return Say(handler, "Invalid run id: use 1-64 letters, digits, '.', '_' or '-'.");

    std::string const scope = words.size() > 2 ? Lower(words[2]) : std::string("party");
    Player* sender = SenderPlayer(handler);
    Player* anchor = nullptr;
    Player* single = nullptr;
    std::string why;
    if (scope == "bot")
    {
        if (words.size() < 4)
            return Say(handler, usage);
        single = ResolveBot(words[3], why);
        if (!single)
            return Say(handler, why);
        anchor = single;
    }
    else if (scope == "party")
    {
        if (words.size() > 3)
        {
            anchor = ResolveBot(words[3], why);
            if (!anchor)
                return Say(handler, why);
        }
        else
        {
            anchor = sender;
            if (!anchor)
                return Say(handler, "From the console, name the party leader: .tb trace start " + runId +
                    " party <leader> (or bot <Name>).");
        }
        if (!anchor->GetGroup())
            return Say(handler, std::string(anchor->GetName()) + " is not in a party.");
        if (!CountPartyBots(anchor))
            return Say(handler, "There are no bots with an AI in that party.");
    }
    else
        return Say(handler, usage);

    RunTraceService::StartParams params;
    params.runId = runId;
    params.driver = driver;
    params.context = &TraceContext;
    params.requester = sender ? sender->GetName() : std::string("console");
    // The console has no character to resolve each frame: the anchor stands in for it, so the scope's own bots keep the
    // run alive and the GM-only check above is not re-asked. A GM who typed the command uses their own guid (TB's rule:
    // the trace ends when the requester leaves).
    params.requesterGuid = GuidKeyCompat(sender ? sender->GetGUID() : anchor->GetGUID());
    params.mayRecord = &MayRecordBot;
    params.wholeParty = scope != "bot";
    params.anchorGuid = GuidKeyCompat(anchor->GetGUID());
    params.scopeName = single ? single->GetName() : anchor->GetName();
    std::string message;
    if (!service.Start(params, message))
        return Say(handler, message);
    return Say(handler, message);
}

bool HandleWhy(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (args.w.empty())
        return Say(handler, "Usage: .tb why <bot> [seconds] (default 30, at most 120)");
    std::string const rest = args.w.size() > 1 ? args.w[1] : std::string();
    unsigned seconds = 0;
    if (!WhyTrace::ParseSeconds(rest, seconds))
        return Say(handler, "The seconds must be a whole number (default 30, at most 120).");
    std::string why;
    Player* bot = ResolveBot(args.w[0], why);
    if (!bot)
        return Say(handler, why);
    std::string message;
    if (!WhyTraceService::Instance().Start(bot, seconds, SenderPlayer(handler), message))
        return Say(handler, message);
    return Say(handler, message);
}

} // namespace trace
} // namespace TortoiseBots

#else

#include "host/TbH3.h"

namespace TortoiseBots
{
namespace trace
{
bool HandleTrace(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::NotBuilt(handler, args, "trace");
}

bool HandleWhy(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::NotBuilt(handler, args, "why");
}
} // namespace trace
} // namespace TortoiseBots

#endif
