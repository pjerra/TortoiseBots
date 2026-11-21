// Commander P6: task orders (objects, loot windows, gossip, lootmethod, state).
// Calls `run::tasks::BeginCommander`, `run::tasks::SetLootMethodForParty` and `run::tasks::BotStatesOf` so `.tb cmd` and `.tb run` share one task engine.
#include "host/cmd/TbCmdTasks.h"

#include "Chat.h"
#include "host/TbH3.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRunTasks.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <list>
#include <string>
#include <strings.h>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace
{
constexpr float kMySearchYd = 40.0f;

bool AllDigits(std::string const& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

std::string RebuildTail(std::vector<std::string> const& words, size_t start)
{
    std::string out;
    for (size_t i = start; i < words.size(); ++i)
    {
        if (!out.empty())
            out += ' ';
        out += words[i];
    }
    return out;
}

// Resolves the commander and its bound party (lead first) through P3's `ResolveBinding`.
// If `sender` is null (console), falls back to the commander of `botHint` when a bot name is given.
bool ResolveCommanderParty(ChatHandler* handler, std::string const& botHint, ObjectGuid& commanderGuid, Player*& lead, std::vector<Player*>& bots, std::string& code, std::string& detail)
{
    bots.clear();
    lead = nullptr;
    Player* sender = h3::SenderPlayer(handler);
    Player* commander = sender;
    if (!commander && !botHint.empty())
    {
        Player* hint = h3::FindOnline(botHint);
        if (hint && h3::IsHostBot(hint))
        {
            ObjectGuid const cg = h3::CommanderGuid(hint->GetGUID());
            if (!cg.IsEmpty())
                commander = ObjectAccessor::FindPlayer(cg);
        }
    }
    if (!commander)
    {
        code = sender ? "not-bound" : "needs-player-sender";
        detail = sender ? "you have no bound bots (`.tb master <bot|party> <commander>`)" : "from the console, name a bound bot or run as a GM character";
        return false;
    }

    commanderGuid = commander->GetGUID();
    ObjectGuid leadGuid;
    std::vector<ObjectGuid> memberGuids;
    if (!ResolveBinding(commanderGuid, leadGuid, memberGuids))
    {
        code = "not-bound";
        detail = "you have no bound bots (`.tb master <bot|party> <commander>`)";
        return false;
    }

    for (ObjectGuid const& g : memberGuids)
    {
        Player* bot = ObjectAccessor::FindPlayer(g);
        if (bot && bot->IsInWorld() && IsCommandedBy(commander, bot))
        {
            if (!h3::CheckBotAccess(handler, bot, code, detail))
                return false;
            bots.push_back(bot);
        }
    }
    if (bots.empty())
    {
        code = "not-bound";
        detail = "you command no bound bots in the world (use `.tb master`)";
        return false;
    }

    for (size_t i = 0; i < bots.size(); ++i)
    {
        if (bots[i]->GetGUID() == leadGuid)
        {
            lead = bots[i];
            if (i != 0)
                std::swap(bots[0], bots[i]);
            break;
        }
    }
    if (!lead)
        lead = bots.front();
    return true;
}

Player* FindBoundBot(std::vector<Player*> const& bots, std::string const& name)
{
    for (Player* bot : bots)
        if (bot && strcasecmp(bot->GetName().c_str(), name.c_str()) == 0)
            return bot;
    return nullptr;
}

bool ResolveCmdTarget(Player* anchor, Player* sender, run::tasks::Verb verb, std::string const& arg, ObjectGuid& out, std::string& code, std::string& detail)
{
    out = ObjectGuid::Empty;
    bool const isLoot = verb == run::tasks::Verb::Loot;
    bool const isGossip = verb == run::tasks::Verb::Gossip;
    bool const isGoOnly = verb == run::tasks::Verb::Open || verb == run::tasks::Verb::Use;
    Map* map = anchor ? anchor->FindMap() : nullptr;
    if (!map)
    {
        code = "unavailable";
        detail = "the anchor character is on no map";
        return false;
    }

    std::string const lower = h3::Lower(arg);
    if (lower == "my")
    {
        if (!sender)
        {
            code = "needs-player-sender";
            detail = "`my` requires a player sender; from the console pass a 32-hex guid or entry:<id>";
            return false;
        }
        ObjectGuid const sel = sender->GetTarget();
        if (!sel.IsEmpty())
        {
            if (isGossip && sel.IsAnyTypeCreature())
            {
                Creature* c = ObjectAccessor::GetCreature(*sender, sel);
                if (c && c->IsAlive())
                {
                    out = sel;
                    return true;
                }
            }
            else if (isLoot && sel.IsAnyTypeCreature())
            {
                Creature* c = ObjectAccessor::GetCreature(*sender, sel);
                if (c && !c->IsAlive())
                {
                    out = sel;
                    return true;
                }
            }
            else if ((isLoot || isGoOnly) && sel.IsGameObject())
            {
                GameObject* go = map->GetGameObject(sel);
                if (go && go->isSpawned())
                {
                    out = sel;
                    return true;
                }
            }
            code = run::tasks::RC::kCodeInvalidTarget;
            detail = "your selection is not a valid target for this verb";
            return false;
        }
        if (isGossip)
        {
            std::list<Creature*> npcs;
            sender->GetCreatureListWithOptionsInGrid(npcs, kMySearchYd, FindCreatureOptions{});
            float best = kMySearchYd + 1.0f;
            for (Creature* c : npcs)
            {
                if (!c || !c->IsAlive() || !c->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
                    continue;
                float const d = sender->GetDistance(c);
                if (d < best)
                {
                    best = d;
                    out = c->GetGUID();
                }
            }
            if (!out.IsEmpty())
                return true;
            code = TortoiseBots::GossipOrder::kCodeNoNpc;
            detail = "no gossip NPC selected or within 40 yd of you";
            return false;
        }
        if (isLoot)
        {
            std::list<Creature*> bodies;
            sender->GetCreatureListWithOptionsInGrid(bodies, kMySearchYd, FindCreatureOptions{});
            float best = kMySearchYd + 1.0f;
            for (Creature* c : bodies)
            {
                if (!c || c->IsAlive())
                    continue;
                float const d = sender->GetDistance(c);
                if (d < best)
                {
                    best = d;
                    out = c->GetGUID();
                }
            }
        }
        std::list<GameObject*> objects;
        sender->GetGameObjectListWithOptionsInGrid(objects, kMySearchYd, FindGameObjectOptions{});
        float best = out.IsEmpty() ? kMySearchYd + 1.0f : sender->GetDistance(ObjectAccessor::GetCreature(*sender, out));
        for (GameObject* go : objects)
        {
            if (!go || !go->isSpawned())
                continue;
            float const d = sender->GetDistance(go);
            if (d < best)
            {
                best = d;
                out = go->GetGUID();
            }
        }
        if (!out.IsEmpty())
            return true;
        code = run::tasks::RC::kCodeInvalidTarget;
        detail = isLoot ? "no corpse or game object selected or within 40 yd of you" : "no spawned game object selected or within 40 yd of you";
        return false;
    }

    if (h3::ParseGuidText(arg, out))
        return true;

    run::tasks::RC::TargetSpec spec;
    if (run::tasks::RC::ParseTargetSpec(arg, spec) && spec.ByEntry())
    {
        float const px = spec.hasPoint ? spec.point.x : anchor->GetPositionX();
        float const py = spec.hasPoint ? spec.point.y : anchor->GetPositionY();
        float const pz = spec.hasPoint ? spec.point.z : anchor->GetPositionZ();
        float const maxYd = spec.hasPoint ? run::tasks::kEntrySearchYd : kMySearchYd;
        float const gridRange = anchor->GetDistance(px, py, pz) + maxYd + 1.0f;
        float best = maxYd + 0.001f;

        if (!isGossip)
        {
            std::list<GameObject*> objects;
            anchor->GetGameObjectListWithEntryInGrid(objects, spec.entry, gridRange);
            for (GameObject* go : objects)
            {
                if (!go || !go->isSpawned())
                    continue;
                float const d = go->GetDistance(px, py, pz);
                if (d <= maxYd && d < best)
                {
                    best = d;
                    out = go->GetGUID();
                }
            }
        }
        if ((isLoot || isGossip) && out.IsEmpty())
        {
            std::list<Creature*> units;
            anchor->GetCreatureListWithEntryInGrid(units, spec.entry, gridRange);
            for (Creature* c : units)
            {
                if (!c)
                    continue;
                if (isGossip && !c->IsAlive())
                    continue;
                if (isLoot && c->IsAlive())
                    continue;
                float const d = c->GetDistance(px, py, pz);
                if (d <= maxYd && d < best)
                {
                    best = d;
                    out = c->GetGUID();
                }
            }
        }
        if (!out.IsEmpty())
            return true;
        code = isGossip ? TortoiseBots::GossipOrder::kCodeNoNpc : run::tasks::RC::kCodeInvalidTarget;
        detail = "no matching spawned target for " + arg;
        return false;
    }

    code = "invalid";
    detail = "target must be `my`, a 32-hex guid, or `entry:<id>[@x,y,z]`";
    return false;
}

bool HandleLootOpenUse(ChatHandler* handler, h3::Words const& args, std::string const& verb, size_t argStart)
{
    if (argStart >= args.w.size())
        return h3::Fail(handler, args, "cmd", "invalid", "usage: .tb cmd " + verb + " <my|guid|entry:<id>> [bot]" + (verb == "loot" ? " [itemId]" : ""));

    std::string const& targetArg = args.w[argStart];
    if (verb == "use" && (AllDigits(targetArg) || h3::Lower(targetArg).rfind("item:", 0) == 0))
        return h3::Fail(handler, args, "cmd", "unsupported", run::tasks::UseUnsupportedText());

    std::string botArg;
    uint32 itemId = 0;
    size_t next = argStart + 1;
    if (next < args.w.size())
    {
        if (verb == "loot" && AllDigits(args.w[next]))
            itemId = uint32(std::strtoul(args.w[next].c_str(), nullptr, 10));
        else
        {
            botArg = args.w[next];
            ++next;
            if (verb == "loot" && next < args.w.size())
            {
                if (!AllDigits(args.w[next]))
                    return h3::Fail(handler, args, "cmd", "invalid", "itemId must be a number");
                itemId = uint32(std::strtoul(args.w[next].c_str(), nullptr, 10));
            }
        }
    }

    ObjectGuid commanderGuid;
    Player* lead = nullptr;
    std::vector<Player*> bots;
    std::string code, detail;
    if (!ResolveCommanderParty(handler, botArg, commanderGuid, lead, bots, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    Player* chosenBot = botArg.empty() ? lead : FindBoundBot(bots, botArg);
    if (!chosenBot)
        return h3::Fail(handler, args, "cmd", "no-bot", botArg + " is not in your bound party");

    run::tasks::Verb const v = verb == "loot" ? run::tasks::Verb::Loot : (verb == "open" ? run::tasks::Verb::Open : run::tasks::Verb::Use);
    Player* sender = h3::SenderPlayer(handler);
    ObjectGuid targetGuid;
    if (!ResolveCmdTarget(sender ? sender : chosenBot, sender, v, targetArg, targetGuid, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    run::tasks::TaskOrder order;
    order.verb = v;
    order.bot = chosenBot->GetName();
    order.itemId = itemId;

    run::OrderAnswer const ans = run::tasks::BeginCommander(commanderGuid, lead, bots, order, targetGuid, GameTime::GetGameTimeMS());
    if (!ans.accepted)
        return h3::Fail(handler, args, "cmd", ans.code, ans.text);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    h3::Rec rec("cmd");
    rec.S("verb", verb).S("bot", chosenBot->GetName()).S("status", "accepted").S("target", h3::GuidText(targetGuid));
    reply.Line(rec);
    h3::Rec end("cmd");
    end.S("verb", verb).N("ok", 1).N("refused", 0);
    reply.End(end);
    return true;
}

bool HandleGossip(ChatHandler* handler, h3::Words const& args, size_t argStart)
{
    if (argStart >= args.w.size())
        return h3::Fail(handler, args, "cmd", "invalid", "usage: .tb cmd gossip <my|guid|entry:<id>> [bot] [optionIndex|\"option text\"|text]");

    std::string const& targetArg = args.w[argStart];
    std::string maybeBot = (argStart + 1 < args.w.size()) ? args.w[argStart + 1] : std::string();

    ObjectGuid commanderGuid;
    Player* lead = nullptr;
    std::vector<Player*> bots;
    std::string code, detail;
    if (!ResolveCommanderParty(handler, maybeBot, commanderGuid, lead, bots, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    Player* chosenBot = lead;
    size_t optStart = argStart + 1;
    if (!maybeBot.empty())
    {
        if (Player* explicitBot = FindBoundBot(bots, maybeBot))
        {
            chosenBot = explicitBot;
            optStart = argStart + 2;
        }
    }

    run::tasks::TaskOrder order;
    order.verb = run::tasks::Verb::Gossip;
    order.bot = chosenBot->GetName();
    std::string option = GossipOrder::Trimmed(RebuildTail(args.w, optStart));
    if (!option.empty())
    {
        order.gossipHasOption = true;
        if (option.front() == '"')
        {
            if (option.size() < 3 || option.back() != '"')
                return h3::Fail(handler, args, "cmd", "invalid", "unclosed quote in gossip option");
            order.gossipText = option.substr(1, option.size() - 2);
            if (GossipOrder::Trimmed(order.gossipText).empty())
                return h3::Fail(handler, args, "cmd", "invalid", "empty gossip option");
        }
        else if (AllDigits(option))
        {
            if (option.size() > 2)
                return h3::Fail(handler, args, "cmd", "invalid", "gossip option index out of range");
            uint32 const idx = uint32(std::strtoul(option.c_str(), nullptr, 10));
            if (idx == 0 || idx > 32)
                return h3::Fail(handler, args, "cmd", "invalid", "gossip option index must be 1..32");
            order.gossipByIndex = true;
            order.gossipIndex = idx;
        }
        else
            order.gossipText = option;
    }

    Player* sender = h3::SenderPlayer(handler);
    ObjectGuid targetGuid;
    if (!ResolveCmdTarget(sender ? sender : chosenBot, sender, run::tasks::Verb::Gossip, targetArg, targetGuid, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    run::OrderAnswer const ans = run::tasks::BeginCommander(commanderGuid, lead, bots, order, targetGuid, GameTime::GetGameTimeMS());
    if (!ans.accepted)
        return h3::Fail(handler, args, "cmd", ans.code, ans.text);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    h3::Rec rec("cmd");
    rec.S("verb", "gossip").S("bot", chosenBot->GetName()).S("status", "accepted").S("target", h3::GuidText(targetGuid));
    reply.Line(rec);
    h3::Rec end("cmd");
    end.S("verb", "gossip").N("ok", 1).N("refused", 0);
    reply.End(end);
    return true;
}

bool HandleLootMethod(ChatHandler* handler, h3::Words const& args, size_t argStart)
{
    std::vector<std::string> subArgs(args.w.begin() + std::min(argStart, args.w.size()), args.w.end());
    run::tasks::TaskOrder order;
    std::string code, detail;
    if (!run::tasks::ParseTaskOrder("lootmethod", subArgs, order, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    ObjectGuid commanderGuid;
    Player* lead = nullptr;
    std::vector<Player*> bots;
    if (!ResolveCommanderParty(handler, std::string(), commanderGuid, lead, bots, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    run::OrderAnswer const ans = run::tasks::SetLootMethodForParty(lead, bots, order);
    if (!ans.accepted)
        return h3::Fail(handler, args, "cmd", ans.code, ans.text);

    Group* group = lead->GetGroup();
    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    h3::Rec rec("cmd");
    rec.S("verb", "lootmethod").S("status", "ok").S("method", subArgs[0]).N("threshold", group ? uint32(group->GetLootThreshold()) : 2);
    reply.Line(rec);
    h3::Rec end("cmd");
    end.S("verb", "lootmethod").N("ok", 1).N("refused", 0);
    reply.End(end);
    return true;
}

bool HandleState(ChatHandler* handler, h3::Words const& args, size_t argStart)
{
    std::string botArg = argStart < args.w.size() ? args.w[argStart] : std::string();
    ObjectGuid commanderGuid;
    Player* lead = nullptr;
    std::vector<Player*> bots;
    std::string code, detail;
    if (!ResolveCommanderParty(handler, botArg, commanderGuid, lead, bots, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    if (!botArg.empty())
    {
        Player* only = FindBoundBot(bots, botArg);
        if (!only)
            return h3::Fail(handler, args, "cmd", "no-bot", botArg + " is not in your bound party");
        bots = { only };
    }

    std::vector<run::tasks::BotTaskState> const states = run::tasks::BotStatesOf(commanderGuid, bots);
    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    for (run::tasks::BotTaskState const& s : states)
    {
        char const* status = s.busy ? "running" : (s.reply == 1 ? "done" : (s.reply == 2 ? "failed" : "none"));
        h3::Rec rec("task");
        rec.S("bot", s.botName).B("busy", s.busy).S("status", status);
        if (!s.verb.empty())
            rec.S("verb", s.verb);
        if (s.entry)
            rec.N("entry", s.entry);
        if (!s.text.empty())
            rec.T("text", s.text);
        reply.Line(rec);
    }
    h3::Rec end("cmd");
    end.S("verb", "state");
    reply.End(end);
    return true;
}
}

bool TryHandleTaskVerb(ChatHandler* handler, h3::Words const& args, std::string const& verb, size_t argStart, bool* outOk)
{
    auto finish = [outOk](bool ok)
    {
        if (outOk)
            *outOk = ok;
        return true;
    };
    if (verb == "loot" || verb == "open" || verb == "use")
        return finish(HandleLootOpenUse(handler, args, verb, argStart));
    if (verb == "gossip")
        return finish(HandleGossip(handler, args, argStart));
    if (verb == "lootmethod")
        return finish(HandleLootMethod(handler, args, argStart));
    if (verb == "state")
        return finish(HandleState(handler, args, argStart));
    if (verb == "ritual" || verb == "scout")
        return finish(h3::Fail(handler, args, "cmd", "unsupported", verb + " is not supported on ForeverCore (decision D5)"));
    return false;
}
}
}

#else

namespace TortoiseBots
{
namespace cmd
{
bool TryHandleTaskVerb(ChatHandler* /*handler*/, h3::Words const& /*args*/, std::string const& /*verb*/, size_t /*argStart*/, bool* /*outOk*/)
{
    return false;
}
}
}

#endif
