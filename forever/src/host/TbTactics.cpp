// Tactical orders M1: see TbTactics.h. The command surfaces and the order queue call Preflight when the order is typed and Run on the bot's
// next tick; nothing here runs on a command's own tick.
#include "host/TbTactics.h"
#include "host/TbH3.h"
#include "host/TbTacticsPure.h"
#include "Config.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <string>
#include "MotionMaster.h"
#include "Player.h"

#include "compat/TbCompat_Unit.h"
#include "playerbot/ForceRebuffPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PullJoinWindowPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/Stances.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace tactics
{
static_assert(kEngageDefaultSeconds == ai::kEngageDefaultDelaySeconds, "the pure default must stay the engine's engage default");

namespace
{
PlayerbotAI* AiOf(Player* bot)
{
    return bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
}

// The context's own lookup, caching a miss (the same probe TbOrders.cpp makes for its actions).
bool ActionRegistered(PlayerbotAI* ai, char const* key)
{
    return ai->GetAiObjectContext() && ai->GetAiObjectContext()->GetAction(key) != nullptr;
}
}

bool Enabled()
{
    return sConfigMgr->GetBoolDefault("TortoiseBots.Orders.Tactical", true);
}

bool Preflight(Player* bot, h3::OrderSpec const& spec, std::string& code, std::string& detail)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
    {
        code = "no-engine";
        detail = "the bot has no running engine (`.tb engine <bot> on`)";
        return false;
    }
    auto const need = [&](char const* key)
    {
        if (ActionRegistered(ai, key))
            return true;
        code = "action-not-registered";
        detail = std::string("the legacy action `") + key + "` is not registered in this build";
        return false;
    };
    // Every tactical verb needs a live bot (spec D2, and TB's own rule): a dead bot cannot stop, flee, guard, take a stance, engage or
    // rebuff, and a queued order would only run into its resurrection.
    if (!bot->IsAlive())
    {
        code = "not-alive";
        detail = "the bot is dead";
        return false;
    }
    std::string const& verb = spec.verb;
    if (verb == "stop")
        return true;
    if (verb == "flee")
        return need("flee chat shortcut");
    if (verb == "guard")
    {
        if (!need("guard chat shortcut"))
            return false;
        if (!ai->GetAiObjectContext()->GetStrategy("guard"))
        {
            code = "strategy-not-registered:guard";
            detail = "the `guard` strategy is not registered in this build";
            return false;
        }
        return true;
    }
    if (verb == "stance")
    {
        if (!need("stance"))
            return false;
        std::string word;
        if (!NormaliseStance(spec.arg, word))
        {
            code = "bad-argument";
            detail = "stance <near|tank|turnback|turn|behind>";
            return false;
        }
        if (!ai->GetAiObjectContext()->GetValue<ai::Stance*>("stance"))
        {
            code = "value-not-registered";
            detail = "the value `stance` is not registered in this build";
            return false;
        }
        return true;
    }
    if (verb == "engage")
    {
        unsigned seconds = 0;
        if (!ParseEngageSeconds(spec.arg, seconds))
        {
            code = "bad-argument";
            detail = "engage [seconds 0-60]";
            return false;
        }
        return true;
    }
    if (verb == "rebuff")
    {
        if (bot->IsInCombat())
        {
            code = "in-combat";
            detail = "a bot in a fight does not start a rebuff";
            return false;
        }
        if (!ai->GetAiObjectContext()->GetValue<int32>("manual int", "force rebuff begin ms") ||
            !ai->GetAiObjectContext()->GetValue<bool>("manual bool", "force rebuff buff pending") ||
            !ai->GetAiObjectContext()->GetValue<bool>("manual bool", "force rebuff buff proposed"))
        {
            code = "value-not-registered";
            detail = "the force-rebuff window values are not registered in this build";
            return false;
        }
        return true;
    }
    code = "unknown-verb";
    detail = verb;
    return false;
}

