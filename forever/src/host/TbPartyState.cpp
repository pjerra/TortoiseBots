// Slice 3, host package H3: what the bots are doing, as an interface (`.tb state`, `.tb party`, `.tb threat`) and the party forming
// (`.tb party <leader> <member>...`). The line grammar is in forever/docs/TB-COMMANDS.md; every reply goes to the sender of the command
// through the core's ChatHandler (TbH3.h, Say).
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include "Config.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ThreatManager.h"
#include "Unit.h"

#include "TbBotSession.h"
#include "compat/TbCompat_Unit.h"
#include "compat/TbCompat_V.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace h3
{
namespace
{
char const* ClassName(uint8 classId)
{
    static char const* const names[] = { "none", "warrior", "paladin", "hunter", "rogue", "priest", "deathknight", "shaman", "mage", "warlock", "monk", "druid", "demonhunter" };
    return classId < sizeof(names) / sizeof(names[0]) ? names[classId] : "unknown";
}

std::string PowerName(Powers power)
{
    switch (power)
    {
        case POWER_MANA: return "mana";
        case POWER_RAGE: return "rage";
        case POWER_FOCUS: return "focus";
        case POWER_ENERGY: return "energy";
        default: return "power" + std::to_string(int(power));
    }
}

std::string Fixed(float value, int decimals = 1)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::vector<std::string> Names(PlayerbotAI* ai, BotState state)
{
    std::vector<std::string> names;
    for (std::string_view name : ai->GetStrategies(state))
        names.emplace_back(name);
    return names;
}

std::string RoleOf(Player* bot, PlayerbotAI* ai)
{
    if (ai ? PlayerbotAI::IsTank(bot, true) : PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot->GetGUID()) == 1)
        return "tank";
    if (ai ? PlayerbotAI::IsHeal(bot, true) : PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot->GetGUID()) == 2)
        return "healer";
    return ai || PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot->GetGUID()) ? "dps" : "none";
}

std::string DistanceTo(Player* bot, Player* other)
{
    if (!other)
        return {};
    if (other == bot)
        return "self";
    if (other->GetMapId() != bot->GetMapId() || other->GetInstanceId() != bot->GetInstanceId())
        return "othermap";
    return Fixed(bot->GetDistance(other));
}

uint32 PercentOf(uint64 value, uint64 max)
{
    return max ? uint32(std::min<uint64>(100, value * 100 / max)) : 0;
}

// What `.tb state` says of one bot's readiness for the next pull: ready = alive, out of combat, not resting, within follow range of its master, not blocked.
struct Readiness
{
    bool rest = false;                    // the old rest gate (health, mana, eating, drinking, combat, dead)
    std::vector<std::string> why;         // reasons this bot is not ready: dead combat resting far blocked
};

