// The `inv`/`inventory` and `item` addon verbs (TBF #134 pieces P134-A/P134-B):
// TB's `.bot inv` snapshot (TortoiseBots/commands/Inventory.cpp:158-228) and
// `.bot item <bot> equip|unequip|give` server-side moves (:353-600) over the
// TBM channel, with TB's TBM:INV_* and ACTION_ACK/ERR lines (BotPanel.lua).
// Permission is the player tier Owner per target bot
// (host/player/TbPlayerAuthPure.h, COMMON-134: Owner for moves on own bots,
// GM all). `give` moves the item into the owner's bags directly while no
// trade window is involved: TB's give goes through the trade window
// (Inventory.cpp:560-600, "pending" reply, addon retries on TRADE_SHOW),
// which headless bots cannot see; the direct move is Forever's acceptance
// path and i-trade owns the real trade window (`item <bot> trade`).
#include "host/TbAddonInv.h"
#include "host/TbAddonInvPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"
#include "host/TbAddonChannel.h"
#include "host/TbAddonTrade.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"
#include "runtime/PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/actions/EquipAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

#include "Bag.h"
#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace inv
{
namespace
{
std::uint32_t InvBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts InvFacts(Player* requester, Player* bot, std::uint32_t bnet)
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
        player::Decide(caller, InvFacts(requester, bot, bnet), player::Tier::Owner) ==
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

Player* FindOwnedBot(Player* requester, std::string const& wanted)
{
    if (!requester)
        return nullptr;
    player::Caller caller;
    caller.gm = requester->GetSession() && requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = InvBnet(requester);
    caller.guid = requester->GetGUID().GetCounter();
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || LowerName(bot->GetName()) != wanted)
            continue;
        if (!OwnerAllowed(caller, caller.account, requester, bot))
            continue;
        return bot;
    }
    return nullptr;
}

// TB's InventoryItemFlags (Inventory.cpp:130-156): e/u/b from the live item.
std::string LiveFlags(PlayerbotAI* ai, Player* bot, Player* requester, Item* item)
{
    bool equippable = false;
    bool upgrade = false;
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    if (proto && proto->GetInventoryType() != INVTYPE_NON_EQUIP && bot->CanUseItem(proto) == EQUIP_ERR_OK)
    {
        equippable = true;
        if (ai && ai->GetAiObjectContext())
        {
            ai::Value<ai::ItemUsage>* value = ai->GetAiObjectContext()->GetValue<ai::ItemUsage>(
                "item usage", ai::ItemQualifier(item).GetQualifier());
            upgrade = value && value->Get() == ai::ItemUsage::ITEM_USAGE_EQUIP;
        }
    }
    return InvFlags(equippable, upgrade, item && !item->CanBeTraded(false, true));
}
} // namespace

