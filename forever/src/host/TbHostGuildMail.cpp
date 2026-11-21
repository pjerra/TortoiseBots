#include "host/TbHostGuildMail.h"

#include "TbBotMgr.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTown.h"
#include "host/pool/TbPoolRegistry.h"

#include "AccountMgr.h"
#include "Bag.h"
#include "CharacterCache.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "playerbot/policy/ah/AhSellPolicy.h"
#include "playerbot/policy/world/GuildMailPolicy.h"
#include "playerbot/policy/world/PoolRegistryPolicy.h"

#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guild_mail
{
namespace
{

using namespace TortoiseBots::world;

constexpr float kMaxMailboxWalkDistYd = 300.0f;
constexpr uint64_t kMailboxWalkTimeoutMs = 45000;

struct BotGuildMailState
{
    uint64_t nowMs = 0;
    uint64_t nextCheckMs = 0;
    uint64_t goldCooldownUntilMs = 0;
    uint64_t matsCooldownUntilMs = 0;
    bool walkingToMailbox = false;
    ObjectGuid mailboxGuid;
    uint64_t walkStartedAtMs = 0;
};

std::unordered_map<ObjectGuid, BotGuildMailState>& States()
{
    static std::unordered_map<ObjectGuid, BotGuildMailState> map;
    return map;
}

template <typename Fn>
void ForEachBagItem(Player* bot, Fn&& fn)
{
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            fn(item);
    }
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            if (Item* item = bag->GetItemByPos(slot))
                fn(item);
        }
    }
}

void CollectGuildMembers(Guild const* guild, std::vector<GuildMemberMailView>& outMembers, std::vector<Player*>& outOnlineBots, bool& outHasRealPlayers)
{
    outMembers.clear();
    outOnlineBots.clear();

    auto const& mgr = TortoiseBots::BotMgr::Instance();
    for (auto const& [memberGuid, member] : guild->GetMembers())
    {
        Player* mp = ObjectAccessor::FindPlayer(memberGuid);
        bool isBot = mgr.IsBot(memberGuid);
        if (!isBot && !mp)
        {
            uint32_t const accountId = sCharacterCache->GetCharacterAccountIdByGuid(memberGuid);
            if (accountId != 0)
            {
                std::string accountName;
                if (TortoiseBots::pool::AccountRegistered(accountId) ||
                    (AccountMgr::GetName(accountId, accountName) && IsPoolAccountName(accountName)))
                {
                    isBot = true;
                }
            }
        }

        GuildMemberMailView view;
        view.guidLow = memberGuid.GetCounter();
        view.name = member.GetName();
        view.isBot = isBot;
        view.isOnline = (mp != nullptr && mp->IsInWorld());
        view.moneyCopper = mp ? mp->GetMoney() : 0;
        outMembers.push_back(view);

        if (mp && mp->IsInWorld() && isBot)
            outOnlineBots.push_back(mp);
    }
    outHasRealPlayers = HasRealGuildPlayers(outMembers);
}

struct GuildMailPlan
{
    bool hasWork = false;
    bool sendGold = false;
    GuildGoldGiftPlan goldPlan;
    bool sendMats = false;
    std::string matsRecipientName;
    std::vector<ObjectGuid> attachments;
};