// One state record of one bot (grammar version 2: forever/docs/TB-COMMANDS.md). `ready` says what keeps the bot from the next pull.
Rec StateRec(Player* bot, Readiness& ready)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    ObjectGuid const guid = bot->GetGUID();

    uint32 const hp = uint32(bot->GetHealth());
    uint32 const hpMax = uint32(bot->GetMaxHealth());
    Powers const power = bot->GetPowerType();
    int32 powerValue = bot->GetPower(power);
    int32 powerMax = bot->GetMaxPower(power);
    if (power == POWER_RAGE)       // the core keeps rage times ten; the line shows rage points
    {
        powerValue /= 10;
        powerMax /= 10;
    }
    bool const usesMana = power == POWER_MANA && powerMax > 0;
    uint32 const manaPct = usesMana ? PercentOf(uint64(std::max(powerValue, 0)), uint64(powerMax)) : 0;

    std::string state = !bot->IsAlive() ? "dead" : bot->IsInCombat() ? "combat" : "non-combat";
    if (ai)
        state = ai->GetState() == BotState::BOT_STATE_DEAD ? "dead" : ai->GetState() == BotState::BOT_STATE_COMBAT ? "combat" : "non-combat";

    Unit* target = nullptr;
    if (context)
        if (ai::Value<Unit*>* value = context->GetValue<Unit*>("current target"))
            target = value->Get();
    Unit* victim = bot->GetVictim();

    Player* master = ai ? ai->GetMaster() : CommanderOf(bot);
    Group* group = bot->GetGroup();
    Player* leader = group ? ObjectAccessor::FindPlayer(group->GetLeaderGUID()) : nullptr;

    bool away = false, blocked = false, hasBrain = false;
    uint32 awaySeconds = 0;
    std::string blockedWhy;
    BotMgr::Instance().FollowStatus(guid, away, awaySeconds, blocked, blockedWhy, hasBrain);

    OrderStatus const order = LastOrder(guid);
    std::string orderResult = order.result == "none" ? std::string() : order.result;   // no order yet: an empty result, never the word `none`

    bool eating = false, drinking = false;
    if (ai)
    {
        eating = ai->HasSeatedMeal(false);
        drinking = ai->HasSeatedMeal(true);
    }
    int32 const restHealthPct = std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Slice.RestHealthPct", 90), 0, 100);
    int32 const restManaPct = std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Slice.RestManaPct", 80), 0, 100);
    std::vector<std::string> restWhy;
    if (!bot->IsAlive())
        restWhy.push_back("dead");
    if (bot->IsInCombat())
        restWhy.push_back("combat");
    if (bot->IsAlive() && uint64(hp) * 100 < uint64(restHealthPct) * hpMax)
        restWhy.push_back("hp");
    if (bot->IsAlive() && usesMana && int32(manaPct) < restManaPct)
        restWhy.push_back("mana");
    if (eating)
        restWhy.push_back("eating");
    if (drinking)
        restWhy.push_back("drinking");
    ready.rest = !restWhy.empty();

    // Readiness for the next pull: alive, out of combat, not resting, within follow range of the master (10 yd, 18 for a healer), not blocked.
    ready.why.clear();
    if (!bot->IsAlive())
        ready.why.push_back("dead");
    if (bot->IsInCombat())
        ready.why.push_back("combat");
    if (ready.rest && bot->IsAlive() && !bot->IsInCombat())
        ready.why.push_back("resting");
    if (master && master != bot && bot->IsAlive())
    {
        bool const sameMap = master->GetMapId() == bot->GetMapId() && master->GetInstanceId() == bot->GetInstanceId();
        float const limit = RoleOf(bot, ai) == "healer" ? 18.0f : 10.0f;
        if (!sameMap || bot->GetDistance(master) > limit)
            ready.why.push_back("far");
    }
    if (away || blocked)
        ready.why.push_back("blocked");

    Rec rec("state");
    rec.S("bot", bot->GetName());
    rec.S("role", RoleOf(bot, ai));
    rec.S("rolesrc", PlayerbotAIStorage::Instance().GetPlayerForcedRole(guid) ? "forced" : "auto");
    rec.S("class", ClassName(bot->GetClass()));
    rec.N("level", bot->GetLevel());
    rec.N("hp", hp).N("hpmax", hpMax).N("hppct", PercentOf(hp, hpMax));
    rec.S("power", PowerName(power)).N("pw", powerValue).N("pwmax", powerMax);
    if (usesMana)
        rec.N("manapct", manaPct);
    rec.S("st", state);
    rec.B("incombat", bot->IsInCombat());
    rec.S("tgt", target ? GuidText(target->GetGUID()) : std::string());
    rec.S("tgtentry", target ? std::to_string(target->GetEntry()) : std::string());
    rec.S("tgthp", target ? std::to_string(PercentOf(uint64(target->GetHealth()), uint64(target->GetMaxHealth()))) : std::string());
    rec.S("victim", victim ? GuidText(victim->GetGUID()) : std::string());
    rec.B("hold", ai && ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT));
    rec.S("mv", !ai ? "" : ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ? "follow" : ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ? "stay" : "");
    if (away)
        rec.P("stuck", {"away", std::to_string(awaySeconds)});
    else if (blocked)
        rec.P("stuck", {"blocked", blockedWhy});
    else
        rec.S("stuck", !ai ? "noengine" : "");
    rec.B("engine", ai != nullptr);
    rec.Coord("pos", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    rec.N("map", bot->GetMapId()).N("inst", bot->GetInstanceId());
    rec.S("dl", DistanceTo(bot, leader)).S("dm", DistanceTo(bot, master));
    if (order.seq)
        rec.P("ord", {std::to_string(order.seq), orderResult});
    else
        rec.S("ord", "");                                    // no order yet: no value (addendum A6), never `0:none`
    rec.S("lastorder", order.verb == "none" ? std::string() : order.verb);
    if (ai)
        rec.L("co", Names(ai, BotState::BOT_STATE_COMBAT)).L("nc", Names(ai, BotState::BOT_STATE_NON_COMBAT));
    if (auto* session = dynamic_cast<BotSession*>(bot->GetSession()))
    {
        CastResultCounts const counts = session->GetCastResultCounts();
        rec.N("cast_queued", counts.queued).N("cast_delivered", counts.delivered).N("cast_dropped", counts.dropped).N("cast_pending", counts.pending);
    }
    rec.B("eating", eating).B("drinking", drinking);
    rec.B("resting", ready.rest).L("rest_why", restWhy);
    return rec;
}

