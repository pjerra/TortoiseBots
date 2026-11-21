// Slice 3, host package H3: what the bots are doing, as an interface (`.tb state`, `.tb party`, `.tb threat`) and the party forming
// (`.tb party <leader> <member>...`). The line grammar is in forever/docs/TB-COMMANDS.md; every reply goes to the sender of the command
// through the core's ChatHandler (TbH3.h, Say).
#include "host/TbH3.h"
#include "host/TbResistGear.h"     // #20: the resist set a bot holds (rgear=)
#include "host/TbRaidConsumables.h"   // #22: cons= / consmiss= (the raid consumables a bot holds and lacks)
#include "host/TbRaidLayout.h"
#include "host/TbRaidGroupsPure.h"
#include "host/TbPet.h"
#include "host/TbHostTaxi.h"
#include "host/TbHostTown.h"
#include "host/gear/TbGearGen.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTransport.h"
#include "host/TbHostAuction.h"
#include "host/TbHostGuildOrders.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbLuaBotsPure.h"   // TBF-B1 finding 6: RoleWord, the role word both `.tb state` and the provider write
#include "host/chat/TbChatIn.h"
#include "host/TbCc.h"
#include "host/TbThrown.h"
#include "host/TbSchool.h"
#include "host/TbHybrid.h"
#include "host/TbTactics.h"
#include "host/run/TbRunHooks.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include "Config.h"
#include "Group.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Corpse.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ThreatManager.h"
#include "Unit.h"

#include "TbBotSession.h"
#include "compat/TbCompat_Unit.h"
#include "compat/TbCompat_P.h"
#include "compat/TbCompat_V.h"
#include "compat/TbCompat_W6.h"
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/policy/death/SafeRevivePolicy.h"
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

