#include "host/TbOnyxia.h"
#include "host/TbOnyxiaGround.h"
#include "host/TbH3.h"
#include "host/TbOnyxiaPolicy.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "Chat.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "Log.h"

#include <map>

namespace TortoiseBots::onyxia
{
namespace
{
struct Watch
{
    ObjectGuid boss;
    uint32 instance = 0;
    Encounter encounter;
    uint32 reportMs = 0;
};
std::map<ObjectGuid, Watch> watches;

Creature* Boss(Player* bot, ObjectGuid guid)
{
    Creature* boss = ObjectAccessor::GetCreature(*bot, guid);
    return boss && bot->GetMapId() == 249 && boss->GetEntry() == 10184 &&
        boss->GetMap() == bot->GetMap() ? boss : nullptr;
}

h3::Rec Describe(Player* bot, Watch const& watch)
{
    h3::Rec row("onyxia");
    row.S("bot", bot->GetName()).S("boss", h3::GuidText(watch.boss))
        .N("map", bot->GetMapId()).N("inst", watch.instance)
        .S("phase", PhaseName(watch.encounter.phase)).N("elapsed_ms", watch.encounter.elapsedMs)
        .B("alive", bot->IsAlive()).S("owner", h3::GuidText(GroundOwner(bot->GetGUID())))
        .S("mode", GroundOwner(bot->GetGUID()).IsEmpty() ? "observe" : "ground");
    if (Creature* boss = Boss(bot, watch.boss))
    {
        Spell* cast = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        row.F("hp", boss->GetHealthPct()).S("victim", h3::GuidText(boss->GetVictim() ? boss->GetVictim()->GetGUID() : ObjectGuid::Empty))
            .F("threat", boss->GetThreatManager().GetThreat(bot)).N("cast", cast ? cast->GetSpellInfo()->Id : 0)
            .B("hover", boss->HasAura(17131)).B("flight", boss->IsFlying())
            .Coord("pos", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }
    return row;
}
}

void Forget(ObjectGuid bot) { ForgetGround(bot); watches.erase(bot); }

void Tick(Player* bot, PlayerbotAI* ai, uint32 diff)
{
    // Ground control is a host seam of its own; a future controller need not
    // create a telemetry subscription merely to keep its configured state alive.
    GroundTick(bot, ai);
    auto it = watches.find(bot->GetGUID());
    if (it == watches.end())
        return;
    Watch& watch = it->second;
    Creature* boss = Boss(bot, watch.boss);
    if (!boss || bot->GetInstanceId() != watch.instance)
    {
        TC_LOG_INFO("tortoisebots", "Onyxia observer cleared bot={} reason=map-or-boss-lost", bot->GetName());
        ForgetGround(bot->GetGUID());
        watches.erase(it);
        return;
    }
    bool const changed = watch.encounter.Update({true, boss->IsAlive(), boss->IsInCombat(),
        boss->HasAura(17131) || boss->IsFlying(), boss->GetHealthPct(), 249, watch.instance, boss->GetEntry()}, diff);
    watch.reportMs += diff;
    if (changed || watch.reportMs >= 1000)
    {
        watch.reportMs = 0;
        Spell* cast = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        TC_LOG_INFO("tortoisebots", "Onyxia observe bot={} inst={} phase={} elapsed_ms={} hp={} victim={} alive={} hover={} flight={} cast={} threat={}",
            bot->GetName(), watch.instance, PhaseName(watch.encounter.phase), watch.encounter.elapsedMs,
            boss->GetHealthPct(), boss->GetVictim() ? boss->GetVictim()->GetGUID().ToString() : "none",
            bot->IsAlive(), boss->HasAura(17131), boss->IsFlying(), cast ? cast->GetSpellInfo()->Id : 0,
            boss->GetThreatManager().GetThreat(bot));
    }
}

bool HandleCommand(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    using namespace h3;
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "onyxia"))
        return false;
    if (args.badToken || args.w.size() < 2 || args.w.size() > 4)
        return Fail(handler, args, "onyxia", "usage", ".tb onyxia <bot> observe <boss-guid> | ground <boss-guid> <tank> | status | off");
    size_t position = 0;
    if (args.w[0] == "party")
        return Fail(handler, args, "onyxia", "single-bot", "name one host bot");
    Address address = ResolveAddress(args, position, SenderPlayer(handler), false);
    if (!address.ok())
        return Fail(handler, args, "onyxia", address.code, address.detail);
    if (address.bots.size() != 1)
        return Fail(handler, args, "onyxia", "single-bot");
    Player* bot = address.bots.front();
    std::string const& mode = args.w[1];
    if ((mode == "observe" && args.w.size() == 3) || (mode == "ground" && args.w.size() == 4))
    {
        ObjectGuid guid;
        if (!ParseGuidText(args.w[2], guid) || !Boss(bot, guid))
            return Fail(handler, args, "onyxia", "wrong-boss", "expected Onyxia 10184 in this bot's map 249 instance");
        if (mode == "ground")
        {
            std::string const error = ConfigureGround(bot, guid, FindOnline(args.w[3]));
            if (!error.empty())
                return Fail(handler, args, "onyxia", error, "ground mode needs a running engine and a living host tank of this group and instance");
        }
        else
            ForgetGround(bot->GetGUID());
        watches[bot->GetGUID()] = {guid, bot->GetInstanceId(), {}, 0};
    }
    else if (mode == "off" && args.w.size() == 2)
        Forget(bot->GetGUID());
    else if (mode != "status" || args.w.size() != 2)
        return Fail(handler, args, "onyxia", "usage", ".tb onyxia <bot> observe <boss-guid> | ground <boss-guid> <tank> | status | off");
    Reply reply(handler, args, "onyxia");
    reply.Begin();
    auto it = watches.find(bot->GetGUID());
    if (it != watches.end())
        reply.Line(Describe(bot, it->second));
    else
        reply.Line(Rec("onyxia").S("bot", bot->GetName()).S("mode", "off"));
    reply.End(Rec("onyxia"));
    return true;
}
}
#else
#include "Chat.h"
namespace TortoiseBots::onyxia
{
bool HandleCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    return h3::HandleOnyxiaOff(handler, args);
}
void Tick(Player*, PlayerbotAI*, uint32) { }
void Forget(ObjectGuid) { }
}
#endif
