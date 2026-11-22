// The `choice` addon verb (TBF #133 piece c-choice): mirrors TB's `.bot choice`
// (TortoiseBots/commands/Choice.cpp) over the TBM addon channel.
#include "host/TbAddonChoice.h"
#include "host/TbAddonChoicePure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/TbAddonChannel.h"
#include "host/TbAddonPure.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/generic/ClassChoicePolicy.h"
#include "playerbot/strategy/rogue/RogueTriggers.h"
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
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace choice
{
namespace
{
std::uint32_t ChoiceBnet(Player* requester)
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

bool ChoiceAllowed(player::Caller const& caller, std::uint32_t bnet, Player* requester, Player* bot, player::Tier tier)
{
    return bot && bot != requester && bot->IsInWorld() &&
        player::Decide(caller, FactsFor(requester, bot, bnet), tier) == player::Verdict::Allowed;
}

bool ChoiceKnown(PlayerbotAI* ai, Player* bot, int kind, std::string const& word)
{
    if (ai::ChoiceKindUsesItem(kind))
        return ai::HasUsablePoisonItem(ai, bot, ai::ChoicePoisonItemIds(word));
    return ai->HasSpell(ai::ChoiceSpell(kind, word));
}

void SendChoiceLine(Player* requester, Player* bot, PlayerbotAI* ai, int kind)
{
    std::string const stored =
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(kind))->Get();
    std::string known;
    for (int i = 0; i < ai::ChoiceWordCount(kind); ++i)
    {
        std::string const word = ai::ChoiceWord(kind, i);
        if (!ChoiceKnown(ai, bot, kind, word))
            continue;
        known += (known.empty() ? "" : "+") + word;
    }
    addon::Send(requester, FormatChoiceLine(bot->GetName(), kind, stored, known));
}

void HandleChoiceVerb(Player* requester, std::string_view args)
{
    ParsedChoice const parsed = ParseChoiceArgs(args);
    if (!parsed.ok)
    {
        addon::Err(requester, parsed.intent, "invalid", kChoiceUsage);
        return;
    }
    if (!requester || !requester->IsInWorld())
    {
        addon::Err(requester, parsed.intent, "requester", "Requester must be in world.");
        return;
    }

    std::uint32_t const bnet = ChoiceBnet(requester);
    player::Caller caller;
    caller.gm = requester->GetSession() && requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();
    player::Tier const tier = parsed.status ? player::Tier::Tactical : player::Tier::Owner;

    std::vector<Player*> bots;
    if (parsed.scopeIsParty)
    {
        if (Group* group = requester->GetGroup())
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                Player* member = ObjectAccessor::FindPlayer(slot.guid);
                if (ChoiceAllowed(caller, bnet, requester, member, tier))
                    bots.push_back(member);
            }
        }
        else
        {
            for (Player* bot : BotMgr::Instance().BotsOfMaster(requester->GetGUID()))
            {
                if (ChoiceAllowed(caller, bnet, requester, bot, tier))
                    bots.push_back(bot);
            }
        }
        std::sort(bots.begin(), bots.end(),
            [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
        if (bots.empty())
        {
            addon::Err(requester, parsed.intent, "no-bot", "No live owned party bots are controllable.");
            return;
        }
    }
    else
    {
        Player* found = nullptr;
        for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        {
            Player* candidate = ObjectAccessor::FindPlayer(guid);
            if (!candidate || !candidate->IsInWorld() || candidate == requester)
                continue;
            if (addon::pure::Lower(candidate->GetName()) == parsed.scope)
            {
                found = candidate;
                break;
            }
        }
        if (!found)
        {
            addon::Err(requester, parsed.intent, "no-bot", "Unknown or uncontrollable bot name.");
            return;
        }
        player::Verdict const verdict = player::Decide(caller, FactsFor(requester, found, bnet), tier);
        if (verdict != player::Verdict::Allowed)
        {
            addon::Err(requester, parsed.intent, player::CodeOf(verdict), "You may not configure that bot.");
            return;
        }
        bots.push_back(found);
    }

    std::string const ackScope = parsed.scopeIsParty ? "party" : ("bot:" + bots.front()->GetName());
    std::uint32_t matched = 0;
    bool refused = false;
    std::string const spell = parsed.status ? std::string() : ai::ChoiceSpell(parsed.kind, parsed.word3);

    for (Player* bot : bots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        if (parsed.status)
        {
            std::vector<int> const kinds = ai::ChoiceKindsForClass(bot->GetClass());
            for (int rowKind : kinds)
                SendChoiceLine(requester, bot, ai, rowKind);
            if (!kinds.empty())
                ++matched;
            continue;
        }
        if (bot->GetClass() != ai::ChoiceKindClassId(parsed.kind))
            continue;
        if (!spell.empty() && !ChoiceKnown(ai, bot, parsed.kind, parsed.word3))
        {
            bool const item = ai::ChoiceKindUsesItem(parsed.kind);
            std::string const msg = bot->GetName() + std::string(item ? " has no usable " : " has not trained ") + spell + ".";
            addon::Err(requester, parsed.intent, item ? "no-item" : "unknown-spell", msg);
            refused = true;
            continue;
        }
        std::string const previous =
            ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(parsed.kind))->Get();
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::ChoiceOrderKey(parsed.kind))->Set(parsed.word3);
        // The automatic aura row only fires with no aura up, so drop the
        // ordered one when returning to auto so it can take over.
        if (parsed.kind == ai::CHOICE_AURA && parsed.word3 == "auto" && !previous.empty() && previous != "auto")
            ai->RemoveAura(ai::ChoiceSpell(parsed.kind, previous));
        sPlayerbotDbStore.Save(ai);
        SendChoiceLine(requester, bot, ai, parsed.kind);
        ++matched;
    }

    if (parsed.status)
    {
        addon::Ack(requester, parsed.intent, ackScope, matched, "-");
        return;
    }
    if (!matched)
    {
        if (!refused)
            addon::Err(requester, parsed.intent, "no-bot", "No bots of that kind's class in scope.");
        return;
    }
    addon::Ack(requester, parsed.intent, ackScope, matched, parsed.word2 + "=" + parsed.word3);
}
} // namespace
} // namespace choice
} // namespace TortoiseBots

void TortoiseBots::choice::RegisterAddonVerbsChoice()
{
    addon::RegisterVerb("choice", &HandleChoiceVerb);
    addon::AdvertiseCap("choice");
}

#else // no engine: the verb is unknown and the channel answers it so

void TortoiseBots::choice::RegisterAddonVerbsChoice()
{
}

#endif