std::string RoleOf(Player* bot, PlayerbotAI* /*ai*/)
{
    // TBF-B1 third review finding 6: the word is never empty. IsTank/IsHeal read the forced role, the engine's strategies when there
    // is one, and the bot's talents otherwise (AiFactory::GetPlayerRoles); neither needs a PlayerbotAI object, so the word is right
    // before the brain is built and while it is away (an entry walk, a master away, masterAway), and dps is the fallback.
    return luabots::RoleWord(PlayerbotAI::IsTank(bot, true), PlayerbotAI::IsHeal(bot, true));
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
    BotMgr::LifeInfo const lifeInfo = BotMgr::Instance().GetLifeInfo(guid);

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
    rec.S("mv", !ai ? "" : ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ? "follow" : ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ? "stay" : ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT) ? "guard" : "");
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
        // S2 (appended after the cast-result counters): pushbacks the core reported (SMSG_SPELL_DELAYED) and the session accepted into its queue.
        rec.N("delayed", counts.delayed);
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
    tactics::AppendState(rec, bot);                           // the tactical state keys: stance=, rebuff= (appended after the h8 keys)
    h10::AppendHybridState(rec, bot);                         // H10 (slice 10): form, totems, imbue, seal, paura (appended: the older keys are unchanged)
    p11::AppendPetState(rec, bot);                            // P11 (slice 11): pet=, petreact=, petauto=, loyal=, ammo=, shards= (a hunter or a warlock)
    kb::AppendThrownState(rec, bot);                          // S3: thrown=, falldmg=, runrate= (appended: the older keys are unchanged)
    // #22: the raid consumables this bot holds, `<category>:<aura id|0>;...` (0 for the weapon row, held by the main hand's
    // temporary enchantment), the categories still due for its role, level and the map it is on (`consmiss=`, a `;` list;
    // empty while the feature is off, the map refuses, the bot is dead or in combat), and the slots the table declares
    // uncovered for its role (`consnotcovered=`, so an incomplete role is never read as ready). Read only.
    {
        std::vector<std::vector<std::string>> cons;
        std::uint32_t aura = 0;
        for (std::uint8_t id = 0; id < static_cast<std::uint8_t>(ai::raidcons::Category::Count); ++id)
        {
            ai::raidcons::Category const category = static_cast<ai::raidcons::Category>(id);
            if (TortoiseBots::raidcons::Held(bot, category, aura))
                cons.push_back({ ai::raidcons::CategoryWord(category), std::to_string(aura) });
        }
        rec.LP("cons", cons).L("consmiss", TortoiseBots::raidcons::MissingCategories(bot))
            .L("consnotcovered", TortoiseBots::raidcons::NotCoveredCategories(bot));
    }
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
    // #20: the resistances the worn gear gives (holy, fire, nature, frost, shadow, arcane: the client's school order) and
    // the resist set this module holds on the bot, `<school>:<pieces>` (empty when it holds nothing). Read only.
    std::vector<int64> resist;
    for (uint8 school = SPELL_SCHOOL_HOLY; school <= SPELL_SCHOOL_ARCANE; ++school)
        resist.push_back(bot->GetResistance(static_cast<SpellSchools>(school)));
    rec.Csv("resist", resist);
    int const heldSchool = TortoiseBots::resistgear::HeldSchool(bot);
    if (heldSchool != 0)
        rec.P("rgear", { TortoiseBots::resistgear::SchoolWord(heldSchool), std::to_string(TortoiseBots::resistgear::HeldCount(bot)) });
    else
        rec.P("rgear", {});
    rec.N("mail", tbhost::town::GetUnreadMailCount(bot));

    // ---- W1 (world life): the free flag and what the bot has done since login ----
    rec.B("free", lifeInfo.free).N("kills", lifeInfo.kills).N("deaths", lifeInfo.deaths);
    rec.N("xp", PlayerXPCompat(bot)).N("xpnext", PlayerNextLevelXPCompat(bot));
    rec.N("bags", bot->GetFreeInventorySlotCount());
    rec.B("sick", bot->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS));
    if (ai)
        rec.N("dur", AI_VALUE(uint8, "lowest durability"));
    rec.N("rescue", lifeInfo.rescues).N("tele", lifeInfo.teleports);
    if (ai && context && context->GetValue<Unit*>("grind target"))
        if (Unit* grind = context->GetValue<Unit*>("grind target")->Get())
            rec.S("grind", std::to_string(grind->GetEntry()) + ":" + std::to_string(grind->GetLevel()) + ":" + Fixed(bot->GetDistance(grind), 1));
    // corpserun: release (a body that will release) | walk | delay | safe-spot | reclaim | healer | rescue; empty while alive.
    std::string phase;
    float ghostYd = -1.0f;
    if (!alive)
    {
        if (!ghost)
            phase = "release";
        else
        {
            phase = "walk";
            Corpse* corpse = bot->GetCorpse();
            bool const corpseHere = corpse && corpse->GetMapId() == bot->GetMapId();
            if (corpseHere)
            {
                ghostYd = bot->GetDistance(corpse);
                bool const unsafe = ai && ai->safeReviveUnsafeSinceMs_ && ai->safeReviveCorpseGhostTime_ == corpse->GetGhostTime();
                if (unsafe && !TortoiseBots::SafeReviveWaitExpired(ai->safeReviveUnsafeSinceMs_, WorldTimer::getMSTime()))
                    phase = "safe-spot";
                else if (corpse->IsWithinDistInMap(bot, (float)CORPSE_RECLAIM_RADIUS, true))
                {
                    int64 const wait = int64(corpse->GetGhostTime()) + int64(bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) - int64(time(nullptr));
                    phase = wait > 0 ? "delay" : "reclaim";
                }
            }
            if (ai && AI_VALUE2(int32, "manual int", "w1 ghost leg") == 1)
                phase = "healer";
            if (lifeInfo.rescuedThisDeath)
                phase = "rescue";
        }
    }
    rec.S("corpserun", phase);
    if (ghostYd >= 0.0f)
        rec.N("ghostyd", static_cast<int32>(ghostYd + 0.5f));
    chatin::AppendState(rec, guid);                           // S1-R2: chatin=, chatlast= (appended: the older keys are unchanged)
    rec.S("prof", tbhost::prof::FormatStateProf(bot));
    rec.N("taxi", tbhost::taxi::KnownNodeCount(bot)).B("onTaxi", tbhost::taxi::InFlight(bot));
    ObjectGuid const trGuid = tbhost::ride::CurrentTransportGuid(bot);
    rec.S("transport", trGuid.IsEmpty() ? std::string("none") : trGuid.ToString());
    uint32 ahPosted = 0, ahSold = 0;
    tbhost::ah::GetBotStateCounts(bot, ahPosted, ahSold);
    rec.S("ah", std::to_string(ahPosted) + "/" + std::to_string(ahSold));
    if (auto gorder = tbhost::guild_orders::FindOrder(guid.GetCounter()))
        rec.P("order", {std::to_string(gorder->itemId), std::to_string(gorder->delivered) + "/" + std::to_string(gorder->amount)});
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
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, false, false);
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
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, false, false);
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

