// Slice 3, host package H3: what the bots are doing, as an interface (`.tb state`, `.tb party`, `.tb threat`) and the party forming
// (`.tb party <leader> <member>...`). The line grammar is in forever/docs/TB-COMMANDS.md; every reply goes to the sender of the command
// through the core's ChatHandler (TbH3.h, Say).
#include "host/TbH3.h"
#include "host/TbPet.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbCc.h"
#include "host/TbHybrid.h"
#include "host/run/TbRunHooks.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include "Config.h"
#include "Group.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ThreatManager.h"
#include "Unit.h"

#include "TbBotSession.h"
#include "compat/TbCompat_Unit.h"
#include "compat/TbCompat_V.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
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
    std::string life;                     // H4: alive | dead | ghost | awaiting-res | running-back (the `life` key)
    bool together = false;                // H4: alive, and on its leader's map within 40 yd (or it is the leader): part of `whole=yes`
};

// H4 (the crew rule the lease answer asks of RunReleaseHold::Decide, runtime/RunReleaseHold.h:57-64): is a living priest, paladin, shaman or druid of the bot's
// party on its map, and is a living member of the party in combat? Read-only; the same two questions AutoReleaseSpiritAction asks (ReleaseSpiritAction.h:64-98).
void CrewFacts(Player* bot, bool& rezzer, bool& fighting)
{
    rezzer = fighting = false;
    Group* group = bot->GetGroup();
    if (!group)
        return;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || member == bot || !member->IsInWorld() || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;
        switch (member->GetClass())
        {
            case CLASS_PRIEST: case CLASS_PALADIN: case CLASS_SHAMAN: case CLASS_DRUID: if (MemberKnowsOocResurrection(member)) rezzer = true; break;
            default: break;
        }
        if (member->IsInCombat())
            fighting = true;
    }
}

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

    // ---- H4 (slice 4): life, corpse, where the bot stands against its leader's instance, the last entry (appended: the older keys are unchanged) ----
    // life: alive | dead (a body that will release) | awaiting-res (a body held by the crew rule or by a pending resurrection) | ghost | running-back
    // (alive again, more than 30 yd from its commander or leader, until it is within 20 yd). relhold says why a body waits: rezzer (a living resurrecter on
    // the map), combat (a member fights), wipe (nobody left: it releases now), no-entrance (the instance has none: the release is held, never a graveyard
    // outside), empty (not a body on an instance map, or alive).
    bool const alive = bot->IsAlive();
    bool const ghost = !alive && bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    std::string relhold;                  // empty = not a body on an instance map (the grammar never writes the word none)
    std::string life = "alive";
    if (!alive)
    {
        if (ghost)
            life = "ghost";
        else
        {
            life = "dead";
            Map* map = bot->GetMap();
            if (map && map->IsDungeon() && IsCommandedBot(bot))
            {
                bool rezzer = false, fighting = false;
                CrewFacts(bot, rezzer, fighting);
                if (!BotMgr::Instance().ReleaseHasEntrance(bot))
                    relhold = "no-entrance";
                else if (rezzer)
                    relhold = "rezzer";
                else if (fighting)
                    relhold = "combat";
                else
                    relhold = "wipe";
                if (relhold != "wipe")
                    life = "awaiting-res";
            }
            if (life == "dead" && bot->IsResurrectRequested())
                life = "awaiting-res";
        }
    }
    else if (BotMgr::Instance().RunningBack(guid))
        life = "running-back";
    ready.life = life;
    Player* offerer = ResurrectOffererOf(bot);
    rec.S("life", life).S("relhold", relhold).S("resreq", offerer ? std::string(offerer->GetName()) : std::string());
    if (!alive)
    {
        // The body (not yet released) lies where the bot is; a released ghost has a corpse, which may be on another map.
        if (bot->HasCorpse())
        {
            auto const& corpse = bot->GetCorpseLocation();   // (WorldLocation is a legacy alias in this file: take the core type by auto)
            rec.Coord("corpse", corpse.GetPositionX(), corpse.GetPositionY(), corpse.GetPositionZ()).N("corpsemap", corpse.GetMapId());
        }
        else
            rec.Coord("corpse", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()).N("corpsemap", bot->GetMapId());
    }
    // outside=yes: the party's leader is inside an instance and the bot is not in that instance.
    bool const outside = leader && leader != bot && leader->IsInWorld() && leader->GetMap() && leader->GetMap()->Instanceable() && leader->GetMap() != bot->GetMap();
    rec.B("outside", outside);
    ready.together = alive && (!leader || leader == bot || (leader->GetMap() == bot->GetMap() && bot->GetDistance(leader) <= 40.0f));
    BotMgr::EntryInfo const entry = BotMgr::Instance().GetEntryInfo(guid);
    rec.S("entered", entry.entered).S("entry", entry.phase).S("block", entry.block);
    if (!entry.blockText.empty())
        rec.T("blockmsg", entry.blockText);
    rec.N("catchup", entry.catchups).B("practice", BotMgr::Instance().Practice(guid));
    h8::AppendCcState(rec, bot);                              // H8 (slice 8): rticc, ccfam, held, combo (appended: the older keys are unchanged)
    h10::AppendHybridState(rec, bot);                         // H10 (slice 10): form, totems, imbue, seal, paura (appended: the older keys are unchanged)
    p11::AppendPetState(rec, bot);                            // P11 (slice 11): pet=, petreact=, petauto=, loyal=, ammo=, shards= (a hunter or a warlock)
    // S9 (C9): the free slots of the bot's bags and its money (copper): a run starts with at least 12 free slots, and the loot code leaves an item that would need
    // a new slot when two or fewer are free.
    rec.N("bagfree", bot->GetFreeInventorySlotCount()).N("money", bot->GetMoney());
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());
    rec.S("spec", sRandomItemMgr.GetSpecName(specId));
    std::vector<std::vector<std::string>> gear;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            gear.push_back({std::to_string(slot), std::to_string(item->GetEntry())});
    rec.LP("gear", gear);
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
    uint32 resting = 0, aliveCount = 0, deadCount = 0, ghostCount = 0, togetherCount = 0;
    std::vector<std::vector<std::string>> notReady;
    for (Player* bot : who.bots)
    {
        Readiness ready;
        reply.Line(StateRec(bot, ready));
        if (ready.rest)
            ++resting;
        if (bot->IsAlive())
            ++aliveCount;
        else if (ready.life == "ghost")
            ++ghostCount;
        else
            ++deadCount;
        if (ready.together)
            ++togetherCount;
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
    // H4: how many of the addressed bots live, wait as a body or run as a ghost; wipe = nobody is alive; whole = every addressed bot is alive and with its
    // leader (same map, within 40 yd).
    uint32 const total = uint32(who.bots.size());
    end.N("alive", aliveCount).N("dead", deadCount).N("ghost", ghostCount).B("wipe", total > 0 && aliveCount == 0).B("whole", total > 0 && togetherCount == total);
    // S9 (C9): the party's loot method and threshold, as the policy names them (group | ffa | rr | master | nbg, or none for a method it does not know:
    // the core's personal loot) and the quality number; absent for a party with no group.
    if (!who.bots.empty() && who.bots.front()->GetGroup())
    {
        Group const* lootGroup = who.bots.front()->GetGroup();
        end.S("lootmethod", run::LootMethodWord(lootGroup)).N("lootthr", uint32(lootGroup->GetLootThreshold()));
    }
    run::AnnotateStateEnd(end, who.bots.empty() ? nullptr : who.bots.front());     // S5 (H5): run=<lead> runstate=<state> for a party in a run
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
        // H4 (Q2): where the mob stands and whether it flees (the fear-state of a 15 % flee, or the walk to the nearest helper that DoFleeToGetAssistance
        // starts: Creature.cpp:1050-1072). Read only.
        bool const fleeing = mob->HasUnitState(UNIT_STATE_FLEEING) || mob->GetMotionMaster()->GetCurrentMovementGeneratorType() == ASSISTANCE_MOTION_TYPE;
        rec.Coord("pos", mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ()).B("flee", fleeing);
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
        // N5: bots join only a bot's party or the sender's own character's party, never a real player's.
        if (!MayLeadBots(handler, leader->GetGUID()))
            return Fail(handler, args, "party", "not-allowed", args.w[0] + " is neither a host bot nor your own character: bots do not join a real player's party");
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
                rec.S("status", "refused").S("code", "party-refused").T("msg", error);
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

namespace TortoiseBots
{
namespace h3
{
// N3 (package N): does this party member KNOW an out-of-combat resurrection (the legacy action names: priest "resurrection", paladin "redemption", shaman
// "ancestral spirit", druid "revive"; a druid's battle resurrection "rebirth" is not one)? A host bot is asked through its own spell book; a host bot with no
// engine cannot cast, so it does not count; a real player is counted by class (nothing to ask, the legacy rule). Shared with the engine side through
// MemberKnowsOocResurrectionCompat (compat/TbCompat_D4.h), so the engine's hold and `relhold` agree.
bool MemberKnowsOocResurrection(Player* member)
{
    if (!member)
        return false;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(member);
    if (!ai)
        return !IsHostBot(member);
    switch (member->GetClass())
    {
        case CLASS_PRIEST: return ai->HasSpell("resurrection");
        case CLASS_PALADIN: return ai->HasSpell("redemption");
        case CLASS_SHAMAN: return ai->HasSpell("ancestral spirit");
        case CLASS_DRUID: return ai->HasSpell("revive");
        default: return false;
    }
}

namespace
{
char const* UsageName(ai::ItemUsage usage)
{
    switch (usage)
    {
        case ai::ItemUsage::ITEM_USAGE_NONE: return "NONE";
        case ai::ItemUsage::ITEM_USAGE_EQUIP: return "EQUIP";
        case ai::ItemUsage::ITEM_USAGE_BAD_EQUIP: return "BAD_EQUIP";
        case ai::ItemUsage::ITEM_USAGE_BROKEN_EQUIP: return "BROKEN_EQUIP";
        case ai::ItemUsage::ITEM_USAGE_QUEST: return "QUEST";
        case ai::ItemUsage::ITEM_USAGE_SKILL: return "SKILL";
        case ai::ItemUsage::ITEM_USAGE_USE: return "USE";
        case ai::ItemUsage::ITEM_USAGE_GUILD_TASK: return "GUILD_TASK";
        case ai::ItemUsage::ITEM_USAGE_DISENCHANT: return "DISENCHANT";
        case ai::ItemUsage::ITEM_USAGE_AH: return "AH";
        case ai::ItemUsage::ITEM_USAGE_BROKEN_AH: return "BROKEN_AH";
        case ai::ItemUsage::ITEM_USAGE_KEEP: return "KEEP";
        case ai::ItemUsage::ITEM_USAGE_VENDOR: return "VENDOR";
        case ai::ItemUsage::ITEM_USAGE_AMMO: return "AMMO";
        case ai::ItemUsage::ITEM_USAGE_FORCE_NEED: return "FORCE_NEED";
        case ai::ItemUsage::ITEM_USAGE_FORCE_GREED: return "FORCE_GREED";
    }
    return "NONE";
}
}

void AppendItemScanEngine(Player* bot, uint32 itemId, Rec& rec)
{
    ai::ItemQualifier qual(itemId);
    if (PlayerbotAIStorage::Instance().GetAI(bot))
        rec.S("usage", UsageName(ai::ItemUsageValue::QueryItemUsageForEquip(qual, bot)));
    rec.N("w", sRandomItemMgr.ItemStatWeight(bot, qual));
    ItemPrototype const* botProto = tbcompat::ItemProto(itemId);
    if (Item* worn = botProto ? ai::ItemUsageValue::CurrentItemInSlot(botProto, bot) : nullptr)
        rec.N("worn", worn->GetEntry()).N("wornw", sRandomItemMgr.ItemStatWeight(bot, worn));
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