std::string NameOf(Unit const* unit)
{
    return unit ? std::string(unit->GetName()) : std::string();
}

}

// ---- `.tb state <bot|party [leader]> [#token]` ----

bool HandleState(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "state"))
        return false;
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "state", who.code, who.detail);
    if (pos < args.w.size())
        return Fail(handler, args, "state", "unexpected-argument", args.w[pos]);

    Reply reply(handler, args, "state");
    reply.Begin();
    uint32 resting = 0;
    std::vector<std::vector<std::string>> notReady;
    for (Player* bot : who.bots)
    {
        Readiness ready;
        reply.Line(StateRec(bot, ready));
        if (ready.rest)
            ++resting;
        for (std::string const& reason : ready.why)
            notReady.push_back({reason, bot->GetName()});
    }
    // How many of the first bot's group's eight raid marks are set (an `attack` order puts one on and must leave none behind).
    uint32 marks = 0;
    if (!who.bots.empty() && who.bots.front()->GetGroup())
        for (uint8 i = 0; i < 8; ++i)
            if (!who.bots.front()->GetGroup()->GetTargetIcon(i).IsEmpty())
                ++marks;
    Rec end("state");
    end.N("bots", who.bots.size()).N("resting", resting).B("ready", notReady.empty());
    if (!notReady.empty())
        end.LP("why", notReady);
    end.N("marks", marks).P("target", LabelParts(who.label));
    reply.End(end);
    return true;
}

// ---- `.tb threat <bot|party [leader]> [#token]` ----

bool HandleThreat(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "threat"))
        return false;
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "threat", who.code, who.detail);
    if (pos < args.w.size())
        return Fail(handler, args, "threat", "unexpected-argument", args.w[pos]);

    // The mobs that have one of the bots on their threat list (the walk the engine's own value uses, ForEachThreatenerCompat), each once.
    std::vector<Unit*> mobs;
    for (Player* bot : who.bots)
        ForEachThreatenerCompat(bot, [&mobs](Unit* attacker)
        {
            if (std::find(mobs.begin(), mobs.end(), attacker) == mobs.end())
                mobs.push_back(attacker);
            return false;
        });

    Reply reply(handler, args, "threat");
    reply.Begin();
    for (Unit* mob : mobs)
    {
        // READ ONLY: GetSortedThreatList and the last victim (CurrentThreatVictimCompat). Nothing that adds, scales, taunts, resets or re-selects.
        std::vector<std::pair<float, Unit*>> list;
        for (ThreatReference const* ref : mob->GetThreatManager().GetSortedThreatList())
            if (ref && ref->GetVictim())
                list.emplace_back(ref->GetThreat(), ref->GetVictim());
        std::stable_sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        std::vector<std::vector<std::string>> top;
        for (size_t i = 0; i < list.size() && i < 3; ++i)
            top.push_back({list[i].second->GetName(), Fixed(list[i].first, 0)});
        Rec rec("threat");
        rec.S("mob", GuidText(mob->GetGUID())).N("entry", mob->GetEntry()).S("name", mob->GetName());
        rec.N("hppct", PercentOf(uint64(mob->GetHealth()), uint64(mob->GetMaxHealth())));
        rec.S("victim", NameOf(CurrentThreatVictimCompat(mob))).S("target", NameOf(mob->GetVictim()));
        rec.N("size", list.size()).LP("top", top);
        reply.Line(rec);
    }
    Rec end("threat");
    end.N("mobs", mobs.size()).N("bots", who.bots.size()).P("target", LabelParts(who.label));
    reply.End(end);
    return true;
}