// ---- `.tb school <bot|party [leader]> [#token]` (#48: what the school switch sees) ----

bool HandleSchool(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "school"))
        return false;
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, false, false);
    if (!who.ok())
        return Fail(handler, args, "school", who.code, who.detail);
    if (pos < args.w.size())
        return Fail(handler, args, "school", "unexpected-argument", args.w[pos]);

    Reply reply(handler, args, "school");
    reply.Begin();
    for (Player* bot : who.bots)
    {
        // The unit the bot is fighting, else the one it has selected: read only.
        Unit* target = bot->GetVictim();
        if (!target)
            target = bot->GetSelectedUnit();
        school::Reading const reading = school::Read(target);
        school::SpecSchools const spec = school::SchoolsOf(bot, AiFactory::GetPlayerSpecTab(bot));
        std::string taken;
        for (int schoolIndex = 0; schoolIndex < ai::kSchoolCount; ++schoolIndex)
            taken += (schoolIndex ? "," : "") + std::to_string(int(reading.state.takenPct[schoolIndex]));
        Rec rec("school");
        rec.S("bot", bot->GetName())
            .N("tgt", target ? target->GetEntry() : 0)
            .N("immune", reading.state.immuneMask)
            .S("taken", taken)
            .S("main", ai::SchoolWord(spec.mainSchool))
            .S("pick", ai::SchoolWord(school::PickFor(spec, target)))
            .S("src", reading.coreReading ? "core" : (reading.dataRow ? "data" : "none"));
        reply.Line(rec);
    }
    Rec end("school");
    end.N("bots", who.bots.size()).B("on", school::Enabled()).N("gain", school::MinGainPct()).P("target", LabelParts(who.label));
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
        std::string code, detail;
        if (!CheckPartyLeadAccess(handler, leader, code, detail))
            return Fail(handler, args, "party", code, detail);
        // N5: bots join only a bot's party or the sender's own character's party, never a real player's.
        if (!MayLeadBots(handler, leader->GetGUID()))
            return Fail(handler, args, "party", "not-allowed", args.w[0] + " is neither a host bot nor your own character: bots do not join a real player's party");
        bool const raids = BotMgr::Instance().RaidsEnabled();
        if (args.w.size() - 1 > raidgroups::MaxNamedMembers(raids))
            return Fail(handler, args, "party", "too-many", raids ? "a raid holds 40: the leader and 39 members"
                : "a party holds five: the leader and four members (TortoiseBots.Raid.Enable = 1 lets it become a raid)");
        std::set<ObjectGuid> seen;
        seen.insert(leader->GetGUID());
        for (size_t i = 1; i < args.w.size(); ++i)
        {
            Player* member = FindOnline(args.w[i]);
            if (!member)
                return Fail(handler, args, "party", "no-such-player", args.w[i] + " is not in the world");
            if (!IsHostBot(member))
                return Fail(handler, args, "party", "not-a-bot", args.w[i] + " is not a logged-in host bot");
            if (!CheckBotAccess(handler, member, code, detail))
                return Fail(handler, args, "party", code, detail);
            if (!seen.insert(member->GetGUID()).second)
                return Fail(handler, args, "party", "duplicate", args.w[i]);
        }
        // A member that is in another party (the core keeps an old group with its offline members: a persisted group blocks the join) refuses the command
        // before anything is changed, and the error names the member and the party to break up: `.tb ungroup <member>` is the one way out.
        for (size_t i = 1; i < args.w.size(); ++i)
        {
            Player* member = FindOnline(args.w[i]);
            Group* other = member->GetGroup() == leader->GetGroup() ? nullptr : member->GetGroup();
            if (other)
            {
                std::string names;
                for (Group::MemberSlot const& slot : other->GetMemberSlots())
                {
                    Player* holder = ObjectAccessor::FindPlayer(slot.guid);
                    if (!names.empty())
                        names += ", ";
                    names += slot.name + (holder && holder->IsInWorld() ? "" : " (offline)");
                }
                return Fail(handler, args, "party", "party-refused", member->GetName() + " is already in another party (" + names +
                    "): .tb ungroup " + member->GetName() + " first");
            }
            if (member->GetGroupInvite())
                return Fail(handler, args, "party", "party-refused", member->GetName() + " has a pending party invite: cancel it, then form the party");
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
                if (Group* group = member->GetGroup(); group && group->isRaidGroup())
                {
                    // A raid with a layout seats the newcomer by its role; without one it stays where the core put it (the first free seat).
                    PlaceJoiner(group, member);
                    rec.N("sub", group->GetMemberGroup(member->GetGUID()) + 1);
                }
            }
            else
                rec.S("status", "refused").S("code", "party-refused").T("msg", error);
            reply.Line(rec);
        }
        Rec end("party");
        Group const* formed = leader->GetGroup();
        bool const raid = formed && formed->isRaidGroup();
        end.S("leader", leader->GetName()).N("joined", joined).N("asked", args.w.size() - 1).B("raid", raid)
            .S("note", raid ? "the host master is the leader: run .tb layout, then .tb master" : "the host master is the leader: run .tb master next");
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
        if (group->isRaidGroup())
        {
            rec.N("sub", slot.group + 1);
            if (slot.flags & MEMBER_FLAG_MAINTANK)
                rec.S("mark", "maintank");
            else if (slot.flags & MEMBER_FLAG_ASSISTANT)
                rec.S("mark", "assistant");
        }
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
            // The party's commander is the one its MEMBERS obey: the leader's own binding is on its line, and a run binds the members to the leader, so the
            // party reads the run's commander (one name) instead of "mixed" (the leader still bound to its own commander).
            if (bound && commander && !group->IsLeader(slot.guid))
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

