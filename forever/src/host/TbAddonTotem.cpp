// The `totem` addon verb (TBF #133 piece c-totem): TB's `.bot totem`
// grammar (TortoiseBots/commands/Totem.cpp:229-464) over the TBM channel,
// with TB's TBM:TOTEM record and ACTION_ACK/ERR lines (BotTotem.lua).
// Permission is the player tier Owner per target bot
// (host/player/TbPlayerAuthPure.h, COMMON-133: Owner for changes on own
// bots, GM all). Orders live in the bot AIs' `manual saved string` values,
// which the shaman triggers/actions consult first (shared code, already
// compiled), so the next fight drops the loadout. Cross-session persistence
// of those values is piece c-store's; until it lands, orders hold in
// session (the missing value reads as "" = auto, never crashes).
#include "host/TbAddonTotem.h"
#include "host/TbAddonTotemPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonChannel.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "playerbot/strategy/shaman/ShamanTotemGroupPolicy.h"
#include "TbBotMgr.h"

#include "compat/botpch_forever.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Engine.h"
#include "playerbot/strategy/Event.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace totem
{
namespace
{
// The caller's Battle.net id, 0 when the session's own game account is no
// longer one of its game accounts (TbAddonActions.cpp ActionBnet).
std::uint32_t TotemBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

// FactsFor (TbAddonActions.cpp): the same facts from the same exported calls.
player::BotFacts TotemFacts(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

bool OwnerAllowed(player::Caller const& caller, std::uint32_t bnet, Player* requester, Player* bot)
{
    return bot && bot != requester && bot->IsInWorld() &&
        player::Decide(caller, TotemFacts(requester, bot, bnet), player::Tier::Owner) ==
            player::Verdict::Allowed;
}

std::string LowerName(std::string_view name)
{
    std::string out;
    for (char c : name)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

PlayerbotAI* AiOf(Player* bot)
{
    return bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
}

std::string StoredOrder(PlayerbotAI* ai, int slot)
{
    if (!ai || !ai->GetAiObjectContext())
        return std::string();
    ai::Value<std::string>* value =
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot));
    return value ? value->Get() : std::string();
}

void SetStoredOrder(PlayerbotAI* ai, int slot, std::string const& stored)
{
    if (!ai || !ai->GetAiObjectContext())
        return;
    ai::Value<std::string>* value =
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot));
    if (value)
        value->Set(stored);
}

std::string StoredMode(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return std::string();
    ai::Value<std::string>* value =
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemModeKey());
    return value ? value->Get() : std::string();
}

// One TBM:TOTEM record per bot (TB Totem.cpp SendTotemLine): stored orders,
// drop mode, then every totem the bot has trained (each name once).
void SendTotemLine(Player* requester, Player* bot, PlayerbotAI* ai)
{
    std::vector<std::string> trained;
    for (int slot = 0; slot < 4; ++slot)
    {
        std::uint32_t count = 0;
        char const* const* choices = ai::TotemChoicesForSlot(slot, count);
        for (std::uint32_t i = 0; i < count; ++i)
            if (ai && ai->HasSpell(choices[i]))
                trained.push_back(choices[i]);
    }
    addon::Send(requester, FormatTotemLine(addon::Safe(bot->GetName()), DisplaySlot(StoredOrder(ai, 0)),
        DisplaySlot(StoredOrder(ai, 1)), DisplaySlot(StoredOrder(ai, 2)), DisplaySlot(StoredOrder(ai, 3)),
        DisplayMode(StoredMode(ai)), JoinKnown(trained)));
}

// Subgroup census for one bot (TB Totem.cpp TotemDropCensus, the trigger's
// ReadTotemSubgroup shape): own-subgroup live mates on the same map within
// 100 yd. Hunters count as melee (pet swings), warriors/rogues always, feral
// druids and melee-flagged shamans too.
ai::TotemSubgroupCensus TotemDropCensus(PlayerbotAI* ai, Player* bot)
{
    ai::TotemGroupMember members[6];
    std::uint8_t count = 0;
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (group && bot)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || member == bot || !member->IsInWorld())
                continue;
            if (!group->SameSubGroup(bot, member))
                continue;
            if (member->GetMapId() != bot->GetMapId() || bot->GetDistance(member) > 100.0f)
                continue;
            if (!member->IsAlive())
                continue;
            uint8 cls = member->GetClass();
            bool melee = cls == CLASS_WARRIOR || cls == CLASS_ROGUE || cls == CLASS_HUNTER ||
                (cls == CLASS_DRUID && ai->IsTank(member)) || (cls == CLASS_SHAMAN && ai->IsMelee(member));
            bool mana = member->GetMaxPower(POWER_MANA) > 0;
            if (count < 6)
                members[count++] = ai::TotemGroupMember{false, melee, mana};
        }
    }
    return ai::CountTotemSubgroup(members, count);
}

