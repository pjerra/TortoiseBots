// The `behavior` and `service` addon verbs (TBF #133 piece c-behsvc):
// - `behavior <bot|party|raid> <key> <on|off>` and `behavior <bot|party|raid> [status]`:
//   per-bot / party toggles of the 13 allowlisted strategies (`offheal`/`offdps`
//   only on off-spec classes) + TBM:BOTSTATE refresh.
// - `service <bot>` and `service <bot> portal <spellId|city>`:
//   stateless query of food/water/healthstone/portals (`TBM:SERVICE`) and
//   MagePortalPolicy-validated portal cast.
#include "host/TbAddonBehaviorService.h"
#include "host/TbAddonBehaviorServicePure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/TbAddonChannel.h"
#include "host/TbAddonRosterPure.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "PlayerbotAIStorage.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace behsvc
{
namespace
{

std::uint32_t ActionBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
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
        player::Decide(caller, FactsFor(requester, bot, bnet), player::Tier::Owner) ==
            player::Verdict::Allowed;
}

player::Caller MakeCaller(Player* requester, std::uint32_t bnet)
{
    player::Caller caller;
    caller.gm = requester && requester->GetSession() &&
        requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester ? requester->GetGUID().GetCounter() : 0;
    return caller;
}

Player* FindOwnedBotByName(Player* requester, player::Caller const& caller, std::uint32_t bnet,
    std::string_view botName, std::string& outErrCode, std::string& outErrMsg)
{
    std::string const wanted = Lower(botName);
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
            continue;
        if (Lower(bot->GetName()) != wanted)
            continue;
        player::Verdict const v = player::Decide(caller, FactsFor(requester, bot, bnet), player::Tier::Owner);
        if (v == player::Verdict::Allowed)
            return bot;
        outErrCode = player::CodeOf(v);
        outErrMsg = (v == player::Verdict::InRun)
            ? "That bot is in an active dungeon run."
            : "Unknown or uncontrollable bot name.";
        return nullptr;
    }
    outErrCode = "no-bot";
    outErrMsg = "Unknown or uncontrollable bot name.";
    return nullptr;
}

std::vector<Player*> ResolvePartyScope(Player* requester, player::Caller const& caller, std::uint32_t bnet)
{
    std::vector<Player*> bots;
    if (!requester)
        return bots;
    if (Group* group = requester->GetGroup())
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (OwnerAllowed(caller, bnet, requester, member))
                bots.push_back(member);
        }
    }
    else
    {
        for (Player* bot : BotMgr::Instance().BotsOfMaster(requester->GetGUID()))
        {
            if (OwnerAllowed(caller, bnet, requester, bot))
                bots.push_back(bot);
        }
    }
    std::sort(bots.begin(), bots.end(),
        [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
    return bots;
}

BotState ToEngineState(BehaviorState state)
{
    switch (state)
    {
        case BehaviorState::Combat: return BotState::BOT_STATE_COMBAT;
        case BehaviorState::NonCombat: return BotState::BOT_STATE_NON_COMBAT;
        case BehaviorState::Reaction: return BotState::BOT_STATE_REACTION;
    }
    return BotState::BOT_STATE_COMBAT;
}

bool IsBehaviorOn(PlayerbotAI* ai, BehaviorToggle const& toggle)
{
    return ai->HasStrategy(toggle.strategy, ToEngineState(toggle.state)) != toggle.inverted;
}

void SetBehavior(PlayerbotAI* ai, BehaviorToggle const& toggle, bool enable)
{
    ai->ChangeStrategy((enable != toggle.inverted ? "+" : "-") + std::string(toggle.strategy),
        ToEngineState(toggle.state));
}

std::string MovementWord(Player* bot, PlayerbotAI* ai)
{
    if (ai->HasStrategy("passive", BotState::BOT_STATE_NON_COMBAT))
        return "flee";
    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT))
        return "follow";
    if (ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
        return "stay";
    if (ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
        return "guard";
    if (ai->HasStrategy("free", BotState::BOT_STATE_NON_COMBAT))
        return "free";
    return roster::BotMoveWord(BotMgr::Instance().IsFreeBot(bot->GetGUID()),
        h3::LastOrder(bot->GetGUID()).verb);
}

std::string BuildBotStateFields(Player* bot, PlayerbotAI* ai)
{
    std::string fields = "move=" + MovementWord(bot, ai);
    std::size_t toggleCount = 0;
    BehaviorToggle const* toggles = BehaviorToggles(toggleCount);
    for (std::size_t i = 0; i < toggleCount; ++i)
    {
        if (ClassAllowsToggle(bot->GetClass(), toggles[i]))
        {
            fields += std::string(",") + toggles[i].key + "=" +
                (IsBehaviorOn(ai, toggles[i]) ? "on" : "off");
        }
    }
    fields += std::string(",sap=") +
        (ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT) ? "off" : "on");

    std::size_t specCount = 0;
    char const* const* specsList = SpecStrategies(specCount);
    std::string specs;
    for (std::size_t i = 0; i < specCount; ++i)
    {
        if (ai->HasStrategy(specsList[i], BotState::BOT_STATE_COMBAT))
        {
            if (!specs.empty())
                specs += "+";
            specs += specsList[i];
        }
    }
    if (!specs.empty())
        fields += ",specs=" + specs;

    std::string focus;
    if (AiObjectContext* ctx = ai->GetAiObjectContext())
        if (ai::Value<std::string>* rti = ctx->GetValue<std::string>("rti"))
            focus = rti->Get();
    fields += ",focus=" + addon::Safe(focus);
    return fields;
}

