// Slice 3, host package H3: the orders (`.tb order`, `.tb role`, `.tb master`) and the engine hooks behind them.
// Design: forever/docs/SLICE3-H-NOTES.md; the reply grammar: forever/docs/TB-COMMANDS.md.
//
// An order is validated and QUEUED by the command (which answers at once), and run by BeforeEngineTick, which engine::Tick
// (TbEngineBrain.cpp) calls right before the bot's UpdateAI: the legacy ACTION is called by its key through
// PlayerbotAI::DoSpecificAction (PlayerbotAI.h:466), never PlayerbotAI::HandleCommand (spec D1). Both the command and the tick run on the
// world thread (TbH3.h), so the queue is plain data. The outcome of an order is kept per bot and shown by `.tb state` (ord=<seq>:<result>).
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <map>
#include <sstream>
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/actions/PullActions.h"
#include "playerbot/strategy/values/RtiTargetValue.h"
#include "PlayerbotAIStorage.h"
#include "runtime/BotManager.h"

namespace TortoiseBots
{
namespace h3
{
namespace
{
constexpr uint32 OrderMaxAgeMs = 15000;     // an order the engine could not run within this long (no tick: the bot is teleporting, away) expires
constexpr size_t OrderQueueMax = 16;
constexpr uint32 kPullJoinDelaySeconds = 3;       // L2: the held bots' minimum wait after the pull landed (lead decision: 3 s)
constexpr uint32 kPullApproachWindowSeconds = 15; // L2: how long the hold lasts when the pull never lands (AiPlayerbot.PullBackMaxReturnTime's default)

uint32 NowMs()
{
    return uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Order
{
    uint32 seq = 0;
    std::string verb;
    std::string arg;            // follow: member name or "none"; co/nc: the strategy change; role: the role number
    ObjectGuid owner;           // the Event's owner: the commander when in the world, else the bot itself
    ObjectGuid unit;            // attack / pull: the unit
    ObjectGuid who;             // assist: the member whose victim is taken
    float x = 0, y = 0, z = 0;  // come
    uint32 map = 0;
    uint32 queuedAt = 0;
};

struct BotRec
{
    std::deque<Order> queue;
    uint32 seq = 0;             // the last order that finished or is queued
    std::string verb = "none";
    std::string result = "none";   // ok | fail:<code> | queued
};

std::map<ObjectGuid, BotRec>& Recs()
{
    static std::map<ObjectGuid, BotRec> recs;
    return recs;
}

std::map<ObjectGuid, uint8>& Roles()
{
    static std::map<ObjectGuid, uint8> roles;
    return roles;
}

uint32& SeqCounter()
{
    static uint32 seq = 0;
    return seq;
}

constexpr uint8 RoleTank = 1, RoleHealer = 2, RoleDps = 4;      // ChatHelper.h BOT_ROLE_*

bool IsVerb(std::string const& word)
{
    static char const* const verbs[] = { "follow", "stay", "come", "attack", "pull", "endpull", "end", "hold", "free", "assist", "co", "nc" };
    for (char const* verb : verbs)
        if (word == verb)
            return true;
    return false;
}

bool ActionRegistered(PlayerbotAI* ai, char const* key)
{
    // The context's own lookup; it caches what it finds (and a miss), so asking never leaks and never creates a second object.
    return ai->GetAiObjectContext() && ai->GetAiObjectContext()->GetAction(key) != nullptr;
}

PlayerbotAI* AiOf(Player* bot)
{
    return PlayerbotAIStorage::Instance().GetAI(bot);
}

char const* const MarkWords[8] = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" };

// One of the eight raid marks by its word, -1 if the word is none of them.
int MarkIndex(std::string const& word)
{
    for (int i = 0; i < 8; ++i)
        if (word == MarkWords[i])
            return i;
    return -1;
}

// The unit an `attack` / `pull` argument names, for this bot: `my` (the sender's selection, read when the command runs), a raid mark of the
// bot's group (`skull`, `mark`, `mark:<word>`), `spawn:<db spawn id>` (a creature of the bot's map) or the 32-hex guid text of `.tb state`.
bool ResolveTarget(Player* bot, Player* sender, std::string const& argument, ObjectGuid& out, std::string& code, std::string& detail)
{
    std::string const arg = Lower(argument);
    ObjectGuid guid;
    if (arg == "my")
    {
        if (!sender)
        {
            code = "needs-player-sender";
            detail = "`my` is the selection of the player who sent the command; from the console name a guid, a mark or spawn:<id>";
            return false;
        }
        guid = sender->GetTarget();
        if (guid.IsEmpty())
        {
            code = "no-selection";
            detail = "the sender has no target selected";
            return false;
        }
    }
    else if (arg == "mark" || arg.rfind("mark:", 0) == 0 || MarkIndex(arg) >= 0)
    {
        std::string const word = arg == "mark" ? "skull" : (arg.rfind("mark:", 0) == 0 ? arg.substr(5) : arg);
        int const index = MarkIndex(word);
        Group* group = bot->GetGroup();
        if (index < 0)
        {
            code = "bad-target";
            detail = "unknown raid mark " + word;
            return false;
        }
        if (!group)
        {
            code = "no-party";
            detail = "the bot is in no party, so it sees no raid mark";
            return false;
        }
        guid = group->GetTargetIcon(uint8(index));
        if (guid.IsEmpty())
        {
            code = "mark-empty";
            detail = "the " + word + " mark is on nothing";
            return false;
        }
    }
    else if (arg.rfind("spawn:", 0) == 0)
    {
        char* end = nullptr;
        unsigned long long const id = std::strtoull(arg.c_str() + 6, &end, 10);
        Creature* creature = (end && *end == '\0' && id) ? bot->GetMap()->GetCreatureBySpawnId(ObjectGuid::LowType(id)) : nullptr;
        if (!creature)
        {
            code = "no-such-spawn";
            detail = "no creature with that spawn id on the bot's map (it is not loaded or not spawned)";
            return false;
        }
        guid = creature->GetGUID();
    }
    else if (!ParseGuidText(arg, guid))
    {
        code = "bad-target";
        detail = "a target is `my`, a raid mark, spawn:<id> or the 32 hex digits of a guid";
        return false;
    }

    Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
    if (!unit)
    {
        code = "target-not-found";
        detail = "no such unit on the bot's map";
        return false;
    }
    if (!unit->IsAlive())
    {
        code = "target-dead";
        detail = "the target is dead";
        return false;
    }
    if (!bot->IsValidAttackTarget(unit))
    {
        code = "not-attackable";
        detail = "the core says this bot may not attack that unit";
        return false;
    }
    out = guid;
    return true;
}

// The eight raid marks of a group as the engine's mark-based attack reads them: set the mark, run the action, put every mark back. The marks
// are the group's own state and the group (a GM's client) sees them change, so the order leaves nothing of itself behind.
bool AttackThroughMark(Player* bot, PlayerbotAI* ai, Player* owner, ObjectGuid unit, char const* action)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    ObjectGuid saved[8];
    for (uint8 i = 0; i < 8; ++i)
        saved[i] = group->GetTargetIcon(i);

    std::string rti = "skull";
    if (ai::Value<std::string>* value = ai->GetAiObjectContext()->GetValue<std::string>("rti"))
        rti = value->Get();
    int index = MarkIndex(rti);
    if (index < 0)
        index = 7;

    group->SetTargetIcon(uint8(index), unit, owner->GetGUID());
    // "rti target" is a cached value: ask again.
    if (ai::Value<Unit*>* value = ai->GetAiObjectContext()->GetValue<Unit*>("rti target"))
        value->Reset();
    bool const ok = ai->DoSpecificAction(action, Event("tb order", unit, owner), true);

    for (int pass = 0; pass < 2; ++pass)
        for (uint8 i = 0; i < 8; ++i)
            if (group->GetTargetIcon(i) != saved[i])
                group->SetTargetIcon(i, saved[i], owner->GetGUID());
    return ok;
}

// Role: the forced role of the AI, then a rebuild of the kit from it (the slice's AiFactory reads it), then the movement mode back as it was.
void ApplyKit(PlayerbotAI* ai, uint8 role, bool setRole)
{
    bool const stay = ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
    if (setRole)
        ai->SetForcedRole(role);
    ai->ResetStrategies();
    ai->SetMovementStrategy(stay ? "stay" : "follow");
}

std::string Run(Player* bot, PlayerbotAI* ai, Order const& o)
{
    Player* owner = ObjectAccessor::FindPlayer(o.owner);
    if (!owner || !owner->IsInWorld())
        owner = bot;
    auto const act = [&](char const* key, Event event) { return ai->DoSpecificAction(key, event, true); };

    if (o.verb == "follow")
    {
        if (!o.arg.empty() && !act("follow target", Event("tb order", o.arg, owner)))
            return "fail:follow-target";
        return act("follow chat shortcut", Event("tb order", std::string(), owner)) ? "ok" : "fail:follow";
    }
    if (o.verb == "stay")
        return act("stay chat shortcut", Event("tb order", std::string(), owner)) ? "ok" : "fail:stay";
    if (o.verb == "come")
    {
        // The stay position is the spot (PullActions.cpp:128-133 writes it the same way); the stay strategy's own trigger
        // ("return to stay position", StayStrategy.cpp:25-30) walks the bot there. An explicit order is a fresh walk (StayWalkOrdered).
        ai->Reset();
        ai::PositionMap& positions = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
        ai::PositionEntry stay = positions["stay"];
        stay.Set(o.x, o.y, o.z, o.map);
        positions["stay"] = stay;
        ai->SetMovementStrategy("stay");
        TortoiseBots::StayWalkOrdered(ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get().stayWalk);
        return ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ? "ok" : "fail:strategy-not-registered:stay";
    }
    if (o.verb == "attack" || o.verb == "assist")
    {
        ObjectGuid unit = o.unit;
        if (o.verb == "assist")
        {
            Player* who = ObjectAccessor::FindPlayer(o.who);
            Unit* victim = who ? who->GetVictim() : nullptr;
            if (!victim && who)
                victim = ObjectAccessor::GetUnit(*bot, who->GetTarget());
            if (!victim || !victim->IsAlive() || !bot->IsValidAttackTarget(victim))
                return "fail:nothing-to-assist";
            unit = victim->GetGUID();
        }
        if (!bot->GetGroup())
            return "fail:no-party";
        return AttackThroughMark(bot, ai, owner, unit, "attack rti target") ? "ok" : "fail:attack";
    }
    if (o.verb == "pull")
    {
        if (!act("pull my target", Event("tb order", o.unit, owner)))
            return "fail:pull";
        // L2 (package L): `pull my target` places no hold (only the chat `pullback` and the command layer's PausePartyDpsForPull do), so the damage
        // bots and the healer's damage spells opened on a mob the tank had not reached (M package, observation d). The same party hold as the
        // legacy chat pull, lead-keyed: damage warriors stay on the commander's anchor, every member's window opens only after the pull LANDED
        // (PullActions.cpp RestampPullParty) and ends when the whole pack is on the tank (3 s at the earliest, 10 s at the latest), at once when a
        // mob hits a held bot or the tank is under 60 %, and at the latest 15 s after the order if the pull never lands.
        ai::PausePartyPullHold(bot, kPullJoinDelaySeconds, true, kPullApproachWindowSeconds);
        return "ok";
    }
    if (o.verb == "endpull")
        return act("end pull", Event("tb order", std::string(), owner)) ? "ok" : "fail:end-pull";
    if (o.verb == "hold")
    {
        ai->ChangeStrategy("+passive", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("+passive", BotState::BOT_STATE_NON_COMBAT);
        return ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT) ? "ok" : "fail:strategy-not-registered:passive";
    }
    if (o.verb == "free")
    {
        // Drop every order: the factory kit again (it ends a running pull window too), the movement mode follow as engine::Create sets it, and the
        // explicit attack target the attack orders keep.
        ApplyKit(ai, ai->GetForcedRole(), false);
        ai->SetMovementStrategy("follow");
        if (ai::Value<ObjectGuid>* value = ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target"))
            value->Set(ObjectGuid());
        return "ok";
    }
    if (o.verb == "co" || o.verb == "nc")
    {
        ai->ChangeStrategy(o.arg, o.verb == "co" ? BotState::BOT_STATE_COMBAT : BotState::BOT_STATE_NON_COMBAT);
        return "ok";
    }
    if (o.verb == "role")
    {
        ApplyKit(ai, uint8(std::atoi(o.arg.c_str())), true);
        return "ok";
    }
    if (o.verb == "rekit")
    {
        ApplyKit(ai, ai->GetForcedRole(), false);
        return "ok";
    }
    return "fail:unknown-verb";
}

// Queues one order for one bot. Empty string = queued (seq in `seq`), else the refusal code and detail in `code`/`detail`.
bool Enqueue(Player* bot, Order order, std::string& code, std::string& detail, uint32& seq)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
    {
        code = "no-engine";
        detail = "the bot has no running engine (`.tb engine <bot> on`)";
        return false;
    }
    BotRec& rec = Recs()[bot->GetGUID()];
    if (rec.queue.size() >= OrderQueueMax)
    {
        code = "queue-full";
        detail = "the bot's engine is not ticking: " + std::to_string(rec.queue.size()) + " orders wait";
        return false;
    }
    order.seq = ++SeqCounter();
    order.queuedAt = NowMs();
    seq = order.seq;
    rec.seq = order.seq;
    rec.verb = order.verb;
    rec.result = "queued";
    rec.queue.push_back(std::move(order));
    return true;
}

std::string JoinFrom(std::vector<std::string> const& words, size_t from)
{
    std::string out;
    for (size_t i = from; i < words.size(); ++i)
        out += (out.empty() ? "" : " ") + words[i];
    return out;
}
}

// ---- the engine hooks ----

void BeforeEngineTick(Player* bot, PlayerbotAI* ai)
{
    if (!bot || !ai)
        return;
    ObjectGuid const guid = bot->GetGUID();

    // A brain that was built after `.tb role` (the engine was off, or it was rebuilt) starts with no forced role: the stored role is applied
    // before its first tick, so the kit it runs is the one the role asks for.
    if (auto role = Roles().find(guid); role != Roles().end() && ai->GetForcedRole() != role->second)
        ApplyKit(ai, role->second, true);

    auto rec = Recs().find(guid);
    if (rec == Recs().end() || rec->second.queue.empty())
        return;
    uint32 const now = NowMs();
    while (!rec->second.queue.empty())
    {
        Order order = std::move(rec->second.queue.front());
        rec->second.queue.pop_front();
        std::string result = now - order.queuedAt > OrderMaxAgeMs ? "fail:expired" : Run(bot, ai, order);
        if (rec->second.seq == order.seq)       // a later order of the same bot keeps the later one's name and seq
            rec->second.result = result;
        if (result != "ok")
            sLog.outString("TortoiseBots order: %s seq %u (%s) -> %s", bot->GetName().c_str(), order.seq, order.verb.c_str(), result.c_str());
    }
}

bool IsCommandedBot(Player* bot)
{
    return bot && !CommanderGuid(bot->GetGUID()).IsEmpty();
}

OrderStatus LastOrder(ObjectGuid bot)
{
    OrderStatus status;
    if (auto rec = Recs().find(bot); rec != Recs().end())
    {
        status.seq = rec->second.seq;
        status.verb = rec->second.verb;
        status.result = rec->second.result;
    }
    return status;
}

uint8 WantedRole(ObjectGuid bot)
{
    auto role = Roles().find(bot);
    return role == Roles().end() ? uint8(0) : role->second;
}

// ---- `.tb order <bot|party [leader]> <verb> [arg] [#token]` ----

bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "order"))
        return false;
    Player* sender = SenderPlayer(handler);

    // `party` takes the next word as the leader unless that word is already the verb.
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" && !IsVerb(Lower(args.w[1]));
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, sender, leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "order", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "order", "missing-verb", "follow, stay, come, attack, pull, endpull, hold, free, assist, co, nc");
    std::string verb = Lower(args.w[pos++]);
    if (verb == "end")
    {
        if (pos < args.w.size() && Lower(args.w[pos]) == "pull")
            ++pos;
        verb = "endpull";
    }
    if (!IsVerb(verb))
        return Fail(handler, args, "order", "unknown-verb", verb);
    std::string const rest = JoinFrom(args.w, pos);

    if ((verb == "attack" || verb == "pull" || verb == "assist" || verb == "co" || verb == "nc") && rest.empty())
        return Fail(handler, args, "order", "missing-argument", verb == "assist" ? "assist <member name>" :
            (verb == "co" || verb == "nc") ? "co|nc <+strategy,-strategy>" : "attack|pull <my | mark | skull | spawn:<id> | 32-hex guid>");
    if ((verb == "stay" || verb == "come" || verb == "hold" || verb == "free" || verb == "endpull") && !rest.empty())
        return Fail(handler, args, "order", "unexpected-argument", rest);
    if (verb == "co" || verb == "nc")
        for (char c : rest)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '+' || c == '-' || c == ',' || c == '_'))
                return Fail(handler, args, "order", "bad-argument", "strategy changes use letters, digits, space, + - , _");

    uint32 queued = 0, refused = 0;
    // One bot: the answer is one line. Several: `order-begin`, one line per bot, `order-end` (grammar version 2).
    bool const multi = who.bots.size() > 1;
    Reply reply(handler, args, "order");
    if (multi)
        reply.Begin();
    auto const emit = [&](Rec const& rec) { if (multi) reply.Line(rec); else reply.Single(rec); };
    for (Player* bot : who.bots)
    {
        auto const refuse = [&](std::string const& code, std::string const& detail, char const* name = nullptr)
        {
            ++refused;
            Rec rec("order");
            rec.S("bot", bot->GetName()).S("verb", verb).S("status", "refused").S("code", code);
            if (name)
                rec.S("name", name);
            rec.S("msg", std::string_view(detail).substr(0, 400));
            emit(rec);
        };

        PlayerbotAI* ai = AiOf(bot);
        if (!ai || !ai->GetAiObjectContext())
        {
            refuse("no-engine", "the bot has no running engine (`.tb engine <bot> on`)");
            continue;
        }

        Player* commander = CommanderOf(bot);
        Order order;
        order.verb = verb;
        order.owner = commander ? commander->GetGUID() : bot->GetGUID();
        order.arg = rest;
        char const* needAction[2] = { nullptr, nullptr };
        std::string code, detail;

        if (verb == "follow")
        {
            needAction[0] = "follow chat shortcut";
            if (!rest.empty())
            {
                order.arg = Lower(rest);
                needAction[1] = "follow target";
                if (order.arg != "none")
                {
                    Player* member = FindOnline(rest);
                    Group* group = bot->GetGroup();
                    if (!member || !group || !group->IsMember(member->GetGUID()) || member == bot)
                    {
                        refuse("not-in-group", rest + " is not another member of the bot's party");
                        continue;
                    }
                }
            }
        }
        else if (verb == "stay")
            needAction[0] = "stay chat shortcut";
        else if (verb == "come")
        {
            // The spot: where the sender stands (a GM's character), or the bound commander from the console.
            Player* spot = sender ? sender : commander;
            if (!spot)
            {
                refuse("no-commander", "no sender and no bound commander to come to (`.tb master`)");
                continue;
            }
            if (spot->GetMapId() != bot->GetMapId() || spot->GetInstanceId() != bot->GetInstanceId())
            {
                refuse("other-map", "the bot is not on the map of " + spot->GetName());
                continue;
            }
            if (bot->GetDistance(spot) > sPlayerbotAIConfig.reactDistance)
            {
                refuse("too-far", "the spot is further than the engine's react distance (" + std::to_string(int(sPlayerbotAIConfig.reactDistance)) +
                    " yd): the stay position would be dropped");
                continue;
            }
            order.x = spot->GetPositionX();
            order.y = spot->GetPositionY();
            order.z = spot->GetPositionZ();
            order.map = spot->GetMapId();
        }
        else if (verb == "attack" || verb == "pull")
        {
            needAction[0] = verb == "attack" ? "attack rti target" : "pull my target";
            if (!ResolveTarget(bot, sender, rest, order.unit, code, detail))
            {
                refuse(code, detail);
                continue;
            }
            if (verb == "attack" && !bot->GetGroup())
            {
                refuse("no-party", "attack uses the raid mark of the bot's party; the bot is in no party");
                continue;
            }
            if (verb == "pull" && !ai->HasStrategy("pull", BotState::BOT_STATE_COMBAT))
            {
                refuse("no-pull-strategy", "the bot has no `pull` combat strategy (give it the tank role)");
                continue;
            }
        }
        else if (verb == "assist")
        {
            needAction[0] = "attack rti target";
            Player* member = FindOnline(rest);
            Group* group = bot->GetGroup();
            if (!member || !group || !group->IsMember(member->GetGUID()) || member == bot)
            {
                refuse("not-in-group", rest + " is not another member of the bot's party");
                continue;
            }
            Unit* victim = member->GetVictim();
            if (!victim)
                victim = ObjectAccessor::GetUnit(*bot, member->GetTarget());
            if (!victim || !victim->IsAlive() || !bot->IsValidAttackTarget(victim))
            {
                refuse("nothing-to-assist", member->GetName() + " has no attackable victim or target now");
                continue;
            }
            order.who = member->GetGUID();
        }
        else if (verb == "endpull")
            needAction[0] = "end pull";

        bool missing = false;
        for (char const* key : needAction)
            if (key && !ActionRegistered(ai, key))
            {
                refuse("action-not-registered", std::string("the legacy action `") + key + "` is not registered in this build", key);
                missing = true;
                break;
            }
        if (missing)
            continue;

        uint32 seq = 0;
        if (!Enqueue(bot, std::move(order), code, detail, seq))
        {
            refuse(code, detail);
            continue;
        }
        ++queued;
        Rec rec("order");
        rec.S("bot", bot->GetName()).S("verb", verb).N("seq", seq).S("status", "queued");
        emit(rec);
    }
    if (multi)
    {
        Rec end("order");
        end.S("verb", verb).P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("queued", queued).N("refused", refused);
        reply.End(end);
    }
    return true;
}