std::string TotemWantForBot(PlayerbotAI* ai, Player* bot, int slot)
{
    if (!ai)
        return std::string();
    std::string const stored = StoredOrder(ai, slot);
    std::string const ordered = ai::OrderedTotemForSlot(stored);
    if (!ordered.empty())
        return ordered;
    // Explicit "none" ("") holds the slot empty; auto/unset resolves the
    // subgroup pick at drop time (drop never forces a totem the subgroup
    // would not want — caster groups keep earth empty the same way).
    if (!stored.empty() && stored != "auto")
        return std::string();
    ai::TotemSubgroupCensus census = TotemDropCensus(ai, bot);
    switch (slot)
    {
    case 0:
        return ai::BestEarthTotemForSubgroup(census, ai->HasSpell("strength of earth totem"));
    case 2:
        return ai::BestWaterTotemForSubgroup(census,
            ai->HasSpell("mana spring totem"), ai->HasSpell("healing stream totem"));
    case 3:
        return ai::BestAirTotemForSubgroup(census,
            ai->HasSpell("windfury totem"), ai->HasSpell("grace of air totem"),
            ai->HasSpell("tranquil air totem"));
    default:
        return std::string();
    }
}

bool IsShamanBot(Player* bot)
{
    return bot && bot->GetClass() == CLASS_SHAMAN;
}

// Queue one totem cast on the bot's active engine (TB BotCommandContext.h
// QueueMatureAction): the engine's prerequisite/continuation chain handles
// reach-then-cast, so `drop` only queues. Combat bots queue on the combat
// engine, like an explicit order would.
bool QueueTotem(PlayerbotAI* ai, Player* bot, std::string const& want, Player* requester)
{
    if (!ai || !bot || want.empty())
        return false;
    if (bot->IsInCombat())
        ai->OnCombatStarted();
    ai::Engine* engine = ai->GetCurrentEngine();
    if (!engine)
        return false;
    ai::Event dropEvent("totem drop", std::string(), requester);
    return engine->QueueAction(want, ai::ACTION_PASSTROUGH, dropEvent);
}

bool HasTotemUp(PlayerbotAI* ai, std::string const& want)
{
    if (!ai || !ai->GetAiObjectContext() || want.empty())
        return false;
    ai::Value<bool>* value = ai->GetAiObjectContext()->GetValue<bool>("has totem", want);
    return value && value->Get();
}