void SendInventorySnapshot(Player* requester, Player* bot)
{
    if (!requester || !bot)
        return;
    PlayerbotAI* ai = AiOf(bot);
    std::string const name = addon::Safe(bot->GetName());
    addon::Send(requester, FormatInvBegin(name, bot->GetMoney()));
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        addon::Send(requester,
            FormatInvEq(slot, item->GetEntry(), std::uint32_t(item->m_itemData->Durability),
                std::uint32_t(item->m_itemData->MaxDurability)));
    }
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        Bag* bag = bagItem ? bagItem->ToBag() : nullptr;
        if (!bag)
            continue;
        addon::Send(requester, FormatInvEqBag(bagSlot, bag->GetEntry(), bag->GetBagSize()));
    }
    std::uint32_t free = 0;
    std::uint32_t total = 0;
    auto emit = [&](std::uint32_t bag, std::uint32_t slot, Item* item)
    {
        ++total;
        if (!item)
        {
            ++free;
            return;
        }
        addon::Send(requester,
            FormatInvItem(bag, slot, item->GetEntry(), item->GetCount(), LiveFlags(ai, bot, requester, item)));
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        emit(INVENTORY_SLOT_BAG_0, slot, bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        Bag* bag = bagItem ? bagItem->ToBag() : nullptr;
        if (!bag)
            continue;
        for (std::uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
            emit(bagSlot, slot, bag->GetItemByPos(slot));
    }
    addon::Send(requester, FormatInvEnd(name, free, total));
}

void HandleInvVerb(Player* requester, std::string_view args)
{
    InvRequest parsed = ParseInvRequest(args);
    if (!parsed.ok)
    {
        addon::Err(requester, "inv", parsed.error, parsed.message);
        return;
    }
    Player* bot = FindOwnedBot(requester, parsed.bot);
    if (!bot)
    {
        addon::Err(requester, "inv", "no-bot", "Unknown or uncontrollable bot name.");
        return;
    }
    SendInventorySnapshot(requester, bot);
}

void HandleItemVerb(Player* requester, std::string_view args)
{
    ItemRequest parsed = ParseItemRequest(args);
    if (!parsed.ok)
    {
        addon::Err(requester, parsed.intent, parsed.error, parsed.message);
        return;
    }
    if (!requester)
        return;
    // `trade` is the trade-window piece's (i-trade): it re-resolves the bot by
    // live name and drives the real window. A false return means it declined
    // (lost session), so the verb still answers.
    if (parsed.op == ItemRequest::Op::Trade)
    {
        Player* bot = FindOwnedBot(requester, parsed.bot);
        if (!bot)
        {
            addon::Err(requester, parsed.intent, "no-bot", "Unknown or uncontrollable bot name.");
            return;
        }
        if (!trade::HandleItemTrade(requester, bot->GetName()))
            addon::Err(requester, parsed.intent, "unsupported", "Trade is not available right now.");
        return;
    }
    Player* bot = FindOwnedBot(requester, parsed.bot);
    if (!bot)
    {
        addon::Err(requester, parsed.intent, "no-bot", "Unknown or uncontrollable bot name.");
        return;
    }
    PlayerbotAI* ai = AiOf(bot);
    if (!ai)
    {
        addon::Err(requester, parsed.intent, "no-bot", "Unknown or uncontrollable bot name.");
        return;
    }
    if (!bot->IsAlive())
    {
        addon::Err(requester, parsed.intent, "dead", "The bot must be alive.");
        return;
    }
    std::string const scope = ItemScope(addon::Safe(bot->GetName()));

    std::uint8_t const bag = static_cast<std::uint8_t>(parsed.bag);
    std::uint8_t const slot = static_cast<std::uint8_t>(parsed.slot);
    bool const equipped = Player::IsEquipmentPos(bag, slot);
    if (!equipped && !Player::IsInventoryPos(bag, slot))
    {
        addon::Err(requester, parsed.intent, "invalid", "Only carried items can be ordered.");
        return;
    }
    Item* item = bot->GetItemByPos(bag, slot);
    if (!item)
    {
        addon::Err(requester, parsed.intent, "no-item", "That item is no longer there; refresh the bag view.");
        SendInventorySnapshot(requester, bot);
        return;
    }
    std::uint32_t const itemId = item->GetEntry();

    if (parsed.op == ItemRequest::Op::Equip)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (parsed.hasTargetSlot)
        {
            std::uint8_t const targetSlot = parsed.targetSlot;
            std::string_view slotErr;
            std::uint32_t const invType = proto ? proto->GetInventoryType() : INVTYPE_NON_EQUIP;
            if (!ValidateEquipTargetSlot(invType, targetSlot, bot->CanDualWield(), slotErr))
            {
                addon::Err(requester, parsed.intent, "cannot-equip", std::string(slotErr));
                return;
            }
            std::uint16_t dest = 0;
            if (bot->CanEquipItem(targetSlot, dest, item, true) != EQUIP_ERR_OK || (dest & 0xFF) != targetSlot)
            {
                addon::Err(requester, parsed.intent, "cannot-equip", "The bot cannot equip this item there.");
                return;
            }
            bot->SwapItem(item->GetPos(), (std::uint16_t(INVENTORY_SLOT_BAG_0) << 8) | targetSlot);
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot) != item)
                addon::Err(requester, parsed.intent, "failed", "The equip was refused (combat, level, or skill).");
            else
            {
                ai::EquipAction::PinPlayerEquippedItemIfClaimed(ai, requester, item);
                addon::Ack(requester, parsed.intent, scope, 1, std::to_string(itemId));
            }
            SendInventorySnapshot(requester, bot);
            return;
        }
        if (equipped || !proto || proto->GetInventoryType() == INVTYPE_NON_EQUIP ||
            bot->CanUseItem(proto) != EQUIP_ERR_OK)
        {
            addon::Err(requester, parsed.intent, "cannot-equip", "The bot cannot equip this item.");
            return;
        }
        ai::EquipAction::EquipItem(ai, requester, item, true);
        bool const done = proto->GetInventoryType() == INVTYPE_AMMO
            ? bot->GetAmmoId() == itemId
            : item->GetBagSlot() == INVENTORY_SLOT_BAG_0 && item->GetSlot() < INVENTORY_SLOT_ITEM_START;
        if (!done)
            addon::Err(requester, parsed.intent, "failed", "The equip was refused (combat, level, or skill).");
        else
        {
            ai::EquipAction::PinPlayerEquippedItemIfClaimed(ai, requester, item);
            addon::Ack(requester, parsed.intent, scope, 1, std::to_string(itemId));
        }
        SendInventorySnapshot(requester, bot);
        return;
    }

    if (parsed.op == ItemRequest::Op::Unequip)
    {
        if (!equipped)
        {
            addon::Err(requester, parsed.intent, "not-equipped", "That item is not equipped.");
            return;
        }
        std::uint16_t const pos = item->GetPos();
        if (bot->CanUnequipItem(pos, false) != EQUIP_ERR_OK)
        {
            addon::Err(requester, parsed.intent, "failed", "The item cannot be removed right now.");
            SendInventorySnapshot(requester, bot);
            return;
        }
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
        {
            addon::Err(requester, parsed.intent, "failed", "No free bag space for it.");
            SendInventorySnapshot(requester, bot);
            return;
        }
        bot->RemoveItem(bag, slot, true);
        bot->StoreItem(dest, item, true);
        if (bot->GetItemByPos(bag, slot) == item)
            addon::Err(requester, parsed.intent, "failed", "The item cannot be removed right now.");
        else
        {
            if (ai && ai->GetAiObjectContext())
            {
                ai::Value<ai::ItemUsage>* value = ai->GetAiObjectContext()->GetValue<ai::ItemUsage>(
                    "item usage", ai::ItemQualifier(item).GetQualifier());
                if (value)
                    value->Reset();
            }
            addon::Ack(requester, parsed.intent, scope, 1, std::to_string(itemId));
        }
        SendInventorySnapshot(requester, bot);
        return;
    }

    if (equipped)
    {
        addon::Err(requester, parsed.intent, "equipped", "Unequip the item first.");
        return;
    }
    // TB's give rides an open trade window (i-trade answers there, no snapshot:
    // nothing moved yet). With no window open the direct move below is the path.
    if (bot->GetTrader() == requester)
    {
        trade::PlaceItemInOpenTrade(requester, bot, item, bag, slot);
        return;
    }
    if (item->IsInTrade())
    {
        addon::Err(requester, parsed.intent, "busy", "That item is in a trade window.");
        return;
    }
    if (!item->CanBeTraded(false, true))
    {
        addon::Err(requester, parsed.intent, "no-trade", "This item cannot be traded (soulbound or quest item).");
        return;
    }
    ItemPosCountVec dest;
    if (requester->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
    {
        addon::Err(requester, parsed.intent, "failed", "You have no bag space for it.");
        return;
    }
    bot->RemoveItem(bag, slot, true);
    requester->StoreItem(dest, item, true);
    if (item->GetOwner() != requester)
    {
        addon::Err(requester, parsed.intent, "failed", "The give was refused.");
        SendInventorySnapshot(requester, bot);
        return;
    }
    bot->SaveToDB();
    requester->SaveToDB();
    addon::Ack(requester, parsed.intent, scope, 1, std::to_string(itemId));
    SendInventorySnapshot(requester, bot);
}

void RegisterAddonVerbsInv()
{
    addon::RegisterVerb("inv", &HandleInvVerb);
    addon::RegisterVerb("inventory", &HandleInvVerb);
    addon::RegisterVerb("item", &HandleItemVerb);
    // BotPanel shows the bag button once the server answers with this.
    addon::AdvertiseCap("inventory");
}

} // namespace inv
} // namespace TortoiseBots

#else // no engine: the verbs are unknown and the channel answers it so

void TortoiseBots::inv::RegisterAddonVerbsInv()
{
}

void TortoiseBots::inv::SendInventorySnapshot(Player*, Player*)
{
}

void TortoiseBots::inv::HandleItemVerb(Player*, std::string_view)
{
}

#endif