// ---- `.tb role <bot|party [leader]> tank|healer|dps|none [#token]` ----

bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "role"))
        return false;
    bool const leaderFollows = args.w.size() >= 3 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "role", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "role", "missing-role", "tank, healer (heal), dps or none");
    std::string const word = Lower(args.w[pos]);
    uint8 role;
    if (word == "tank")
        role = RoleTank;
    else if (word == "healer" || word == "heal")
        role = RoleHealer;
    else if (word == "dps")
        role = RoleDps;
    else if (word == "none" || word == "clear")
        role = 0;
    else
        return Fail(handler, args, "role", "bad-role", word);

    char const* const roleName = role == RoleTank ? "tank" : role == RoleHealer ? "healer" : role == RoleDps ? "dps" : "none";
    uint32 queued = 0;
    bool const multi = who.bots.size() > 1;
    Reply reply(handler, args, "role");
    if (multi)
        reply.Begin();
    auto const emit = [&](Rec const& rec) { if (multi) reply.Line(rec); else reply.Single(rec); };
    for (Player* bot : who.bots)
    {
        // The stored role (what PlayerbotAI::IsTank/IsHeal and AiFactory::GetPlayerRoles read first, whatever the master is) is written now; the AI's own
        // forced role and the kit rebuild run in the bot's next engine tick, or at the first tick of an engine that is not running yet.
        PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), role);
        if (role)
            Roles()[bot->GetGUID()] = role;
        else
            Roles().erase(bot->GetGUID());

        Rec rec("role");
        rec.S("bot", bot->GetName()).S("role", roleName);
        PlayerbotAI* ai = AiOf(bot);
        if (!ai)
        {
            rec.S("status", "stored");
            emit(rec);
            continue;
        }
        Order order;
        order.verb = "role";
        order.arg = std::to_string(unsigned(role));
        Player* commander = CommanderOf(bot);
        order.owner = commander ? commander->GetGUID() : bot->GetGUID();
        std::string code, detail;
        uint32 seq = 0;
        if (!Enqueue(bot, std::move(order), code, detail, seq))
        {
            rec.S("status", "refused").S("code", code).S("msg", std::string_view(detail).substr(0, 400));
            emit(rec);
            continue;
        }
        ++queued;
        rec.N("seq", seq).S("status", "queued");
        emit(rec);
    }
    if (multi)
    {
        Rec end("role");
        end.S("role", roleName).P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("queued", queued);
        reply.End(end);
    }
    return true;
}

