// Slice 5, host package H5: see TbPlanCommands.h.
#include "host/run/TbPlanCommands.h"

#include "Chat.h"

#include <cctype>
#include <string>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "ai/playerbot/DungeonPlanMgr.h"
#include "host/TbH3.h"
#include "host/run/TbPlanUnits.h"
#include "host/run/TbRunHooks.h"

#include "DatabaseEnv.h"
#include "Map.h"
#include "Player.h"
#include "StringFormat.h"

#include <algorithm>

namespace TortoiseBots
{
namespace run
{
using namespace h3;

namespace
{
// `SELECT COUNT(*) FROM <table> WHERE plan = <plan>`; false when the query failed (the core's Query answers null for an error too).
bool CountPlanRows(char const* table, std::string plan, uint64& out)
{
    WorldDatabase.EscapeString(plan);
    QueryResult result = WorldDatabase.Query(Trinity::StringFormat("SELECT COUNT(*) FROM `{}` WHERE `plan` = '{}'", table, plan).c_str());
    if (!result)
        return false;
    out = (*result)[0].GetUInt64();
    return true;
}

// The plan row's `source` column (the loader does not keep it); false when the row could not be read.
bool ReadPlanSource(std::string plan, std::string& out)
{
    WorldDatabase.EscapeString(plan);
    QueryResult result = WorldDatabase.Query(Trinity::StringFormat("SELECT `source` FROM `ai_dungeon_plan` WHERE `plan` = '{}'", plan).c_str());
    if (!result)
        return false;
    out = (*result)[0].GetString();
    return true;
}

ai::DungeonPlan const* FindPlan(ai::DungeonPlanTable const& table, std::string const& typed)
{
    if (ai::DungeonPlan const* plan = table.Find(typed))
        return plan;
    return table.Find(Lower(typed));
}

int64 UnitsKnown(std::string const& plan)
{
    return LastStepUnitLoad().read ? int64(UnitsOfPlan(plan).size()) : -1;
}
}

bool HandlePlan(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "plan"))
        return false;
    static char const* const usage = ".tb plan list | <plan> info | <plan> check <character> | reload";
    if (args.w.empty())
        return Fail(handler, args, "plan", "missing-argument", usage);
    std::string const first = Lower(args.w[0]);

    // ---- syntax first: nothing is read before the words are known to be well formed ----
    enum class Verb { List, Reload, Info, Check } verb;
    if (first == "list" || first == "reload")
    {
        if (args.w.size() > 1)
            return Fail(handler, args, "plan", "unexpected-argument", args.w[1]);
        verb = first == "list" ? Verb::List : Verb::Reload;
    }
    else
    {
        if (args.w.size() < 2)
            return Fail(handler, args, "plan", "missing-verb", "info or check");
        std::string const word = Lower(args.w[1]);
        if (word == "info")
        {
            if (args.w.size() > 2)
                return Fail(handler, args, "plan", "unexpected-argument", args.w[2]);
            verb = Verb::Info;
        }
        else if (word == "check")
        {
            if (args.w.size() < 3)
                return Fail(handler, args, "plan", "missing-argument", ".tb plan <plan> check <character>");
            if (args.w.size() > 3)
                return Fail(handler, args, "plan", "unexpected-argument", args.w[3]);
            verb = Verb::Check;
        }
        else
            return Fail(handler, args, "plan", "unknown-verb", word);
    }

    ai::DungeonPlanTable& table = ai::GetDungeonPlans();
    if (!table.Sealed())
        return Fail(handler, args, "plan", "not-loaded", "the dungeon plans are not loaded (the plan loader has not run, or the module is off)");

    // ---- list ----
    if (verb == Verb::List)
    {
        Reply reply(handler, args, "plan");
        reply.Begin();
        std::vector<ai::DungeonPlan const*> plans = table.All();
        std::sort(plans.begin(), plans.end(), [](ai::DungeonPlan const* a, ai::DungeonPlan const* b) { return a->name < b->name; });
        for (ai::DungeonPlan const* plan : plans)
        {
            Rec rec("plan");
            rec.S("plan", plan->name).N("map", plan->map).N("legs", int64(LegsOf(*plan).size())).N("steps", int64(plan->steps.size()));
            if (int64 const units = UnitsKnown(plan->name); units >= 0)
                rec.N("units", units);
            reply.Line(rec);
        }
        Rec end("plan");
        end.S("verb", "list").N("plans", int64(plans.size()));
        reply.End(end);
        return true;
    }