// ---- `.tb party <leader>` (the roster) and `.tb party <leader> <member>...` (forms a party) ----

bool HandleParty(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "party"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "party", "missing-leader", ".tb party <leader>  or  .tb party <leader> <member>...");
    Player* leader = FindOnline(args.w[0]);
    if (!leader)
        return Fail(handler, args, "party", "no-such-player", args.w[0] + " is not in the world");

    if (args.w.size() > 1)
    {
        if (args.w.size() > 5)
            return Fail(handler, args, "party", "too-many", "a party holds five: the leader and four members");
        std::set<ObjectGuid> seen;
        seen.insert(leader->GetGUID());
        for (size_t i = 1; i < args.w.size(); ++i)
        {
            Player* member = FindOnline(args.w[i]);
            if (!member)
                return Fail(handler, args, "party", "no-such-player", args.w[i] + " is not in the world");
            if (!IsHostBot(member))
                return Fail(handler, args, "party", "not-a-bot", args.w[i] + " is not a logged-in host bot");
            if (!seen.insert(member->GetGUID()).second)
                return Fail(handler, args, "party", "duplicate", args.w[i]);
        }
        Reply reply(handler, args, "party");
        reply.Begin();
        uint32 joined = 0;
        for (size_t i = 1; i < args.w.size(); ++i)
        {
            Player* member = FindOnline(args.w[i]);
            std::string const error = BotMgr::Instance().GroupWith(member->GetGUID(), leader->GetGUID());
            Rec rec("party-join");
            rec.S("bot", member->GetName()).S("leader", leader->GetName());
            if (error.empty())
            {
                ++joined;
                rec.S("status", "joined");
            }
            else
                rec.S("status", "refused").S("code", "party-refused").S("msg", error.substr(0, 400));
            reply.Line(rec);
        }
        Rec end("party");
        end.S("leader", leader->GetName()).N("joined", joined).N("asked", args.w.size() - 1).S("note", "the host master is the leader: run .tb master next");
        reply.End(end);
        return true;
    }

    Group* group = leader->GetGroup();
    if (!group)
        return Fail(handler, args, "party", "no-party", leader->GetName() + " is not in a party");
    Reply reply(handler, args, "party");
    reply.Begin();
    uint32 bots = 0;
    std::set<std::string> commanders;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        bool const online = member && member->IsInWorld();
        Rec rec("party");
        rec.S("member", slot.name).B("leader", group->IsLeader(slot.guid)).B("online", online);
        if (online)
        {
            bool const bot = IsHostBot(member);
            PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(member) : nullptr;
            Player* commander = bot ? CommanderOf(member) : nullptr;
            bool const bound = bot && !CommanderGuid(member->GetGUID()).IsEmpty();
            rec.B("bot", bot).S("class", ClassName(member->GetClass())).N("level", member->GetLevel());
            rec.S("role", bot ? RoleOf(member, ai) : std::string()).B("engine", ai != nullptr);
            rec.S("master", commander ? std::string(commander->GetName()) : std::string()).B("commanded", bound);
            if (bot)
                ++bots;
            if (bound && commander)
                commanders.insert(commander->GetName());
        }
        reply.Line(rec);
    }
    Rec end("party");
    end.S("leader", group->GetLeaderName()).N("bots", bots).B("raid", group->isRaidGroup());
    end.S("commander", commanders.empty() ? std::string() : commanders.size() == 1 ? *commanders.begin() : std::string("mixed"));
    reply.End(end);
    return true;
}
}
}

#else   // not built with the slice

namespace TortoiseBots
{
namespace h3
{
bool HandleState(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "state");
}

bool HandleThreat(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "threat");
}

bool HandleParty(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "party");
}
}
}

#endif