// ---- `.tb master <bot|party [leader]> <commander|none> [#token]` ----

bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "master"))
        return false;
    // `party <leader> <commander>` has three words, `party <commander>` two (the one bot party, or the bots already bound to a commander).
    bool const leaderFollows = args.w.size() >= 3 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "master", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "master", "missing-commander", "name the commander, or `none` to release the bots");
    std::string const name = args.w[pos];
    bool const release = Lower(name) == "none";
    Player* commander = release ? nullptr : FindOnline(name);
    if (!release && !commander)
        return Fail(handler, args, "master", "no-such-player", name + " is not in the world");
    for (Player* bot : who.bots)
        if (bot == commander)
            return Fail(handler, args, "master", "self-master", "a bot cannot command itself");

    uint32 done = 0, queued = 0;
    bool const multi = who.bots.size() > 1;
    Reply reply(handler, args, "master");
    if (multi)
        reply.Begin();
    auto const emit = [&](Rec const& rec) { if (multi) reply.Line(rec); else reply.Single(rec); };
    for (Player* bot : who.bots)
    {
        ObjectGuid const guid = bot->GetGUID();
        Rec rec("master");
        rec.S("bot", bot->GetName());
        if (release)
        {
            TortoiseBots::BotManager::Instance().ClearBotMaster(guid);
            SetCommander(guid, ObjectGuid::Empty);
            rec.S("commander", "");
        }
        else
        {
            // What the engine's own hire flow does with a master (BotManager::BindBotMaster: the host's master guid and the engine's pointer), and the
            // binding `IsInCommandedGroup` answers from. The party is not touched: the commander stays outside it (spec D2).
            TortoiseBots::BotManager::Instance().BindBotMaster(guid, commander->GetGUID());
            SetCommander(guid, commander->GetGUID());
            Group* group = bot->GetGroup();
            rec.S("commander", commander->GetName()).B("commander_in_party", group && group->IsMember(commander->GetGUID()));
        }
        ++done;
        // The kit depends on the binding (the commanded threat back-off, AiFactory.cpp:633-642): a running engine rebuilds it at its next tick.
        if (PlayerbotAI* ai = AiOf(bot))
        {
            Order order;
            order.verb = "rekit";
            order.owner = guid;
            std::string code, detail;
            uint32 seq = 0;
            if (Enqueue(bot, std::move(order), code, detail, seq))
            {
                ++queued;
                rec.N("seq", seq).S("status", "queued");
            }
            else
                rec.S("status", "bound").S("rekit", "refused").S("code", code);
        }
        else
            rec.S("status", "bound");
        emit(rec);
    }
    if (multi)
    {
        Rec end("master");
        end.P("target", LabelParts(who.label)).S("commander", release ? std::string() : std::string(commander->GetName())).N("bots", done).N("queued", queued);
        reply.End(end);
    }
    return true;
}
}
}

#else   // not built with the slice: every command says so

namespace TortoiseBots
{
namespace h3
{
bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "order");
}

bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "role");
}

bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "master");
}
}
}

#endif