std::string Run(Player* bot, PlayerbotAI* ai, std::string const& verb, std::string const& arg, ObjectGuid unit, Player* owner)
{
    (void)unit;   // M1 takes no unit; the interrupt order (M2) is the first
    if (!bot || !ai || !ai->GetAiObjectContext())
        return "fail:no-engine";

    // D4: every tactical order but `engage` ends an engage hold. The hold keeps the bots from joining a fight until the commander's first hit;
    // a new order replaces it (Action.cpp keeps the same rule).
    if (verb != "engage" && ai->HasEngagePullWindow())
        ai->EndPullWindow("order");

    Player* requester = (owner && owner->IsInWorld()) ? owner : bot;
    auto const act = [&](char const* key, ai::Event event) { return ai->DoSpecificAction(key, event, true); };

    if (verb == "stop")
    {
        bot->CombatStopWithPets(true);
        ai->Reset(false);
        bot->GetMotionMaster()->Clear();
        if (ai::Value<ObjectGuid>* target = ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target"))
            target->Set(ObjectGuid());
        return "ok";
    }
    if (verb == "flee")
    {
        ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
        bot->CombatStopWithPets(true);
        ai->Reset(false);
        bot->GetMotionMaster()->Clear();
        return act("flee chat shortcut", ai::Event("tb order", std::string(), requester)) ? "ok" : "fail:flee";
    }
    if (verb == "guard")
        return act("guard chat shortcut", ai::Event("tb order", std::string(), requester)) ? "ok" : "fail:guard";
    if (verb == "stance")
    {
        std::string word;
        if (!NormaliseStance(arg, word))
            return "fail:bad-argument";
        bool const done = act("stance", ai::Event("tb order", word, requester));
        ai::Value<ai::Stance*>* value = ai->GetAiObjectContext()->GetValue<ai::Stance*>("stance");
        ai::Stance* stance = value ? value->Get() : nullptr;
        return done && stance && stance->GetName() == word ? "ok" : "fail:stance";
    }
    if (verb == "engage")
    {
        unsigned seconds = 0;
        if (!ParseEngageSeconds(arg, seconds))
            return "fail:bad-argument";
        ai->StartEngageWindow(GuidKeyCompat(requester->GetGUID()), seconds);
        return "ok";
    }
    if (verb == "rebuff")
    {
        if (bot->IsInCombat())
            return "fail:in-combat";
        ai::Value<int32>* begin = ai->GetAiObjectContext()->GetValue<int32>("manual int", "force rebuff begin ms");
        ai::Value<bool>* pending = ai->GetAiObjectContext()->GetValue<bool>("manual bool", "force rebuff buff pending");
        ai::Value<bool>* proposed = ai->GetAiObjectContext()->GetValue<bool>("manual bool", "force rebuff buff proposed");
        if (!begin || !pending || !proposed)
            return "fail:value-not-registered";
        begin->Set(int32(WorldTimer::getMSTime()));
        pending->Set(false);
        proposed->Set(false);
        return "ok";
    }
    return "fail:unknown-verb";
}

void AppendState(h3::Rec& rec, Player* bot)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
    {
        rec.S("stance", "").S("rebuff", "");
        return;
    }
    std::string stance;
    if (ai::Value<ai::Stance*>* value = ai->GetAiObjectContext()->GetValue<ai::Stance*>("stance"))
        if (ai::Stance* current = value->Get())
            stance = current->GetName();
    rec.S("stance", stance);
    int32 beginMs = 0;
    if (ai::Value<int32>* begin = ai->GetAiObjectContext()->GetValue<int32>("manual int", "force rebuff begin ms"))
        beginMs = begin->Get();
    uint32 const nowMs = WorldTimer::getMSTime();
    if (beginMs && ai::ForceRebuffPending(uint32(beginMs), nowMs))
        rec.N("rebuff", int64((ai::ForceRebuffWindowMs() - (nowMs - uint32(beginMs))) / 1000));
    else
        rec.S("rebuff", "");
}

void SelfTestRecords(h3::Reply& reply)
{
    reply.Line(h3::Rec("order").S("bot", "Mtank").S("verb", "stance").N("seq", 41).S("status", "queued"));
    reply.Line(h3::Rec("order").S("bot", "Mtank").S("verb", "rebuff").S("status", "refused").S("code", "in-combat").T("msg", "a bot in a fight does not start a rebuff"));
    reply.Line(h3::Rec("cmd").S("verb", "stance").S("status", "accepted").N("bots", 5).S("result", "party 5").S("stance", "behind"));
    reply.Line(h3::Rec("cmd").S("verb", "engage").S("status", "accepted").N("bots", 5).S("result", "party 5").N("seconds", 5));
    reply.Line(h3::Rec("selftest").S("case", "state-tactics").S("bot", "Mtank").S("stance", "behind").N("rebuff", 37));
}
}
}

#else   // not built with the slice: the same refusal the slice-3 stub gives (TbH3Off.cpp)

namespace TortoiseBots
{
namespace tactics
{
bool Enabled()
{
    return false;
}

bool Preflight(Player*, h3::OrderSpec const&, std::string& code, std::string& detail)
{
    code = "engine-not-built";
    detail = "the engine is not built in";
    return false;
}

std::string Run(Player*, PlayerbotAI*, std::string const&, std::string const&, ObjectGuid, Player*)
{
    return "fail:engine-not-built";
}

void AppendState(h3::Rec&, Player*) {}
void SelfTestRecords(h3::Reply&) {}
}
}

#endif