void HandleTotemVerb(Player* requester, std::string_view args)
{
    Request parsed = ParseTotemRequest(args);
    if (!parsed.ok)
    {
        addon::Err(requester, "totem", parsed.error.empty() ? "invalid" : parsed.error,
            parsed.message.empty() ? Usage() : parsed.message);
        return;
    }
    if (!requester)
        return;
    std::uint32_t const bnet = TotemBnet(requester);
    player::Caller caller;
    caller.gm = requester->GetSession() && requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();

    // Scope: one owned bot, or the live owned party (/raid) scope (TB
    // Totem.cpp: the group walk, or the master's bots ungrouped).
    std::vector<Player*> bots;
    if (parsed.partyScope)
    {
        if (Group* group = requester->GetGroup())
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                Player* member = ObjectAccessor::FindPlayer(slot.guid);
                if (member && member != requester && OwnerAllowed(caller, bnet, requester, member))
                    bots.push_back(member);
            }
        }
        else
        {
            for (Player* bot : BotMgr::Instance().BotsOfMaster(requester->GetGUID()))
                if (bot && bot != requester && OwnerAllowed(caller, bnet, requester, bot))
                    bots.push_back(bot);
        }
        if (bots.empty())
        {
            addon::Err(requester, "totem", "no-bot", "No live owned party bots are controllable.");
            return;
        }
        std::sort(bots.begin(), bots.end(),
            [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
    }
    else
    {
        std::string const wanted = parsed.scope;
        Player* named = nullptr;
        for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot || LowerName(bot->GetName()) != wanted)
                continue;
            if (!OwnerAllowed(caller, bnet, requester, bot))
                continue;
            named = bot;
            break;
        }
        if (!named)
        {
            addon::Err(requester, "totem", "no-bot", "Unknown or uncontrollable bot name.");
            return;
        }
        bots.push_back(named);
    }

    std::string const scope = AckScope(parsed.partyScope, bots.front()->GetName());

    if (parsed.kind == Request::Kind::Status)
    {
        for (Player* bot : bots)
            SendTotemLine(requester, bot, AiOf(bot));
        addon::Ack(requester, "totem status", scope, static_cast<std::uint32_t>(bots.size()), "-");
        return;
    }

    if (parsed.kind == Request::Kind::Mode)
    {
        std::uint32_t succeeded = 0;
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = AiOf(bot);
            if (!ai || !IsShamanBot(bot))
                continue;
            ai::Value<std::string>* mode =
                ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemModeKey());
            if (mode)
                mode->Set(parsed.mode);
            SendTotemLine(requester, bot, ai);
            ++succeeded;
        }
        if (!succeeded)
        {
            addon::Err(requester, "totem mode", "no-shaman", "No shaman bots in scope.");
            return;
        }
        addon::Ack(requester, "totem mode", scope, succeeded, parsed.mode);
        return;
    }

    if (parsed.kind == Request::Kind::Drop)
    {
        std::uint32_t queued = 0;
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = AiOf(bot);
            if (!ai || !IsShamanBot(bot))
                continue;
            std::uint32_t placed = 0;
            for (int slot = 0; slot < 4; ++slot)
            {
                std::string const want = TotemWantForBot(ai, bot, slot);
                if (want.empty() || !ai->HasSpell(want))
                    continue;
                if (HasTotemUp(ai, want))
                    continue;
                if (QueueTotem(ai, bot, want, requester))
                    ++placed;
            }
            if (!placed)
                continue;
            // The set lands one slot per few ticks (the 4 s drop stagger, with
            // a queued retry while this stamp is fresh) and the recall refund
            // stands down for the same window, so the ordered set is not
            // wiped before it ever stands.
            if (ai::AiObjectContext* ctx = ai->GetAiObjectContext())
                if (ai::Value<time_t>* dropStamp = ctx->GetValue<time_t>("manual time", ai::TotemDropTimeKey()))
                    dropStamp->Set(time(0));
            queued += placed;
        }
        if (!queued)
        {
            addon::Err(requester, "totem drop", "nothing",
                "No totems to drop (slots full, none configured, or not trained).");
            return;
        }
        addon::Ack(requester, "totem drop", scope, queued, "-");
        return;
    }

    // `set`: all four slots validated first (TB: a bot missing any named
    // totem is skipped whole); `slot`: one slot.
    std::uint32_t succeeded = 0;
    bool refused = false;
    for (Player* bot : bots)
    {
        PlayerbotAI* ai = AiOf(bot);
        if (!ai || !IsShamanBot(bot))
            continue;
        bool trained = true;
        if (parsed.kind == Request::Kind::Set)
        {
            for (int slot = 0; slot < 4 && trained; ++slot)
            {
                if (!parsed.storedSet[slot].empty() && parsed.storedSet[slot] != "auto" &&
                    !ai->HasSpell(parsed.storedSet[slot]))
                {
                    addon::Err(requester, "totem set", "unknown-spell",
                        std::string(bot->GetName()) + " has not trained " + parsed.storedSet[slot] + ".");
                    trained = false;
                    refused = true;
                }
            }
            if (!trained)
                continue;
            for (int slot = 0; slot < 4; ++slot)
                SetStoredOrder(ai, slot, parsed.storedSet[slot]);
        }
        else
        {
            if (!parsed.stored.empty() && parsed.stored != "auto" && !ai->HasSpell(parsed.stored))
            {
                addon::Err(requester, "totem", "unknown-spell",
                    std::string(bot->GetName()) + " has not trained " + parsed.stored + ".");
                refused = true;
                continue;
            }
            SetStoredOrder(ai, parsed.slot, parsed.stored);
        }
        SendTotemLine(requester, bot, ai);
        ++succeeded;
    }
    if (!succeeded)
    {
        if (!refused)
            addon::Err(requester, parsed.kind == Request::Kind::Set ? "totem set" : "totem", "no-shaman",
                "No shaman bots in scope.");
        return;
    }
    if (parsed.kind == Request::Kind::Set)
        addon::Ack(requester, "totem set", scope, succeeded, "-");
    else
        addon::Ack(requester, "totem", scope, succeeded,
            ai::TotemSlotName(parsed.slot) + std::string("=") +
                (parsed.stored.empty() ? "none" : parsed.stored));
}
} // namespace

void RegisterAddonVerbsTotem()
{
    addon::RegisterVerb("totem", &HandleTotemVerb);
    // FakeServer's FAKE_CAPS advertises it; BotTotem.lua lights up once the
    // server answers with the capability.
    addon::AdvertiseCap("totem");
}

} // namespace totem
} // namespace TortoiseBots

#else // no engine: the verb is unknown and the channel answers it so

void TortoiseBots::totem::RegisterAddonVerbsTotem()
{
}

#endif
