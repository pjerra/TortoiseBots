// Commander surface of the tactical orders M1: see TbCmdTactics.h.
#include "host/cmd/TbCmdTactics.h"
#include "host/TbOrdersApi.h"
#include "host/TbTactics.h"
#include "host/TbTacticsPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <string>
#include <vector>
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include "host/cmd/TbCmdBinding.h"

namespace TortoiseBots
{
namespace cmd
{
namespace
{
Player* BoundBot(std::string const& name, ObjectGuid lead, std::vector<ObjectGuid> const& members)
{
    std::string const wanted = h3::Lower(name);
    auto const match = [&](ObjectGuid guid) -> Player*
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        return bot && bot->IsInWorld() && h3::Lower(bot->GetName()) == wanted ? bot : nullptr;
    };
    if (Player* bot = match(lead))
        return bot;
    for (ObjectGuid const& guid : members)
        if (Player* bot = match(guid))
            return bot;
    return nullptr;
}
}

bool TryHandleTacticalVerb(ChatHandler* handler, h3::Words const& args, std::string const& verb, ObjectGuid lead,
    std::vector<ObjectGuid> const& members, Player* sender, bool* outOk)
{
    (void)sender;   // the binding already belongs to the sender (TbCmdOrders.cpp resolved it)
    if (!tactics::IsTacticalVerb(verb))
        return false;
    bool ok = false;
    auto const done = [&]() { if (outOk) *outOk = ok; return true; };
    std::string orderText;
    for (size_t i = 0; i < args.w.size(); ++i)
        orderText += (i ? " " : "") + args.w[i];
    auto const refuse = [&](std::string const& code, std::string const& detail)
    {
        NoteRunOrder(lead, orderText, false, code, detail);
        h3::Fail(handler, args, "cmd", code, detail);
        return done();
    };

    if (!tactics::Enabled())
        return refuse("disabled", "TortoiseBots.Orders.Tactical is 0");

    if (verb == "interrupt")
    {
        if (args.w.size() < 2 || args.w.size() > 3)
            return refuse(args.w.size() < 2 ? "missing-argument" : "bad-argument", "interrupt <guid> [bot]");
        ObjectGuid guid;
        if (!h3::ParseGuidText(args.w[1], guid))
            return refuse("bad-argument", "interrupt <guid> [bot]");
        std::string const namedBot = args.w.size() == 3 ? args.w[2] : std::string();
        Player* leadPlayer = ObjectAccessor::FindPlayer(lead);
        Unit* target = leadPlayer ? ObjectAccessor::GetUnit(*leadPlayer, guid) : nullptr;
        if (!target || !target->IsInWorld())
            return refuse("no-target", "no such unit on the binding's map");
        if (!target->IsAlive())
            return refuse("dead-target", "the target is dead");
        if (!target->IsNonMeleeSpellCast(true))
            return refuse("not-casting", "the target is not casting an interruptible spell");

        std::vector<Player*> candidates;
        if (!namedBot.empty())
        {
            Player* bot = BoundBot(namedBot, lead, members);
            if (!bot)
                return refuse("no-bot", namedBot + " is not a bot of your binding");
            candidates.push_back(bot);
        }
        else
        {
            auto const add = [&](ObjectGuid botGuid)
            {
                Player* bot = ObjectAccessor::FindPlayer(botGuid);
                if (bot && bot->IsInWorld() && std::find(candidates.begin(), candidates.end(), bot) == candidates.end())
                    candidates.push_back(bot);
            };
            add(lead);
            for (ObjectGuid const& member : members)
                add(member);
            if (candidates.empty())
                return refuse("not-alive", "no bot of your binding is in the world");
        }
        std::string action, reasons;
        Player* picked = tactics::PickInterruptBot(candidates, target, action, reasons);
        if (!picked)
            return refuse("no-interrupt", "no bot has a ready interrupt for this target" +
                (reasons.empty() ? std::string() : ": " + reasons));

        h3::OrderSpec spec;
        spec.verb = "interrupt";
        spec.arg = action;
        spec.unit = guid;
        std::string code, detail;
        uint32 seq = 0;
        if (!h3::QueueOrder(picked, spec, code, detail, seq))
            return refuse(code.empty() ? "failed" : code, detail);

        std::string const result = "bot:" + std::string(picked->GetName()) + " 1";
        h3::Reply reply(handler, args, "cmd");
        reply.Begin();
        h3::Rec head("cmd");
        head.S("verb", verb).S("status", "accepted").N("bots", 1).S("result", result).S("action", action);
        reply.Line(head);
        h3::Rec end("cmd");
        end.S("verb", verb).S("lead", leadPlayer ? std::string(leadPlayer->GetName()) : h3::GuidText(lead))
            .N("bots", 1).N("ok", 1).N("refused", 0);
        reply.End(end);
        NoteRunOrder(lead, orderText, true, "", result + " action=" + action);
        ok = true;
        return done();
    }

    // The words after the verb: `stop [bot]`, `stance <word> [bot]`, `engage [seconds]`, the rest take none.
    std::string named, stanceWord, engageText;
    if (verb == "stop")
    {
        if (args.w.size() > 2)
            return refuse("bad-argument", "stop [bot]");
        if (args.w.size() == 2)
            named = args.w[1];
    }
    else if (verb == "stance")
    {
        if (args.w.size() < 2)
            return refuse("missing-argument", "stance <near|tank|turnback|turn|behind> [bot]");
        if (!tactics::NormaliseStance(args.w[1], stanceWord))
            return refuse("bad-argument", "stance <near|tank|turnback|turn|behind> [bot]");
        if (args.w.size() > 3)
            return refuse("bad-argument", "stance <near|tank|turnback|turn|behind> [bot]");
        if (args.w.size() == 3)
            named = args.w[2];
    }
    else if (verb == "engage")
    {
        if (args.w.size() > 2)
            return refuse("bad-argument", "engage [seconds 0-60]");
        if (args.w.size() == 2)
            engageText = args.w[1];
    }
    else if (args.w.size() > 1)
        return refuse("unexpected-argument", verb + " takes no argument");

    unsigned engageSeconds = 0;
    if (verb == "engage" && !tactics::ParseEngageSeconds(engageText, engageSeconds))
        return refuse("bad-argument", "engage [seconds 0-60]");

    std::vector<Player*> addressed;
    if (!named.empty())
    {
        Player* bot = BoundBot(named, lead, members);
        if (!bot)
            return refuse("no-bot", named + " is not a bot of your binding");
        addressed.push_back(bot);
    }
    else
    {
        auto const add = [&](ObjectGuid guid)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (bot && bot->IsInWorld() && bot->IsAlive() && std::find(addressed.begin(), addressed.end(), bot) == addressed.end())
                addressed.push_back(bot);
        };
        add(lead);
        for (ObjectGuid const& guid : members)
            add(guid);
        if (addressed.empty())
            return refuse("not-alive", "no bot of your binding is alive");
    }