GuildMailPlan PrepareGuildMailPlan(Player* bot, BotGuildMailState const& st)
{
    GuildMailPlan plan;
    uint64_t const guildId = bot->GetGuildId();
    Guild const* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    if (!guild)
        return plan;

    bool const allowMixed = TortoiseBots::BotMgr::Instance().GuildMailAllowMixed();
    std::vector<GuildMemberMailView> members;
    std::vector<Player*> onlineBots;
    bool hasRealPlayers = false;
    CollectGuildMembers(guild, members, onlineBots, hasRealPlayers);

    if (hasRealPlayers && !allowMixed)
        return plan;

    // 1. Gold to poorest online bot guildmate.
    if (st.nowMs >= st.goldCooldownUntilMs)
    {
        plan.goldPlan = PlanGuildGoldGift(
            bot->GetGUID().GetCounter(),
            true,
            bot->GetMoney(),
            hasRealPlayers,
            allowMixed,
            members);
        if (plan.goldPlan.shouldSend)
        {
            plan.hasWork = true;
            plan.sendGold = true;
            return plan;
        }
    }

    // 2. Crafting materials to crafters who use them.
    if (st.nowMs >= st.matsCooldownUntilMs && bot->HasEnoughMoney(kGuildMailPostageCopper))
    {
        GuildMemberMailView const* chosenRecipient = nullptr;
        std::vector<ObjectGuid> attachments;

        ForEachBagItem(bot, [&](Item* item)
        {
            if (attachments.size() >= kMaxGuildMailAttachments)
                return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                return;

            uint32_t const itemId = proto->GetId();
            GuildMatItemCandidate cand;
            cand.itemId = itemId;
            cand.itemClass = proto->GetClass();
            cand.isSoulbound = item->IsSoulBound();
            cand.isEquipped = false;
            cand.isProfessionTool = ai::IsAhProfessionTool(itemId, false);
            cand.neededByOrder = tbhost::guild_orders::IsItemNeededByGuildOrder(guildId, itemId);
            cand.usedBySender = tbhost::prof::IsReagentForBotProfessions(bot, itemId);
            if (!CanMailGuildMaterial(cand))
                return;

            if (!chosenRecipient)
            {
                std::vector<GuildMemberMailView> crafters;
                for (Player* memberPlayer : onlineBots)
                {
                    if (!memberPlayer || memberPlayer == bot)
                        continue;
                    if (!tbhost::prof::IsReagentForBotProfessions(memberPlayer, itemId))
                        continue;
                    GuildMemberMailView v;
                    v.guidLow = memberPlayer->GetGUID().GetCounter();
                    v.name = memberPlayer->GetName();
                    v.isBot = true;
                    v.isOnline = true;
                    v.moneyCopper = memberPlayer->GetMoney();
                    crafters.push_back(v);
                }
                GuildMemberMailView const* picked = PickMaterialRecipient(
                    bot->GetGUID().GetCounter(), true, hasRealPlayers, allowMixed, crafters);
                if (!picked)
                    return;
                // Store pointer into `members` so it outlives `crafters`.
                for (GuildMemberMailView const& m : members)
                {
                    if (m.guidLow == picked->guidLow)
                    {
                        chosenRecipient = &m;
                        break;
                    }
                }
                if (chosenRecipient)
                    attachments.push_back(item->GetGUID());
                return;
            }

            Player* recipPlayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(chosenRecipient->guidLow));
            if (recipPlayer && tbhost::prof::IsReagentForBotProfessions(recipPlayer, itemId))
                attachments.push_back(item->GetGUID());
        });

        if (chosenRecipient && !attachments.empty())
        {
            uint64_t const postage = kGuildMailPostageCopper * attachments.size();
            if (bot->HasEnoughMoney(postage))
            {
                plan.hasWork = true;
                plan.sendMats = true;
                plan.matsRecipientName = chosenRecipient->name;
                plan.attachments = std::move(attachments);
                return plan;
            }
        }
    }

    return plan;
}

