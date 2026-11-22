// Tactical orders M1: see TbTactics.h. The command surfaces and the order queue call Preflight when the order is typed and Run on the bot's
// next tick; nothing here runs on a command's own tick.
#include "host/TbTactics.h"
#include "host/TbH3.h"
#include "host/TbTacticsPure.h"
#include "Config.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <list>
#include <map>
#include <string>
#include <vector>
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_Unit.h"
#include "playerbot/ForceRebuffPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PullJoinWindowPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Engine.h"
#include "playerbot/strategy/Strategy.h"
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

// M3 D7: the toggle table is a host setting per bot guid (memory, server uptime; no DB, PlayerbotDbStore is a stub). The
// defaults match section 4.3: aoe on, autocc off, sap on, loot = whatever the kit has (`lootSet` false means leave it alone).
struct ToggleSettings
{
    bool aoe = true;
    bool autocc = false;
    bool sap = true;
    bool lootSet = false;
    bool loot = false;
};

std::map<ObjectGuid, ToggleSettings>& Toggles()
{
    static std::map<ObjectGuid, ToggleSettings> table;
    return table;
}

// The state each toggle's strategy lives in (section 4.3: aoe/autocc combat, sap/loot non-combat).
BotState ToggleStateOf(std::string const& verb)
{
    return (verb == "aoe" || verb == "autocc") ? BotState::BOT_STATE_COMBAT : BotState::BOT_STATE_NON_COMBAT;
}

bool HasToggleStrategy(PlayerbotAI* ai, std::string const& verb)
{
    char const* key = ToggleStrategy(verb);
    return key && *key && ai->HasStrategy(key, ToggleStateOf(verb));
}
}

bool Enabled()
{
    return sConfigMgr->GetBoolDefault("TortoiseBots.Orders.Tactical", true);
}

bool ReadToggle(Player* bot, std::string const& verb, bool& on)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    on = HasToggleStrategy(ai, verb);
    // aoe and sap are negative strategies: their presence is OFF, their absence ON.
    if (verb == "aoe" || verb == "sap")
        on = !on;
    return true;
}

// A `status` read is not meaningful for a bot the change path would refuse: sap on a non-rogue (the absent `no sap` would read
// "on" for a class with no Sap) or a bot with no running engine. The callers refuse that bot and leave it out of the aggregate.
bool ToggleStatusFault(Player* bot, std::string const& verb, std::string& code, std::string& detail)
{
    if (verb == "sap" && bot->GetClass() != CLASS_ROGUE)
    {
        code = "no-rogue";
        detail = "only a rogue has a Sap to toggle";
        return true;
    }
    bool on = false;
    if (!ReadToggle(bot, verb, on))
    {
        code = "no-engine";
        detail = "the bot has no running engine to read";
        return true;
    }
    return false;
}