    // ---- reload ----
    if (verb == Verb::Reload)
    {
        ai::PlanReloadReport const report = ai::ReloadDungeonPlans(PlansInUse());
        // A successful swap replaced the plan rows: the unit rows are read again against the new table and every cached match is dropped.
        if (report.outcome.swapped)
        {
            ai::LoadDungeonStepUnits();
            ClearPlanChecks();
        }
        Reply reply(handler, args, "plan");
        reply.Begin();
        Rec end("plan");
        end.S("verb", "reload").B("swapped", report.outcome.swapped);
        if (!report.outcome.swapped)
            end.T("why", report.outcome.reason.empty() ? std::string("the reload was refused") : report.outcome.reason);
        else
            end.N("changes", int64(report.outcome.changes.size()));
        end.N("plans", int64(ai::GetDungeonPlans().PlanCount())).N("steps", int64(ai::GetDungeonPlans().StepCount()));
        if (LastStepUnitLoad().read)
            end.N("units", LastStepUnitLoad().kept).N("units_refused", LastStepUnitLoad().refused);
        reply.End(end);
        return true;
    }

    ai::DungeonPlan const* plan = FindPlan(table, args.w[0]);
    if (!plan)
        return Fail(handler, args, "plan", "no-plan", args.w[0] + " is not a loaded dungeon plan (.tb plan list)");

    // ---- info ----
    if (verb == Verb::Info)
    {
        Reply reply(handler, args, "plan");
        reply.Begin();
        std::string source;
        bool const haveSource = ReadPlanSource(plan->name, source);
        reply.Line(PlanRecord(*plan, UnitsKnown(plan->name), haveSource ? &source : nullptr));
        for (uint8 leg : LegsOf(*plan))
            reply.Line(LegRecord(*plan, leg));
        Rec end("plan");
        end.S("verb", "info").S("plan", plan->name);
        // skipped = the rows of this plan in the database that the loaded table does not hold (steps, legs and leg needs the loader refused); omitted when a count
        // could not be read (unknown is not zero).
        uint64 steps = 0, legs = 0, needs = 0;
        if (CountPlanRows("ai_dungeon_step", plan->name, steps) && CountPlanRows("ai_dungeon_leg", plan->name, legs) && CountPlanRows("ai_dungeon_leg_need", plan->name, needs))
        {
            auto short_ = [](uint64 inDb, size_t loaded) { return inDb > loaded ? inDb - loaded : uint64(0); };
            end.N("skipped", int64(short_(steps, plan->steps.size()) + short_(legs, plan->legs.size()) + short_(needs, plan->needs.size())));
        }
        reply.End(end);
        return true;
    }

    // ---- check ----
    Player* bot = FindOnline(args.w[2]);
    if (!bot)
        return Fail(handler, args, "plan", "no-bot", args.w[2] + " is not in the world");
    if (!MayLeadBots(handler, bot->GetGUID()))
        return Fail(handler, args, "plan", "not-allowed", args.w[2] + " is neither a host bot nor your own character");
    Map* map = bot->GetMap();
    if (!bot->IsInWorld() || !map || map->GetId() != plan->map)
        return Fail(handler, args, "plan", "wrong-map", Trinity::StringFormat("{} is not on map {} (the plan's map)", bot->GetName(), plan->map));
    if (UnitsOfPlan(plan->name).empty())
        return Fail(handler, args, "plan", "no-units", "the plan has no ai_dungeon_step_unit rows (a plan exported without --forever, or the rows were not read)");

    PlanCheck const& check = CheckPlanOnMap(plan->name, map, true);
    Reply reply(handler, args, "plan");
    reply.Begin();
    for (UnitCheck const& unit : check.units)
        reply.Line(UnitRecord(unit, unit.result == UnitResult::Missing ? -1 : (SpawnAliveOnMap(map, unit.spawnId) ? 1 : 0)));
    Rec end("plan");
    end.S("verb", "check").S("plan", plan->name).S("bot", bot->GetName()).N("map", check.mapId).N("inst", check.instanceId);
    end.N("units", int64(check.units.size())).N("matched", check.matchedCount).N("near", check.nearCount).N("missing", check.missingCount).N("optional", check.optionalCount);
    end.N("steps", check.steps).N("steps_ok", check.stepsOk).N("steps_short", check.stepsShort);
    reply.End(end);
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

// The default (option-OFF) build: the same refusal the slice-3 and slice-4 stubs give.
bool HandlePlan(ChatHandler* handler, Trinity::ChatCommands::Tail args)
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
    std::string line = "error code=engine-not-built cmd=plan msg=" + msg;
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