void SendBotState(Player* requester, Player* bot, PlayerbotAI* ai)
{
    if (!requester || !bot || !ai)
        return;
    addon::Send(requester, FormatBotStateLine(bot->GetName(), BuildBotStateFields(bot, ai)));
}

void HandleBehavior(Player* requester, std::string_view args)
{
    if (!requester)
        return;
    ParsedBehavior const parsed = ParseBehaviorArgs(args);
    if (!parsed.ok)
    {
        addon::Err(requester, parsed.intent, "invalid", BehaviorUsage());
        return;
    }

    std::uint32_t const bnet = ActionBnet(requester);
    player::Caller const caller = MakeCaller(requester, bnet);

    if (parsed.isStatus)
    {
        std::vector<Player*> targets;
        if (parsed.isPartyScope)
        {
            targets = ResolvePartyScope(requester, caller, bnet);
            if (targets.empty())
            {
                addon::Err(requester, "behavior", "no-bot", "No live owned party bots are controllable.");
                return;
            }
        }
        else
        {
            std::string errCode;
            std::string errMsg;
            Player* bot = FindOwnedBotByName(requester, caller, bnet, parsed.botName, errCode, errMsg);
            if (!bot)
            {
                addon::Err(requester, "behavior", errCode, errMsg);
                return;
            }
            targets.push_back(bot);
        }

        std::vector<std::pair<Player*, PlayerbotAI*>> live;
        for (Player* bot : targets)
        {
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
                live.emplace_back(bot, ai);
        }
        if (live.empty())
        {
            addon::Err(requester, "behavior", "no-bot", "Unknown or uncontrollable bot name.");
            return;
        }
        addon::Send(requester, roster::BotStateBegin(live.size()));
        for (auto const& [bot, ai] : live)
            SendBotState(requester, bot, ai);
        addon::Send(requester, "TBM:BOTSTATE_END");
        addon::Ack(requester, "behavior", parsed.isPartyScope ? "party" : ("bot:" + live.front().first->GetName()),
            static_cast<std::uint32_t>(live.size()), "status");
        return;
    }

    BehaviorToggle const* toggle = FindBehaviorToggle(parsed.key);
    if (!toggle)
    {
        addon::Err(requester, parsed.intent, "invalid", BehaviorUsage());
        return;
    }

    if (parsed.isPartyScope)
    {
        std::vector<Player*> const scope = ResolvePartyScope(requester, caller, bnet);
        if (scope.empty())
        {
            addon::Err(requester, parsed.intent, "no-bot", "No live owned party bots are controllable.");
            return;
        }
        std::uint32_t succeeded = 0;
        for (Player* bot : scope)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai || !ClassAllowsToggle(bot->GetClass(), *toggle))
                continue;
            SetBehavior(ai, *toggle, parsed.enable);
            sPlayerbotDbStore.Save(ai);
            if (IsBehaviorOn(ai, *toggle) == parsed.enable)
                ++succeeded;
            SendBotState(requester, bot, ai);
        }
        addon::Ack(requester, parsed.intent, "party", succeeded, parsed.enable ? "on" : "off");
        return;
    }

    std::string errCode;
    std::string errMsg;
    Player* bot = FindOwnedBotByName(requester, caller, bnet, parsed.botName, errCode, errMsg);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !ai)
    {
        addon::Err(requester, parsed.intent, errCode.empty() ? "no-bot" : errCode,
            errMsg.empty() ? "Unknown or uncontrollable bot name." : errMsg);
        return;
    }
    if (!ClassAllowsToggle(bot->GetClass(), *toggle))
    {
        addon::Err(requester, parsed.intent, "class",
            "Only paladins, priests, shamans and druids have off-spec switches.");
        return;
    }

    SetBehavior(ai, *toggle, parsed.enable);
    sPlayerbotDbStore.Save(ai);
    bool const now = IsBehaviorOn(ai, *toggle);
    if (now != parsed.enable)
        addon::Err(requester, parsed.intent, "failed", "This bot does not support that behaviour.");
    else
        addon::Ack(requester, parsed.intent, "bot:" + bot->GetName(), 1, now ? "on" : "off");
    SendBotState(requester, bot, ai);
}