void BeforeTick(Player* bot, PlayerbotAI* ai)
{
    if (!bot || !ai || !ai->GetAiObjectContext())
        return;
    ToggleSettings const& t = Toggles()[bot->GetGUID()];

    // aoe: `no aoe` present = off, absent = on. Enforce from the table every tick (D7), so a co/nc change does not win.
    {
        bool const wantNoAoe = !t.aoe;
        if (ai->HasStrategy("no aoe", BotState::BOT_STATE_COMBAT) != wantNoAoe)
            ai->ChangeStrategy(wantNoAoe ? "+no aoe" : "-no aoe", BotState::BOT_STATE_COMBAT);
    }
    // autocc: `auto cc` present = on.
    {
        if (ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) != t.autocc)
            ai->ChangeStrategy(t.autocc ? "+auto cc" : "-auto cc", BotState::BOT_STATE_COMBAT);
    }
    // sap: rogues only (a non-rogue has no Sap); `no sap` present = off.
    if (bot->GetClass() == CLASS_ROGUE)
    {
        bool const wantNoSap = !t.sap;
        if (ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT) != wantNoSap)
            ai->ChangeStrategy(wantNoSap ? "+no sap" : "-no sap", BotState::BOT_STATE_NON_COMBAT);
    }
    // loot: only once the table has an entry (the kit default stands otherwise).
    if (t.lootSet)
    {
        if (ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) != t.loot)
            ai->ChangeStrategy(t.loot ? "+loot" : "-loot", BotState::BOT_STATE_NON_COMBAT);
    }
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
    std::string const& verb = spec.verb;

    // M3: corpserun is the one tactical verb that acts on a dead bot, so it comes before the live-bot rule. A body needs the
    // release gates (`no-entrance`); a ghost is already walked by the W1 dead engine (Run returns ok).
    if (verb == "corpserun")
    {
        if (bot->IsAlive())
        {
            code = "not-dead";
            detail = "the bot is not dead";
            return false;
        }
        if (!bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        {
            if (!need("release"))
                return false;
            if (!BotMgr::Instance().ReleaseHasEntrance(bot))
            {
                code = "no-entrance";
                detail = "the instance has no entrance point (no script or no trigger target): a release would revive the bot outside, so it is held";
                return false;
            }
        }
        return true;
    }

    // Every other tactical verb needs a live bot (spec D2, and TB's own rule): a dead bot cannot stop, flee, guard, take a stance, engage or
    // rebuff, and a queued order would only run into its resurrection.
    if (!bot->IsAlive())
    {
        code = "not-alive";
        detail = "the bot is dead";
        return false;
    }
    // M3 toggles: aoe/autocc/sap/loot [on|off|status]. The argument parses through the pure parser; sap is a rogue-only setting.
    if (verb == "aoe" || verb == "autocc" || verb == "sap" || verb == "loot")
    {
        bool ok = false;
        ParseToggle(spec.arg, ok);
        if (!ok)
        {
            code = "bad-argument";
            detail = verb + " [on|off|status]";
            return false;
        }
        if (verb == "sap" && bot->GetClass() != CLASS_ROGUE)
        {
            code = "no-rogue";
            detail = "only a rogue has a Sap to toggle";
            return false;
        }
        return true;
    }
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
    if (verb == "interrupt")
    {
        if (spec.arg.empty())
        {
            code = "bad-argument";
            detail = "interrupt <target> [bot]";
            return false;
        }
        if (!need(spec.arg.c_str()))
            return false;
        Unit* target = ObjectAccessor::GetUnit(*bot, spec.unit);
        if (!target || !target->IsInWorld())
        {
            code = "no-target";
            detail = "no such unit on the bot's map";
            return false;
        }
        if (!target->IsAlive())
        {
            code = "dead-target";
            detail = "the target is dead";
            return false;
        }
        if (!target->IsNonMeleeSpellCast(true))
        {
            code = "not-casting";
            detail = "the target is not casting an interruptible spell";
            return false;
        }
        return true;
    }
    code = "unknown-verb";
    detail = verb;
    return false;
}

Player* PickInterruptBot(std::vector<Player*> const& bots, Unit* target, std::string& action, std::string& reasons)
{
    action.clear();
    reasons.clear();
    std::vector<InterruptProbe> probes;
    std::vector<std::string> why;
    for (Player* bot : bots)
    {
        InterruptProbe probe;
        probe.bot = bot ? bot->GetName() : std::string();
        PlayerbotAI* ai = AiOf(bot);
        std::string reason;
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            reason = "dead";
        else if (!ai || !ai->GetAiObjectContext())
            reason = "no-engine";
        else if (!target || !target->IsInWorld() || bot->GetMap() != target->GetMap())
            reason = "other-map";
        else if (bot->GetDistance(target) > sPlayerbotAIConfig.sightDistance)
            reason = "far";
        else
            for (char const* key : kInterruptActions)
                if (ActionRegistered(ai, key) && ai->HasSpell(key) && ai->IsInterruptableSpellCasting(target, key, true) &&
                    ai->CanCastSpell(key, target, 0, nullptr, true, true, true))
                {
                    probe.actions.push_back(key);
                    // Review fix: a bot that can land the interrupt from where it stands wins over an earlier one that would walk.
                    if (ai->CanCastSpell(key, target, 0, nullptr, false, true, true))
                        probe.inRange.push_back(key);
                }
        probes.push_back(probe);
        if (probe.actions.empty())
            why.push_back(probe.bot + ":" + (reason.empty() ? "no-spell" : reason));
    }
    InterruptPick const pick = PickInterrupt(probes);
    if (!pick.found)
    {
        for (size_t i = 0; i < why.size(); ++i)
            reasons += (i ? ";" : "") + why[i];
        return nullptr;
    }
    action = pick.action;
    return bots[pick.index];
}