// ---- `.tb gear <bot|party [leader]> upgrade [#token]` | `.tb gear <who> set [spec] [level] [quality] [#token]` | `.tb gear audit [level] [#token]` ----

namespace
{
bool ParseGearQuality(std::string const& raw, int32& outQuality)
{
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.rfind("quality=", 0) == 0)
        s = s.substr(8);
    if (s == "poor" || s == "0")      { outQuality = 0; return true; }
    if (s == "common" || s == "1")    { outQuality = 1; return true; }
    if (s == "uncommon" || s == "2")  { outQuality = 2; return true; }
    if (s == "rare" || s == "3")      { outQuality = 3; return true; }
    if (s == "epic" || s == "4")      { outQuality = 4; return true; }
    return false;
}

bool ParseGearLevel(std::string const& raw, uint32& outLevel)
{
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.rfind("level=", 0) == 0)
        s = s.substr(6);
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    uint32 val = 0;
    try { val = static_cast<uint32>(std::stoul(s)); } catch (...) { return false; }
    if (val < 1 || val > 60)
        return false;
    outLevel = val;
    return true;
}
} // namespace

bool HandleGear(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "gear"))
        return false;

    // Form 1: `.tb gear audit [level] [#token]`
    if (!args.w.empty() && Lower(args.w[0]) == "audit")
    {
        std::vector<uint32> levels;
        if (args.w.size() > 2)
            return Fail(handler, args, "gear", "unexpected-argument", args.w[2]);
        if (args.w.size() == 2)
        {
            uint32 lvl = 0;
            if (!ParseGearLevel(args.w[1], lvl))
                return Fail(handler, args, "gear", "bad-argument", args.w[1]);
            levels.push_back(lvl);
        }
        else
        {
            levels = { 10, 30, 60 };
        }

        uint32 const total = TortoiseBots::gear::GearIndexItemCount();
        Reply reply(handler, args, "gear");
        reply.Begin();
        for (uint32 lvl : levels)
        {
            for (auto const& e : TortoiseBots::gear::AuditGearIndex(lvl))
            {
                Rec rec("gear");
                rec.N("level", lvl).N("slot", e.slot).N("candidates", e.candidates);
                reply.Line(rec);
            }
        }
        Rec end("gear");
        end.S("verb", "audit").N("total", total);
        reply.End(end);
        return true;
    }

    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" &&
                               Lower(args.w[1]) != "upgrade" && Lower(args.w[1]) != "set" && Lower(args.w[1]) != "show";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, true);
    if (!who.ok())
        return Fail(handler, args, "gear", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "gear", "missing-verb", "upgrade|set|audit|show");
    std::string const verb = Lower(args.w[pos++]);

    if (verb == "set")
    {
        std::string specArg;
        uint32 levelArg = 0;
        int32 qualityArg = -1;

        while (pos < args.w.size())
        {
            std::string const& tok = args.w[pos++];
            std::string const low = Lower(tok);
            if (low.rfind("quality=", 0) == 0 || low == "poor" || low == "common" ||
                low == "uncommon" || low == "rare" || low == "epic")
            {
                if (qualityArg >= 0 || !ParseGearQuality(tok, qualityArg))
                    return Fail(handler, args, "gear", "bad-argument", tok);
            }
            else if (low.rfind("level=", 0) == 0 ||
                     (!low.empty() && std::all_of(low.begin(), low.end(), [](unsigned char c) { return std::isdigit(c) != 0; })))
            {
                if (levelArg != 0 || !ParseGearLevel(tok, levelArg))
                    return Fail(handler, args, "gear", "bad-argument", tok);
            }
            else if (low.rfind("spec=", 0) == 0)
            {
                if (!specArg.empty() || tok.size() <= 5)
                    return Fail(handler, args, "gear", "bad-argument", tok);
                specArg = tok.substr(5);
            }
            else if (specArg.empty())
            {
                specArg = tok;
            }
            else
            {
                return Fail(handler, args, "gear", "unexpected-argument", tok);
            }
        }

        for (Player* target : who.bots)
        {
            if (target->IsInCombat())
                return Fail(handler, args, "gear", "in-combat", target->GetName() + " is in combat");
        }

        uint32 totalEquipped = 0;
        uint32 totalFailed = 0;
        Reply reply(handler, args, "gear");
        reply.Begin();
        for (Player* target : who.bots)
        {
            TortoiseBots::gear::GearSetOutcome const outcome =
                TortoiseBots::gear::SetGearForTarget(target, specArg, levelArg, qualityArg);
            if (!outcome.ok)
            {
                Rec rec("gear");
                rec.S("bot", target->GetName()).S("status", "refused").S("code", outcome.errorCode).S("msg", outcome.errorDetail);
                reply.Line(rec);
                ++totalFailed;
                continue;
            }
            totalEquipped += outcome.equipped;
            totalFailed += outcome.failed;
            for (auto const& sr : outcome.slots)
            {
                Rec rec("gear");
                rec.S("bot", target->GetName())
                   .N("slot", sr.slot)
                   .N("item", sr.itemId)
                   .N("ilvl", sr.itemLevel)
                   .N("score", sr.score)
                   .S("status", sr.status);
                if (!sr.code.empty())
                    rec.S("code", sr.code);
                reply.Line(rec);
            }
        }
        Rec end("gear");
        end.P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("equipped", totalEquipped).N("failed", totalFailed);
        reply.End(end);
        return true;
    }

    // Form 4: `.tb gear <bot|party [leader]> show [#token]` (#20; the shared equipment metric of #26). READ ONLY and
    // allowed in combat: the resistance totals of every worn piece, and its stat score for the bot's own spec.
    if (verb == "show")
    {
        if (pos < args.w.size())
            return Fail(handler, args, "gear", "unexpected-argument", args.w[pos]);

        Reply reply(handler, args, "gear");
        reply.Begin();
        for (Player* bot : who.bots)
        {
            uint8 const playerClass = bot->GetClass();
            uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
            if (!specId)
                specId = sRandomItemMgr.GetFallbackSpecId(playerClass);
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item)
                    continue;
                ItemPrototype const* proto = tbcompat::ItemProto(item->GetEntry());
                if (!proto)
                    continue;
                ItemSpecType itSpec;
                uint32 const score = sRandomItemMgr.CalculateStatWeight(playerClass, static_cast<uint8>(specId), proto, itSpec);
                Rec rec("gear");
                rec.S("bot", bot->GetName())
                   .N("slot", slot)
                   .N("item", item->GetEntry())
                   .N("ilvl", proto->ItemLevel)
                   .N("score", score);
                rec.Csv("res", { proto->HolyRes, proto->FireRes, proto->NatureRes, proto->FrostRes, proto->ShadowRes, proto->ArcaneRes });
                reply.Line(rec);
            }
        }
        Rec end("gear");
        end.N("bots", who.bots.size()).P("target", LabelParts(who.label));
        reply.End(end);
        return true;
    }

    if (verb != "upgrade")
        return Fail(handler, args, "gear", "unknown-verb", args.w[pos - 1]);
    if (pos < args.w.size())
        return Fail(handler, args, "gear", "unexpected-argument", args.w[pos]);

    // `upgrade` requires a host bot with a running AI engine.
    for (Player* bot : who.bots)
    {
        if (!IsHostBot(bot))
            return Fail(handler, args, "gear", "not-a-bot", bot->GetName() + " is not a logged-in host bot");
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || !ai->GetAiObjectContext())
            return Fail(handler, args, "gear", "no-engine", bot->GetName() + " has no running engine (`.tb engine <bot> on`)");
        if (bot->IsInCombat())
            return Fail(handler, args, "gear", "in-combat", bot->GetName() + " is in combat");
    }

    uint32 totalChanged = 0;
    Reply reply(handler, args, "gear");
    reply.Begin();
    for (Player* bot : who.bots)
    {
        std::array<uint32, EQUIPMENT_SLOT_END> before{};
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                before[slot] = item->GetEntry();

        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        {
            // Explicit order: allow commanded bots through MayAutoEquipUpgradesCompat for this call only.
            tbs_w6::ScopedExplicitEquipOrder explicitOrder;
            ai->DoSpecificAction("equip upgrades", Event("tb gear", std::string(), bot), true);
        }

        uint32 botChanged = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            uint32 const afterId = item ? item->GetEntry() : 0;
            if (afterId != before[slot])
            {
                ++botChanged;
                ++totalChanged;
                Rec rec("gear");
                rec.S("bot", bot->GetName()).N("slot", slot).N("old", before[slot]).N("new", afterId);
                reply.Line(rec);
            }
        }
        if (botChanged == 0)
        {
            Rec rec("gear");
            rec.S("bot", bot->GetName()).N("changed", 0);
            reply.Line(rec);
        }
    }
    Rec end("gear");
    end.P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("changed", totalChanged);
    reply.End(end);
    return true;
}

// Bot API (TBF-B1): the words the provider copies into a Bot handle, the same ones `.tb state` writes. Only the engine
// build carries their code: a slice build without ForeverLua must stay exactly as it was.
#if defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
std::string RoleText(Player* bot, PlayerbotAI* ai)
{
    return RoleOf(bot, ai);   // never empty (finding 6)
}

bool RoleForced(ObjectGuid bot)
{
    return PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot) != 0;
}

std::string SpecText(Player* bot)
{
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());
    return sRandomItemMgr.GetSpecName(specId);
}

std::string EngineStateText(Player* bot, PlayerbotAI* ai)
{
    std::string state = !bot->IsAlive() ? "dead" : bot->IsInCombat() ? "combat" : "non-combat";
    if (ai)
        state = ai->GetState() == BotState::BOT_STATE_DEAD ? "dead" : ai->GetState() == BotState::BOT_STATE_COMBAT ? "combat" : "non-combat";
    return state;
}
#endif
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
