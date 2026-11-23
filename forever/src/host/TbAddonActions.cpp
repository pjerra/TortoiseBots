// The `action` addon verb (TBF #130 piece f-actions): `action <intent>`
// with TB's intents (TortoiseBots/commands/Action.cpp), scope (the player's
// target when it is an owned bot, else the owned party) and ACK/ERR lines,
// routed to the TBF implementations: h3::QueueOrder for the movement, combat
// and upkeep orders, tactics::PickInterruptBot + queue for interrupt,
// h8::ClearCcOrder for cc clear. Permission is the player tier Tactical per
// target bot (host/player/TbPlayerAuthPure.h); the queue, not this verb,
// decides what each bot does on its tick.
#include "host/TbAddonActions.h"
#include "host/TbAddonActionsPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonChannel.h"
#include "host/TbCc.h"
#include "host/TbOrdersApi.h"
#include "host/TbTactics.h"
#include "host/TbTacticsPure.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace action
{
namespace
{
// RtiTargetValue::GetRtiIndex("skull") (ai/playerbot/strategy/values/
// RtiTargetValue.h) and SkullIndex (host/TbCc.cpp:54): the party's kill mark.
uint8 const kSkullIcon = 7;

// The caller's Battle.net id, 0 when the session's own game account is no
// longer one of its game accounts (ScopeOf, host/player/TbPlayerCmd.cpp:79).
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

// FactsOf (host/player/TbPlayerCmd.cpp:460) has no header declaration and its
// OwnerScope lives in that file (pjerra's R3/R5 files, not edited here), so
// this builds the same facts from the same exported calls. If TbPlayerCmd.h
// ever exports FactsOf/ScopeOf, this collapses to that call.
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

bool TacticalAllowed(player::Caller const& caller, std::uint32_t bnet, Player* requester, Player* bot)
{
    return bot && bot != requester && bot->IsInWorld() &&
        player::Decide(caller, FactsFor(requester, bot, bnet), player::Tier::Tactical) ==
            player::Verdict::Allowed;
}

std::string LowerName(std::string_view name)
{
    std::string out;
    for (char c : name)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// TB's enemy target (BotCommandContext.cpp BuildContext): the selection when
// it is not the requester and not a module bot.
Unit* EnemyTarget(Player* requester)
{
    Unit* selected = requester ? requester->GetSelectedUnit() : nullptr;
    if (!selected || selected == requester)
        return nullptr;
    if (BotMgr::Instance().IsBot(selected->GetGUID()))
        return nullptr;
    return selected;
}

// A dismissal ("cc clear"/"cc clear <Bot>"): one owned bot, or the whole
// owned scope, loses its mark through the same call as `.tb cc <bot> clear`.
// TB's dismissal semantics (commands/Action.cpp:1165-1213).
void HandleCcClear(Player* requester, player::Caller const& caller, std::uint32_t bnet,
    std::string const& intent, std::vector<Player*> const& scope, bool single)
{
    std::vector<Player*> clearScope;
    if (intent.size() > 9)
    {
        std::string const wanted = LowerName(intent.substr(9));
        Player* named = nullptr;
        for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot || LowerName(bot->GetName()) != wanted)
                continue;
            if (!TacticalAllowed(caller, bnet, requester, bot))
                continue;
            named = bot;
            break;
        }
        if (!named)
        {
            addon::Err(requester, intent, "no-bot", "Unknown or uncontrollable bot name for cc clear.");
            return;
        }
        clearScope.push_back(named);
        single = true;
    }
    else
        clearScope = scope;
    uint32 cleared = 0;
    std::string clearedName = "-";
    for (Player* bot : clearScope)
    {
        if (!bot || !bot->IsAlive())
            continue;
        std::string code, detail;
        if (!h8::ClearCcOrder(requester, bot, std::string(), ObjectGuid::Empty, code, &detail))
            continue;
        clearedName = bot->GetName();
        ++cleared;
    }
    if (!cleared)
    {
        addon::Err(requester, intent, "no-target", "Select an owned bot or have owned party bots to clear CC marks.");
        return;
    }
    addon::Ack(requester, intent,
        single && cleared == 1 ? ScopeName(true, clearedName) : "party", cleared, clearedName);
}

// One queued order per scope bot (`.tb order`'s verbs through h3::QueueOrder);
// interrupt picks its single bot first (tactics::PickInterruptBot, as `.tb
// order interrupt` does).
void HandleActionVerb(Player* requester, std::string_view args)
{
    ParsedAction const parsed = ParseAction(args);
    if (!parsed.ok)
    {
        addon::Err(requester, InvalidIntentName(args), "invalid", Usage());
        return;
    }
    std::string const& intent = parsed.intent;
    if (!IsSupported(intent))
    {
        addon::Err(requester, intent, "unsupported", WhyUnsupported(intent));
        return;
    }
    if (!requester || !requester->IsInWorld() || !requester->IsAlive() || requester->IsBeingTeleported())
    {
        addon::Err(requester, intent, "requester", "Requester must be alive, in world, and not teleporting.");
        return;
    }
    std::uint32_t const bnet = ActionBnet(requester);
    player::Caller caller;
    caller.gm = requester->GetSession() && requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();

    Player* selectedBot = nullptr;
    if (Unit* selected = requester->GetSelectedUnit())
        if (Player* bot = selected->ToPlayer())
            if (bot->IsAlive() && TacticalAllowed(caller, bnet, requester, bot))
                selectedBot = bot;

    std::vector<Player*> scope;
    bool single = selectedBot != nullptr;
    bool hadCandidates = false;
    if (selectedBot)
        scope.push_back(selectedBot);
    else if (Group* group = requester->GetGroup())
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || member == requester || !member->IsInWorld())
                continue;
            hadCandidates = true;
            if (TacticalAllowed(caller, bnet, requester, member))
                scope.push_back(member);
        }
    }
    else
    {
        for (Player* bot : BotMgr::Instance().BotsOfMaster(requester->GetGUID()))
        {
            if (!bot || bot == requester || !bot->IsInWorld())
                continue;
            hadCandidates = true;
            if (TacticalAllowed(caller, bnet, requester, bot))
                scope.push_back(bot);
        }
    }
    std::sort(scope.begin(), scope.end(),
        [](Player* left, Player* right) { return left->GetName() < right->GetName(); });

    if (Classify(intent) == ExecKind::CcClear)
    {
        if (intent.size() <= 9 && !single && scope.empty())
        {
            addon::Err(requester, intent, hadCandidates ? "not-allowed" : "no-bots",
                hadCandidates ? "you may not order those bots" : "No live owned party bots are controllable.");
            return;
        }
        HandleCcClear(requester, caller, bnet, intent, scope, single);
        return;
    }

    OrderMap const order = MapToOrder(intent, parsed.option);
    if (tactics::IsTacticalVerb(order.verb) && !tactics::Enabled())
    {
        addon::Err(requester, intent, "disabled", "TortoiseBots.Orders.Tactical is 0");
        return;
    }

    bool const needsEnemy =
        intent == "attack" || intent == "interrupt" || intent == "pull" || intent == "pullback" || intent == "focus skull";
    Unit* enemy = needsEnemy ? EnemyTarget(requester) : nullptr;
    if (needsEnemy && intent == "focus skull" && !enemy)
    {
        // TB marks the existing Skull when nothing is selected: the group's
        // mark becomes the order's target.
        if (Group* group = requester->GetGroup())
        {
            ObjectGuid const marked = group->GetTargetIcon(kSkullIcon);
            if (!marked.IsEmpty())
                enemy = ObjectAccessor::GetUnit(*requester, marked);
        }
    }
    if (needsEnemy && !enemy)
    {
        addon::Err(requester, intent, "no-target", "Select a live non-bot target first.");
        return;
    }
    if (needsEnemy && !enemy->IsAlive())
    {
        addon::Err(requester, intent, "dead-target", "Your target is dead. Select a live enemy first.");
        return;
    }
    if (intent == "interrupt" && !enemy->IsNonMeleeSpellCast(true))
    {
        addon::Err(requester, intent, "not-casting", "The target finished casting before the interrupt could start.");
        return;
    }

    if (intent == "interrupt")
    {
        std::string action, reasons;
        Player* picked = tactics::PickInterruptBot(scope, enemy, action, reasons);
        if (!picked)
        {
            addon::Err(requester, intent, "no-interrupt", "No owned bot has a ready interrupt for this target.");
            return;
        }
        h3::OrderSpec spec;
        spec.verb = "interrupt";
        spec.arg = action;
        spec.unit = enemy->GetGUID();
        std::string code, detail;
        uint32 seq = 0;
        if (!h3::QueueOrder(picked, spec, code, detail, seq))
        {
            addon::Err(requester, intent, code.empty() ? "failed" : code, detail);
            return;
        }
        addon::Ack(requester, intent, ScopeName(selectedBot == picked, picked->GetName()), 1, picked->GetName());
        return;
    }

    if (scope.empty())
    {
        addon::Err(requester, intent, hadCandidates ? "not-allowed" : "no-bots",
            hadCandidates ? "you may not order those bots" : "No live owned party bots are controllable.");
        return;
    }
    uint32 queued = 0;
    std::string firstDetail;
    std::string firstCode = "failed";
    for (Player* bot : scope)
    {
        if (order.verb == "release" && bot->IsAlive())
            continue;
        h3::OrderSpec spec;
        spec.verb = order.verb;
        spec.arg = order.arg;
        if (needsEnemy)
            spec.unit = enemy->GetGUID();
        if (Classify(intent) == ExecKind::ComeAt)
        {
            spec.x = requester->GetPositionX();
            spec.y = requester->GetPositionY();
            spec.z = requester->GetPositionZ();
            spec.map = requester->GetMapId();
        }
        std::string code, detail;
        uint32 seq = 0;
        if (h3::QueueOrder(bot, spec, code, detail, seq))
            ++queued;
        else if (firstDetail.empty())
        {
            firstCode = code.empty() ? "failed" : code;
            firstDetail = detail;
        }
    }
    if (!queued)
    {
        addon::Err(requester, intent, firstCode,
            firstDetail.empty() ? "No scoped bot accepted the mature action." : firstDetail);
        return;
    }
    addon::Ack(requester, intent, ScopeName(single && scope.size() == 1, scope.front()->GetName()), queued,
        intent == "stance" ? parsed.option : "");
}
} // namespace
} // namespace action
} // namespace TortoiseBots

void TortoiseBots::action::RegisterAddonVerbsActions()
{
    addon::RegisterVerb("action", HandleActionVerb);
    for (std::string const& intent : SupportedIntents())
        addon::Advertise(AdvertiseToken(intent));
    // The buttons these intents light in the addon (Core.lua PullIntent, the
    // Flee and engage buttons in UI.lua): each token names a gated send the
    // verbs above answer.
    addon::AdvertiseCap("pull-seconds");
    addon::AdvertiseCap("flee");
    addon::AdvertiseCap("engage");
}

#else // no engine: the verb is unknown and the channel answers it so

void TortoiseBots::action::RegisterAddonVerbsActions()
{
}

#endif