    struct Refusal
    {
        Player* bot;
        std::string code;
        std::string detail;
    };
    std::vector<Refusal> refusals;
    uint32 queued = 0;
    for (Player* bot : addressed)
    {
        h3::OrderSpec spec;
        spec.verb = verb;
        spec.arg = verb == "stance" ? stanceWord : (verb == "engage" ? engageText : std::string());
        std::string code, detail;
        uint32 seq = 0;
        if (h3::QueueOrder(bot, spec, code, detail, seq))
            ++queued;
        else
            refusals.push_back({ bot, code, detail });
    }
    if (queued == 0)
    {
        // Nothing was queued: the reply is the one-line refusal.
        Refusal const& first = refusals.front();
        return refuse(first.code.empty() ? "failed" : first.code, first.detail);
    }

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    uint32 const refused = uint32(refusals.size());

    std::string const result = named.empty() ? "party " + std::to_string(queued) : "bot:" + addressed.front()->GetName() + " 1";
    h3::Rec head("cmd");
    head.S("verb", verb).S("status", "accepted").N("bots", int64(queued + refused)).S("result", result);
    if (verb == "stance")
        head.S("stance", stanceWord);
    if (verb == "engage")
        head.N("seconds", int64(engageSeconds));
    if (refused)
        head.N("refused", int64(refused));
    reply.Line(head);
    // The accepted line first, then a row per refused bot (TB-COMMANDS.md section 23).
    for (Refusal const& refusal : refusals)
    {
        h3::Rec rec("cmd");
        rec.S("bot", refusal.bot->GetName()).S("verb", verb).S("status", "refused").S("code", refusal.code).T("msg", refusal.detail);
        reply.Line(rec);
    }
    Player* leadPlayer = ObjectAccessor::FindPlayer(lead);
    h3::Rec end("cmd");
    end.S("verb", verb).S("lead", leadPlayer ? std::string(leadPlayer->GetName()) : h3::GuidText(lead))
        .N("bots", int64(queued + refused)).N("ok", int64(queued)).N("refused", int64(refused));
    reply.End(end);
    NoteRunOrder(lead, orderText, true, "", result);
    ok = true;
    return done();
}
}
}

#else   // not built with the slice: the handler is never reached (TbCmdOrders.cpp is a stub)

namespace TortoiseBots
{
namespace cmd
{
bool TryHandleTacticalVerb(ChatHandler*, h3::Words const&, std::string const&, ObjectGuid, std::vector<ObjectGuid> const&, Player*, bool* outOk)
{
    if (outOk)
        *outOk = false;
    return false;
}
}
}

#endif