namespace
{
// An interrupt order still trying (see kInterruptWindowMs): one per bot, the newest order replaces the older one.
struct PendingInterrupt
{
    ObjectGuid target;
    ObjectGuid owner;
    std::string action;
    uint32 seq = 0;
    uint32 startMs = 0;
    bool walkQueued = false;
};

std::map<ObjectGuid, PendingInterrupt>& PendingInterrupts()
{
    static std::map<ObjectGuid, PendingInterrupt> pending;
    return pending;
}

uint32 NowMs()
{
    return WorldTimer::getMSTime();
}

// One attempt to get the class's interrupt spell cast at `target`. The class action alone was not enough on the live test (nine accepted orders,
// no cast): its own target is the `interrupt target` pick (cached per value, and only a caster that is already a possible attack target), a
// cast already in progress refuses every other cast (PlayerbotAI::CastSpell's cast-in-progress gate: the bot's own combat AI starts one each
// tick), and a bot that sits or is not facing is turned first and refused for that tick. So: the bot stops its own cast and stands, the
// action's cached picks are dropped so it reads the target again, the class action runs, and when it still refuses the same spell is cast
// directly at the order's target.
bool TryInterruptCast(Player* bot, PlayerbotAI* ai, Unit* target, std::string const& action, Player* requester)
{
    // Only a bot that could cast now gives up its own cast for it: a spell on cooldown, or a target still out of reach, waits for a later tick
    // (the walk is queued by the caller) and must not cost the bot its cast every tick for nothing. The reach rule is the pick value's own
    // (InterruptTargetValue): the spell's range, floored at melee distance.
    uint32 const spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", action)->Get();
    if (!spellId || !sServerFacade.IsSpellReady(bot, spellId))
        return false;
    float reach = ATTACK_DISTANCE;
    float spellRange = 0.0f;
    if (ai->GetSpellRange(action, &spellRange) && spellRange > reach)
        reach = spellRange;
    if (sServerFacade.getDistance2d(bot, target) > reach)
        return false;
    if (bot->IsNonMeleeSpellCast(false, true, true))
        bot->InterruptNonMeleeSpells(false);
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    bot->SetSelection(target->GetObjectGuid());
    ai::AiObjectContext* context = ai->GetAiObjectContext();
    if (ai::Value<Unit*>* current = context->GetValue<Unit*>("current target"))
        current->Set(target);
    if (ai::Value<Unit*>* pick = context->GetValue<Unit*>("interrupt target", action))
        pick->Reset();
    if (ai::Value<std::list<ObjectGuid>>* attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets"))
        attackers->Reset();
    ai::Event const event("tb order", std::string(), requester);
    if (ai->DoSpecificAction(action, event, true))
        return true;
    return ai->CastSpell(action, target);
}

// The queued class action is only the walk to the target for a bot out of reach (its reach prerequisite); the cast itself is TryInterruptCast.
void QueueWalk(Player* bot, PlayerbotAI* ai, std::string const& action, Player* requester)
{
    ai::Engine* engine = ai->GetCurrentEngine();
    if (!engine)
        return;
    if (bot->IsInCombat())
        ai->OnCombatStarted();
    engine->QueueAction(action, ACTION_PASSTROUGH, ai::Event("tb order", std::string(), requester));
}

std::string InterruptTickAt(Player* bot, PlayerbotAI* ai, PendingInterrupt& pending, Player* requester, bool& finished)
{
    Unit* target = ObjectAccessor::GetUnit(*bot, pending.target);
    bool const present = target && target->IsInWorld();
    bool const alive = present && target->IsAlive();
    bool const casting = alive && target->IsNonMeleeSpellCast(true);
    bool requested = false;
    if (casting)
    {
        requested = TryInterruptCast(bot, ai, target, pending.action, requester);
        if (!requested && !pending.walkQueued)
        {
            pending.walkQueued = true;
            if (!ai->CanCastSpell(pending.action, target, 0, nullptr, false, true, true))
                QueueWalk(bot, ai, pending.action, requester);
        }
    }
    InterruptTick const tick = NextInterruptTick(present, alive, casting, requested, NowMs() - pending.startMs);
    finished = tick != InterruptTick::Wait;
    return InterruptTickResult(tick);
}
}

void CancelPendingInterrupt(Player* bot)
{
    if (bot)
        PendingInterrupts().erase(bot->GetGUID());
}

bool PollPendingInterrupt(Player* bot, PlayerbotAI* ai, uint32& seq, std::string& result)
{
    if (!bot || !ai || !ai->GetAiObjectContext())
        return false;
    auto it = PendingInterrupts().find(bot->GetGUID());
    if (it == PendingInterrupts().end())
        return false;
    PendingInterrupt& pending = it->second;
    seq = pending.seq;
    Player* owner = ObjectAccessor::FindPlayer(pending.owner);
    bool finished = false;
    result = InterruptTickAt(bot, ai, pending, (owner && owner->IsInWorld()) ? owner : bot, finished);
    if (finished)
        PendingInterrupts().erase(it);
    return true;
}

std::string Run(Player* bot, PlayerbotAI* ai, std::string const& verb, std::string const& arg, ObjectGuid unit, Player* owner, uint32 seq)
{
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
    if (verb == "interrupt")
    {
        // A newer order of this bot replaces an older pending one.
        PendingInterrupt pending;
        pending.target = unit;
        pending.owner = requester->GetGUID();
        pending.action = arg;
        pending.seq = seq;
        pending.startMs = NowMs();
        bool finished = false;
        std::string const result = InterruptTickAt(bot, ai, pending, requester, finished);
        if (finished)
            PendingInterrupts().erase(bot->GetGUID());
        else
            PendingInterrupts()[bot->GetGUID()] = pending;
        return result;
    }
    // M3 toggles: the queued Run writes the host table entry (D7) and changes the strategy at once; BeforeTick re-enforces it.
    if (verb == "aoe" || verb == "autocc" || verb == "sap" || verb == "loot")
    {
        bool ok = false;
        ToggleOp const op = ParseToggle(arg, ok);
        if (!ok)
            return "fail:bad-argument";
        if (verb == "sap" && bot->GetClass() != CLASS_ROGUE)
            return "fail:no-rogue";
        bool current = false;
        if (!ReadToggle(bot, verb, current))
            return "fail:no-engine";
        ToggleSettings& t = Toggles()[bot->GetGUID()];
        bool const wanted = ToggleWanted(op, current);
        if (verb == "aoe")
            t.aoe = wanted;
        else if (verb == "autocc")
            t.autocc = wanted;
        else if (verb == "sap")
            t.sap = wanted;
        else
        {
            t.lootSet = true;
            t.loot = wanted;
        }
        BeforeTick(bot, ai);
        return "ok";
    }
    // M3 corpse run: a body is the release verb with its gates (`no-entrance`, checked in Preflight), then the W1 dead engine
    // walks. A ghost is already walked by the engine, so the queued order answers ok without touching anything.
    if (verb == "corpserun")
    {
        if (bot->IsAlive())
            return "fail:not-dead";
        if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            return "ok";
        return act("release", ai::Event("tb order", std::string(), requester)) ? "ok" : "fail:release";
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
    reply.Line(h3::Rec("order").S("bot", "Mmage").S("verb", "interrupt").N("seq", 42).S("status", "queued"));
    reply.Line(h3::Rec("cmd").S("verb", "interrupt").S("status", "accepted").N("bots", 1).S("result", "bot:Mmage 1").S("action", "counterspell"));
    reply.Line(h3::Rec("cmd").S("verb", "aoe").S("status", "accepted").N("bots", 5).S("result", "party 5").S("state", "off"));
    reply.Line(h3::Rec("order").S("bot", "Mtank").S("verb", "aoe").N("seq", 43).S("status", "read").S("state", "on"));
    reply.Line(h3::Rec("order").S("bot", "Mrogue").S("verb", "sap").S("status", "refused").S("code", "no-rogue").T("msg", "only a rogue has a Sap to toggle"));
    reply.Line(h3::Rec("order").S("bot", "Mtank").S("verb", "corpserun").N("seq", 44).S("status", "queued"));
    reply.Line(h3::Rec("order").S("bot", "Mtank").S("verb", "corpserun").S("status", "refused").S("code", "not-dead").T("msg", "the bot is not dead"));
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

std::string Run(Player*, PlayerbotAI*, std::string const&, std::string const&, ObjectGuid, Player*, uint32)
{
    return "fail:engine-not-built";
}

bool PollPendingInterrupt(Player*, PlayerbotAI*, uint32&, std::string&)
{
    return false;
}

void CancelPendingInterrupt(Player*) {}

Player* PickInterruptBot(std::vector<Player*> const&, Unit*, std::string& action, std::string& reasons)
{
    action.clear();
    reasons = "no-engine";
    return nullptr;
}

bool ReadToggle(Player*, std::string const&, bool& on)
{
    on = false;
    return false;
}

bool ToggleStatusFault(Player*, std::string const&, std::string& code, std::string& detail)
{
    code = "no-engine";
    detail = "the slice engine is not built";
    return true;
}

void BeforeTick(Player*, PlayerbotAI*) {}

void AppendState(h3::Rec&, Player*) {}
void SelfTestRecords(h3::Reply&) {}
}
}

#endif