bool KnowsSpellLine(Player* bot, char const* lineName)
{
    if (!bot || !lineName)
        return false;
    std::string const wanted = Lower(lineName);
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED || playerSpell.disabled)
            continue;
        SpellInfo const* info = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info || !info->SpellName)
            continue;
        char const* rawName = info->SpellName->Str[LOCALE_enUS];
        if (!rawName || !*rawName)
            continue;
        std::string const spellName = Lower(rawName);
        if (spellName.compare(0, wanted.size(), wanted) == 0)
            return true;
    }
    return false;
}

std::vector<std::uint32_t> KnownPortalIds(Player* bot)
{
    std::vector<std::uint32_t> known;
    std::size_t count = 0;
    ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (sSpellMgr.GetSpellInfo(table[i].spellId, DIFFICULTY_NONE) && bot->HasSpell(table[i].spellId))
            known.push_back(table[i].spellId);
    }
    return known;
}

void HandleServiceQuery(Player* requester, Player* bot)
{
    bool const food = KnowsSpellLine(bot, ai::ConjureFoodSpellName());
    bool const water = KnowsSpellLine(bot, ai::ConjureWaterSpellName());
    bool const stone = KnowsSpellLine(bot, ai::CreateHealthstoneSpellName());
    std::string const portals = ai::FormatServicePortals(KnownPortalIds(bot));
    addon::Send(requester, FormatServiceLine(bot->GetName(), food, water, stone, portals));
}

void HandleServicePortal(Player* requester, Player* bot, PlayerbotAI* ai, std::uint32_t spellId)
{
    std::size_t count = 0;
    ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
    bool listed = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (table[i].spellId == spellId)
        {
            listed = true;
            break;
        }
    }

    ai::PortalCastFacts facts;
    facts.alive = bot->IsAlive();
    facts.inCombat = bot->IsInCombat();
    facts.casting = bot->IsNonMeleeSpellCast(false);
    facts.isMage = (bot->GetClass() == CLASS_MAGE);
    facts.requesterGrouped = requester && requester->IsInSameRaidWith(bot);

    bool const knowsSpell = listed && sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE) && bot->HasSpell(spellId);
    ai::PortalCastDecision const decision = !listed
        ? ai::PortalCastDecision{ ai::PortalRefusal::UnknownCity, false, false }
        : ai::DecidePortalCast(facts, spellId, knowsSpell,
            bot->HasItemCount(ai::PortalRuneItemId(), 1), bot->isMoving());

    if (decision.refusal != ai::PortalRefusal::None)
    {
        addon::Err(requester, "service portal", ai::PortalRefusalCode(decision.refusal),
            ai::PortalRefusalText(decision.refusal));
        return;
    }

    if (decision.needsRune)
        bot->StoreNewItemInBestSlots(ai::PortalRuneItemId(), 1, ItemContext::NONE);
    if (decision.needsStop)
        ai->StopMoving();
    if (bot->GetStandState() != UNIT_STAND_STATE_STAND)
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    if (!ai->CastSpell(spellId, bot))
    {
        addon::Err(requester, "service portal", "busy", "The portal fizzled; try again.");
        return;
    }
    addon::Ack(requester, "service portal", "bot:" + bot->GetName(), 1, bot->GetName());
}

void HandleService(Player* requester, std::string_view args)
{
    if (!requester)
        return;
    ParsedService const parsed = ParseServiceArgs(args);
    if (!parsed.ok)
    {
        addon::Err(requester, parsed.intent, parsed.errCode, parsed.errMsg);
        return;
    }

    std::uint32_t const bnet = ActionBnet(requester);
    player::Caller const caller = MakeCaller(requester, bnet);
    std::string errCode;
    std::string errMsg;
    Player* bot = FindOwnedBotByName(requester, caller, bnet, parsed.botName, errCode, errMsg);
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!bot || !ai)
    {
        addon::Err(requester, parsed.intent, errCode.empty() ? "no-bot" : errCode,
            errMsg.empty() ? "Unknown or uncontrollable bot name." : errMsg);
        return;
    }

    if (parsed.isQuery)
        HandleServiceQuery(requester, bot);
    else
        HandleServicePortal(requester, bot, ai, parsed.spellId);
}

} // namespace

void RegisterAddonVerbsBehaviorService()
{
    addon::RegisterVerb("behavior", &HandleBehavior);
    addon::RegisterVerb("service", &HandleService);
    addon::AdvertiseCap("behavior");
    addon::AdvertiseCap("service");
}

} // namespace behsvc
} // namespace TortoiseBots

#else   // no engine: the verbs stay unregistered and the channel answers unsupported

namespace TortoiseBots
{
namespace behsvc
{
void RegisterAddonVerbsBehaviorService() { }
} // namespace behsvc
} // namespace TortoiseBots

#endif