void ExecuteGuildMailPlan(Player* bot, GameObject* mailbox, BotGuildMailState& st, GuildMailPlan const& plan)
{
    if (plan.sendGold)
    {
        std::string failReason;
        if (tbhost::guild::SendMail(bot, mailbox->GetGUID(), plan.goldPlan.recipientName,
                "Guild Aid", "For supplies.", plan.goldPlan.copper, {}, &failReason))
        {
            st.goldCooldownUntilMs = st.nowMs + kGuildMailGoldCooldownMs;
            st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
            TC_LOG_INFO("tortoisebots", "[W9] guild-mail-gold bot={} to={} copper={}",
                bot->GetName(), plan.goldPlan.recipientName, plan.goldPlan.copper);
            return;
        }
        st.nextCheckMs = st.nowMs + kGuildMailFailBackoffMs;
        return;
    }

    if (plan.sendMats && !plan.attachments.empty())
    {
        std::string failReason;
        if (tbhost::guild::SendMail(bot, mailbox->GetGUID(), plan.matsRecipientName,
                "Guild Materials", "Crafting supplies.", 0, plan.attachments, &failReason))
        {
            st.matsCooldownUntilMs = st.nowMs + kGuildMailMatsCooldownMs;
            st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
            TC_LOG_INFO("tortoisebots", "[W9] guild-mail-mats bot={} to={} items={}",
                bot->GetName(), plan.matsRecipientName, uint32_t(plan.attachments.size()));
            return;
        }
        st.nextCheckMs = st.nowMs + kGuildMailFailBackoffMs;
        return;
    }

    st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
}

} // namespace

void TickGuildMail(Player* bot, uint32_t diffMs)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported() || bot->GetGuildId() == 0)
        return;

    BotGuildMailState& st = States()[bot->GetGUID()];
    st.nowMs += diffMs;

    if (st.walkingToMailbox)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->duel != nullptr ||
            (st.nowMs - st.walkStartedAtMs) >= kMailboxWalkTimeoutMs)
        {
            st.walkingToMailbox = false;
            st.nextCheckMs = st.nowMs + kGuildMailFailBackoffMs;
            return;
        }

        GameObject* mailbox = ObjectAccessor::GetGameObject(*bot, st.mailboxGuid);
        if (!mailbox || !mailbox->IsInWorld() || mailbox->GetMap() != bot->FindMap())
        {
            st.walkingToMailbox = false;
            st.nextCheckMs = st.nowMs + kGuildMailFailBackoffMs;
            return;
        }

        if (bot->GetGameObjectIfCanInteractWith(mailbox->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
        {
            bot->StopMoving();
            st.walkingToMailbox = false;
            GuildMailPlan const plan = PrepareGuildMailPlan(bot, st);
            if (plan.hasWork)
                ExecuteGuildMailPlan(bot, mailbox, st, plan);
            else
                st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
        }
        return;
    }

    if (!bot->IsAlive() || bot->IsInCombat() || bot->isMoving() ||
        bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->duel != nullptr)
        return;
    if (tbhost::prof::HasActiveErrand(bot) || tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter()).has_value())
        return;
    if (st.nowMs < st.nextCheckMs)
        return;
    if (st.nowMs < st.goldCooldownUntilMs && st.nowMs < st.matsCooldownUntilMs)
    {
        st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
        return;
    }

    GuildMailPlan const plan = PrepareGuildMailPlan(bot, st);
    if (!plan.hasWork)
    {
        st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
        return;
    }

    GameObject* mailbox = tbhost::town::FindNearestMailbox(bot);
    if (mailbox)
    {
        ExecuteGuildMailPlan(bot, mailbox, st, plan);
        return;
    }

    GameObject* farMailbox = tbhost::town::FindNearestMailbox(bot, kMaxMailboxWalkDistYd);
    if (!farMailbox)
    {
        st.nextCheckMs = st.nowMs + kGuildMailCheckIntervalMs;
        return;
    }

    st.walkingToMailbox = true;
    st.mailboxGuid = farMailbox->GetGUID();
    st.walkStartedAtMs = st.nowMs;
    bot->GetMotionMaster()->MovePoint(0, farMailbox->GetPositionX(), farMailbox->GetPositionY(), farMailbox->GetPositionZ());
}

void ClearBotGuildMail(ObjectGuid const& botGuid)
{
    States().erase(botGuid);
}

} // namespace guild_mail
} // namespace tbhost
