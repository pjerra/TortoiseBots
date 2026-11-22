// Commander port, package P3: `.tb cmd <verb> ... [#token]`. Resolves the caller's commander binding into a `run::Party` and delegates
// every order to the lifted judge (`run::Issue`) so commander orders and dungeon-run orders share one validator, one executor and one card log.
#include "host/cmd/TbCmdOrders.h"
#include "host/cmd/TbCmdTasks.h"
#include "host/TbH3.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/cmd/TbCmdBinding.h"
#include "host/cmd/TbCmdTactics.h"
#include "host/cmd/TbPlace.h"
#include "host/run/TbRunOrders.h"

#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <string>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
bool HandleCmd(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    h3::Words const args = h3::Split(tail);
    if (!h3::RequireBuilt(handler, args, "cmd"))
        return false;
    if (args.badToken)
        return h3::Fail(handler, args, "cmd", "bad-token", "a token starts with # and holds 1-32 characters of [A-Za-z0-9_.:-]");
    if (args.w.empty())
        return h3::Fail(handler, args, "cmd", "missing-verb", "move, walk, pull, pullback, attack, interrupt, focus, rest, revive party, stay, follow, hold, loot, open, use, gossip, state, lootmethod, place, rally, fallback, resume, stop, flee, guard, stance, engage, rebuff");

    Player* sender = h3::SenderPlayer(handler);
    if (!sender)
        return h3::Fail(handler, args, "cmd", "needs-player-sender", "`.tb cmd` addresses the caller's commander binding; run it from a player character");

    ObjectGuid lead;
    std::vector<ObjectGuid> members;
    if (!ResolveBinding(sender->GetGUID(), lead, members) || lead.IsEmpty())
        return h3::Fail(handler, args, "cmd", "not-bound", "you have no bound bots (`.tb master <bot|party> <commander>`)");

    {
        std::string code, detail;
        if (!h3::CheckBotAccess(handler, lead, code, detail))
            return h3::Fail(handler, args, "cmd", code, detail);
        for (ObjectGuid const& m : members)
            if (!h3::CheckBotAccess(handler, m, code, detail))
                return h3::Fail(handler, args, "cmd", code, detail);
    }

    // Any later command from the commander ends a pending cursor request, whichever verb it is.
    place::CancelPending(sender->GetGUID(), handler);

    // Issue #15: place, rally and fall back are the module's own placements, not the lifted order judge's (one order per bot slot, no single order
    // string); they run before the judge and answer with the same `cmd` reply. The words go over as typed: the verb is part of the command.
    if (place::Handle(handler, args))
        return true;

    std::string const verb = h3::Lower(args.w[0]);
    bool taskOk = false;
    if (TryHandleTaskVerb(handler, args, verb, 1, &taskOk))
        return taskOk;

    // The tactical orders are settings and direct actions the judge does not know; they answer their own `cmd` reply.
    bool tacticalOk = false;
    if (TryHandleTacticalVerb(handler, args, verb, lead, members, sender, &tacticalOk))
        return tacticalOk;

    std::string order;
    for (size_t i = 0; i < args.w.size(); ++i)
        order += (i ? " " : "") + args.w[i];

    run::Party party;
    party.leader = lead;
    party.members = std::move(members);
    party.owner = sender->GetGUID();
    party.fromRun = false;
    party.persistentIcons = true;
    party.resolveTarget = [](uint64 /*handle*/, std::string const& token, ObjectGuid& out) -> bool
    {
        return h3::ParseGuidText(token, out);
    };

    run::OrderAnswer const answer = run::Issue(party, order, GameTime::GetGameTimeMS(), false);
    if (!answer.accepted)
    {
        std::string const code = answer.code.empty() ? std::string("failed") : answer.code;
        return h3::Fail(handler, args, "cmd", code, answer.text);
    }

    // A fall back ends on the orders that tell the group to do something else (issue #15).
    place::NoteOrdered(sender->GetGUID(), verb);

    Player* leadPlayer = ObjectAccessor::FindPlayer(lead);
    std::string const leadName = leadPlayer ? std::string(leadPlayer->GetName()) : h3::GuidText(lead);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    h3::Rec rec("cmd");
    rec.S("verb", verb).S("status", "accepted").N("bots", answer.bots).S("result", answer.text);
    reply.Line(rec);
    h3::Rec end("cmd");
    end.S("verb", verb).S("lead", leadName).N("bots", answer.bots);
    reply.End(end);
    return true;
}
}
}

#endif
