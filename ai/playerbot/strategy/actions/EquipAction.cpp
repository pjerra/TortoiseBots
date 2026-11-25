
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/playerbot.h"
#include "EquipAction.h"

#include <set>

#include "playerbot/policy/gear/DualWieldPolicy.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "../../../runtime/ClaimLifecycle.h"
#include "../../../runtime/policy/hire/ClaimedBotPolicy.h"
#include "../../../runtime/HireLifecycle.h"
#include "../../../runtime/BotGearService.h"
#include "../../../../runtime/AltBotService.h"
#include "../../../../runtime/AltBotPolicy.h"
#include "../../../../runtime/GearEffectiveLevelPolicy.h"
#include "../../../runtime/policy/hire/HireSourceGuardPolicy.h"
#include "../../../runtime/policy/hire/HireGearPolicy.h"
#include "playerbot/TbCompatW2.h"
#include "playerbot/TbCompatPoolGear.h"

using namespace ai;

namespace
{
bool IsItemCurrentlyEquipped(Player* bot, Item* item)
{
    if (!bot || !item || !GetItemProtoCompat(item))
        return false;
    if (GetItemProtoCompat(item)->InventoryType == INVTYPE_AMMO)
        return PlayerAmmoIdCompat(bot) == GetItemProtoCompat(item)->ItemId;
    if (GetItemProtoCompat(item)->Class == ITEM_CLASS_CONTAINER || GetItemProtoCompat(item)->Class == ITEM_CLASS_QUIVER)
        return item->GetBagSlot() == INVENTORY_SLOT_BAG_0 &&
            item->GetSlot() >= INVENTORY_SLOT_BAG_START && item->GetSlot() < INVENTORY_SLOT_BAG_END;
    return item->GetBagSlot() == INVENTORY_SLOT_BAG_0 && item->GetSlot() < INVENTORY_SLOT_ITEM_START;
}

bool SetForceItemUsageIfChanged(PlayerbotAI* ai, Item* item, ForceItemUsage usage, bool onlyIfNone)
{
    if (!ai || !item || !GetItemProtoCompat(item))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return false;
    uint32 const itemId = GetItemProtoCompat(item)->ItemId;
    ForceItemUsage const current = AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", itemId, ForceItemUsage::FORCE_USAGE_NONE);
    if (onlyIfNone && current != ForceItemUsage::FORCE_USAGE_NONE)
        return false;
    if (current == usage)
        return false;
    SET_AI_VALUE2(ForceItemUsage, "force item usage", itemId, usage);
    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
    return true;
}
}

bool EquipAction::PinPlayerEquippedItemIfClaimed(PlayerbotAI* ai, Player* requester, Item* item)
{
    if (!ai || !requester || !item || !GetItemProtoCompat(item))
        return false;
    Player* bot = ai->GetBot();
    if (!bot || !IsItemCurrentlyEquipped(bot, item))
        return false;
    bool const isClaimed = TortoiseBots::ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow());
    bool const isOwnedOrHired = !sRandomBotFacade.IsRandomBot(bot) ||
        TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    bool const isOwner = (requester == ai->GetMaster()) ||
        TortoiseBots::ClaimLifecycle::Instance().IsClaimedByPlayer(bot->GetGUIDLow(), requester->GetGUIDLow());
    if (!TortoiseBots::ShouldPinPlayerEquippedItem(isClaimed, isOwner, isOwnedOrHired))
        return false;
    if (!SetForceItemUsageIfChanged(ai, item, ForceItemUsage::FORCE_USAGE_EQUIP, false))
        return false;
    if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
        sPlayerbotDbStore.Save(ai);
    return true;
}

bool EquipAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    if (text == "?")
    {
        ListItems(requester);
        return true;
    }

    uint8 targetSlot = NULL_SLOT;
    if (text.find("mh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_MAINHAND;
        text = text.substr(3);
    }
    else if (text.find("oh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_OFFHAND;
        text = text.substr(3);
    }

    ItemIds ids = chat->parseItems(text);
    if (ids.empty())
    {
        //Get items based on text.
        std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

        //Sort items on itemLevel descending.
        found.sort([](Item* i, Item* j) {return GetItemProtoCompat(i)->ItemLevel > GetItemProtoCompat(j)->ItemLevel; });

        std::vector< uint16> dests;
        for (auto& item : found)
        {
            uint32 itemId = GetItemProtoCompat(item)->ItemId;
            if (std::find(ids.begin(), ids.end(), itemId) != ids.end())
            {
                continue;
            }

            uint16 dest;
            InventoryResult msg = bot->CanEquipItem(targetSlot, dest, item, true);

            if (msg != EQUIP_ERR_OK)
            {
                continue;
            }

            if (std::find(dests.begin(), dests.end(), dest) != dests.end())
            {
                continue;
            }

            dests.push_back(dest);
            ids.insert(itemId);
        }
    }

    if (targetSlot != NULL_SLOT)
    {
        EquipItemsToSlot(requester, ids, targetSlot);
    }
    else
    {
        EquipItems(requester, ids);
    }
    return true;
}

void EquipAction::ListItems(Player* requester)
{
    ai->TellPlayer(requester, "=== Equip ===");

    std::map<uint32, int> items;
    std::map<uint32, bool> soulbound;
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem)
            {
                items[GetItemProtoCompat(pItem)->ItemId] += pItem->GetCount();
            }
        }
    }

    ai->InventoryTellItems(requester, items, soulbound);
}

void EquipAction::EquipItems(Player* requester, ItemIds ids)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        EquipItem(requester, &visitor);
    }
}

void EquipAction::EquipItemsToSlot(Player* requester, ItemIds ids, uint8 targetSlot)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        std::list<Item*> items = visitor.GetResult();
        if (!items.empty())
        {
            Item* item = *items.begin();
            EquipItemToSlot(requester, item, targetSlot);
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot) == item)
                PinPlayerEquippedItemIfClaimed(ai, requester, item);
        }
    }
}

void EquipAction::EquipItem(Player* requester, FindItemVisitor* visitor)
{
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
	if (!items.empty())
    {
        Item* item = *items.begin();
        EquipItem(ai, requester, item);
        PinPlayerEquippedItemIfClaimed(ai, requester, item);
    }
}

// Return the bag slot with smallest bag. A quiver may only replace a
// quiver or take an empty slot - never a plain bag slot (core allows only
// one quiver; SwapItem bypasses the uniqueness veto). A plain bag never
// evicts a quiver/pouch or a soul bag (ammo/shards would strand). A soul
// bag replaces a soul bag (size upgrade) or takes an empty slot; the first
// one (none equipped) may also evict the smallest plain bag, whose contents
// the caller moves out first - otherwise a bot seeded with four plain bags
// could never equip a soul bag. When every slot is excluded, the result is
// 0 (no slot) so the caller fails explicitly instead of corrupting the row.
uint8 EquipAction::GetSmallestBagSlot(Player* bot)
{
    return GetSmallestBagSlot(bot, ITEM_CLASS_CONTAINER, ITEM_SUBCLASS_CONTAINER);
}

uint8 EquipAction::GetSmallestBagSlot(Player* bot, uint32 newClass, uint32 newSubClass)
{
    bool const newIsQuiver = (newClass == ITEM_CLASS_QUIVER);
    bool const newIsSoulBag = (newClass == ITEM_CLASS_CONTAINER && newSubClass == ITEM_SUBCLASS_SOUL_CONTAINER);
    bool const newIsPlain = (newClass == ITEM_CLASS_CONTAINER && newSubClass == ITEM_SUBCLASS_CONTAINER);
    bool haveSoulBag = false;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END && !haveSoulBag; ++bag)
    {
        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        ItemPrototype const* proto = equipped ? GetItemProtoCompat(equipped) : nullptr;
        if (proto && proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_SOUL_CONTAINER)
            haveSoulBag = true;
    }
    int8 curBag = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        const Bag* const pBag = (Bag*)bagItem;
        if (pBag)
        {
            ItemPrototype const* oldProto = bagItem ? GetItemProtoCompat(bagItem) : nullptr;
            bool const oldIsQuiver = oldProto && oldProto->Class == ITEM_CLASS_QUIVER;
            bool const oldIsSoulBag = oldProto && oldProto->Class == ITEM_CLASS_CONTAINER &&
                oldProto->SubClass == ITEM_SUBCLASS_SOUL_CONTAINER;
            bool const oldIsPlainBag = oldProto && oldProto->Class == ITEM_CLASS_CONTAINER &&
                oldProto->SubClass == ITEM_SUBCLASS_CONTAINER;
            // A quiver may only target a quiver slot: with no quiver
            // equipped it must find a free slot elsewhere (caller checks
            // empty slots first). Never a plain-bag slot.
            if (newIsQuiver && !oldIsQuiver)
                continue;
            // A plain bag must not evict a quiver/pouch or a soul bag.
            if (newIsPlain && (oldIsQuiver || oldIsSoulBag))
                continue;
            // A soul bag replaces a soul bag (size upgrade path), takes an
            // empty slot, or - as the first soul bag - evicts the smallest
            // plain bag. Never a quiver slot (ammo would strand).
            if (newIsSoulBag && !oldIsSoulBag && (!oldIsPlainBag || haveSoulBag))
                continue;
            if (curBag > 0 && curSlots < pBag->GetBagSize())
            {
                continue;
            }

            curBag = bag;
            curSlots = pBag->GetBagSize();
        }
        else
        {
            // Empty slot: free for plain bags and soul bags; a quiver may
            // only take it when no quiver is equipped (core vetoes a
            // second one - CanEquipItem re-checks at the swap below).
            if (newIsQuiver)
            {
                bool haveQuiver = false;
                for (uint8 other = INVENTORY_SLOT_BAG_START; other < INVENTORY_SLOT_BAG_END; ++other)
                {
                    Item* otherItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, other);
                    if (otherItem && GetItemProtoCompat(otherItem) && GetItemProtoCompat(otherItem)->Class == ITEM_CLASS_QUIVER)
                    {
                        haveQuiver = true;
                        break;
                    }
                }
                if (haveQuiver)
                    continue;
            }
            return bag;
        }
    }

    return curBag;
}

void EquipAction::EquipItemToSlot(Player* requester, Item* item, uint8 targetSlot)
{
    // The explicit-slot sibling of EquipItem(): same rule, the item must be one the core can equip.
    if (!IsItemProtoUsableCompat(GetItemProtoCompat(item)))
        return;

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = GetItemProtoCompat(item)->ItemId;

    uint16 dest;
    InventoryResult msg = bot->CanEquipItem(targetSlot, dest, item, true);
    if (msg != EQUIP_ERR_OK)
    {
        bot->SendEquipError(msg, item, nullptr);
        return;
    }

    uint8 destSlot = dest & 0xFF;
    if (destSlot != targetSlot)
    {
        ai->TellPlayer(requester, "Cannot equip this item to the specified slot.");
        return;
    }

    Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot);
    Item* oldOffhand = nullptr;
    if (destSlot == EQUIPMENT_SLOT_MAINHAND && GetItemProtoCompat(item)->InventoryType == INVTYPE_2HWEAPON)
        oldOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

    uint16 src = ((bagIndex << 8) | slot);
    uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | targetSlot);

    bot->SwapItem(src, dstPos);

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (oldOffhand)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldOffhand).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", GetItemProtoCompat(item)->Name1, std::to_string(GetItemProtoCompat(item)->ItemId));

    std::map<std::string, std::string> args;
    args["%item"] = chat->formatItem(item);
    ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
}
void EquipAction::EquipItem(PlayerbotAI* ai, Player* requester, Item* item, bool silent)
{
    Player* bot = ai->GetBot();
    // Never equip order stock: the order item and its reagent tree stay in
    // the bags until the order (or its branch) finishes.
    if (bot && bot->GetGuildId() && GuildOrderConsumesItem(bot, item ? GetItemProtoCompat(item)->ItemId : 0))
        return;
    // The last gate before the core's equip: an item whose mirror could not be filled has fields the core will abort on
    // (Player::_ApplyWeaponDamage on a weapon without a damage row). It stays in the bags.
    if (!IsItemProtoUsableCompat(GetItemProtoCompat(item)))
        return;
    AiObjectContext* context = ai->GetAiObjectContext();

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = GetItemProtoCompat(item)->ItemId;
    ItemPrototype const* newProto = GetItemProtoCompat(item);

    uint16 dest;
    InventoryResult result = bot->CanEquipItem(NULL_SLOT, dest, item, !item->IsBag());

    Item* oldItem = nullptr;
    Item* oldOffhand = nullptr;
    if (result == EQUIP_ERR_OK)
    {
        oldItem = bot->GetItemByPos(dest);

        if (oldItem && oldItem->GetSlot() == EQUIPMENT_SLOT_MAINHAND && GetItemProtoCompat(item)->InventoryType == INVTYPE_2HWEAPON)
            oldOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    }

    if (GetItemProtoCompat(item)->InventoryType == INVTYPE_AMMO)
    {
        bot->SetAmmo(itemId);
    }
    else
    {
        bool equipedBag = false;
        bool isBag = GetItemProtoCompat(item)->Class == ITEM_CLASS_CONTAINER || GetItemProtoCompat(item)->Class == ITEM_CLASS_QUIVER;
        if (isBag)
        {
            uint8 newBagSlot = GetSmallestBagSlot(bot, GetItemProtoCompat(item)->Class, GetItemProtoCompat(item)->SubClass);

            // Only replace the smallest equipped bag when it is empty. The
            // core rejects moving a non-empty bag from a specific slot, and
            // SwapItem with two non-empty bags errors as well - so relocate
            // quiver ammo / soul shards into backpack/bag space first.
            // A plain bag never takes a quiver/pouch or soul-bag slot (see
            // GetSmallestBagSlot): with no other slot free the equip fails
            // explicitly instead of silently leaving the bag in the backpack.
            Item* oldBag = newBagSlot > 0 ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, newBagSlot) : nullptr;
            Bag* oldBagBag = (oldBag && oldBag->IsBag()) ? (Bag*)oldBag : nullptr;
            if (newBagSlot > 0 && oldBagBag && !oldBagBag->IsEmpty())
            {
                auto tryStoreElsewhere = [&](Item* content, ItemPosCountVec& dest) -> bool {
                    if (bot->CanStoreItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, content, false) == EQUIP_ERR_OK)
                        return true;
                    for (uint8 otherBag = INVENTORY_SLOT_BAG_START; otherBag < INVENTORY_SLOT_BAG_END; ++otherBag)
                    {
                        if (otherBag == newBagSlot)
                            continue;
                        if (bot->CanStoreItem(otherBag, NULL_SLOT, dest, content, false) == EQUIP_ERR_OK)
                            return true;
                    }
                    return false;
                };

                bool contentsMoved = true;
                for (uint32 i = 0; i < oldBagBag->GetBagSize(); ++i)
                {
                    Item* content = oldBagBag->GetItemByPos(i);
                    if (!content)
                        continue;
                    // Player::RemoveItem, not Bag::RemoveItem: StoreItem
                    // re-registers a timed item (quest items with a
                    // duration), so the bag-only removal left it twice in
                    // the player's duration list and the copy dangled once
                    // the item expired (SIGSEGV in Item::UpdateDuration).
                    bot->RemoveItem(oldBagBag->GetSlot(), i, true);
                    ItemPosCountVec dest;
                    if (tryStoreElsewhere(content, dest))
                    {
                        bot->StoreItem(dest, content, true);
                    }
                    else
                    {
                        ItemPosCountVec back;
                        if (bot->CanStoreItem(oldBagBag->GetSlot(), i, back, content, false) == EQUIP_ERR_OK)
                            bot->StoreItem(back, content, true);
                        if (GetItemProtoCompat(oldBagBag) && GetItemProtoCompat(oldBagBag)->Class == ITEM_CLASS_QUIVER &&
                            GetItemProtoCompat(content) && GetItemProtoCompat(content)->Class == ITEM_CLASS_PROJECTILE)
                        {
                            bot->DestroyItem(oldBagBag->GetSlot(), i, true);
                            continue;
                        }
                        contentsMoved = false;
                        break;
                    }
                }
                if (!contentsMoved || !oldBagBag->IsEmpty())
                {
                    // No room for the ammo/shards, or a quiver with no free
                    // slot (GetSmallestBagSlot returned 0): fail loudly so
                    // the owner sees it once per attempt, not silently.
                    bot->SendEquipError(EQUIP_ERR_INVENTORY_FULL, item, nullptr);
                    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
                    if (oldItem)
                        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
                    if (oldOffhand)
                        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldOffhand).GetQualifier());
                    return;
                }
                oldBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, newBagSlot);
                oldBagBag = (oldBag && oldBag->IsBag()) ? (Bag*)oldBag : nullptr;
            }
            const bool oldBagIsFull = oldBagBag && !oldBagBag->IsEmpty();

            if (newBagSlot > 0 && !oldBagIsFull)
            {
                // bagIndex/slot were read on entry, but the evacuation loop
                // above moves the contents of the replaced bag - including
                // the upgrade bag itself when it was carried inside it, which
                // is where looted bags normally land. Read the live position
                // again and let core swap from there: the destination holds
                // an empty bag (or nothing) and a container's holder is
                // always a plain bag, so the exchange is legal.
                uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | newBagSlot);
                bot->SwapItem(item->GetPos(), dst);

                // Core can still refuse the swap; only report what really
                // happened, so a failed upgrade does not read as equipped.
                equipedBag = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, newBagSlot) == item;
            }
            else
            {
                bot->SendEquipError(EQUIP_ERR_INVENTORY_FULL, item, nullptr);
            }
        }

        if (!equipedBag && !isBag)
        {
            AutoEquipCompat(bot, bagIndex, slot);
        }

        // A hunter swapping bow<->gun keeps the stale ammo id: after the
        // equip completes, re-equip matching ammo from bags (Issue #219).
        if (newProto && (newProto->Class == ITEM_CLASS_WEAPON) &&
            (bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_WARRIOR))
        {
            Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            if (ranged && GetItemProtoCompat(ranged) && GetItemProtoCompat(ranged)->ItemId == itemId)
                ai->DoSpecificAction("equip ammo", Event(), true);
        }
    }

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (oldOffhand)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldOffhand).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", GetItemProtoCompat(item)->Name1, std::to_string(GetItemProtoCompat(item)->ItemId));

    if (!silent)
    {
        std::map<std::string, std::string> args;
        args["%item"] = ChatHelper::formatItem(item);
        ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool EquipUpgradesAction::Execute(Event& event)
{
    if (!MayAutoEquipUpgradesCompat(bot))
        return false;

    if (!sPlayerbotAIConfig.autoEquipUpgradeLoot && !sRandomBotFacade.IsRandomBot(bot))
        return false;

    bool const isClaimed = TortoiseBots::ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow());
    if (!TortoiseBots::MayClaimedBotSelfEquip(bot->GetLevel(), isClaimed, sPlayerbotAIConfig.claimedBotSelfEquip))
        return false;
    // Issue #490: level-60 hires at tier >= 1 wear a static set; loot
    // auto-equip must not replace it (player-equipped and traded items via
    // EquipAction still apply: those are explicit orders, not automation).
    if (TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
        TortoiseBots::HireGearTierEligible(bot->GetLevel(),
            sPlayerbotAIConfig.hireGearTiersEnabled,
            TortoiseBots::EffectiveHireGearTier(
                sPlayerbotAIConfig.hireGearTiersEnabled, sPlayerbotAIConfig.hireGearStartTier)))
        return false;
    // Worn gear is never touched in combat. This action is reachable from the
    // combat engine - WorldPacketHandlerStrategy::InitCombatTriggers installs
    // the same node list as the non-combat one, and bots ding on the killing
    // blow - so without this gate a level-up rewrites the loadout in the middle
    // of a fight, which is the #336 world-mob evade regression. The periodic
    // "equipment audit" node picks the same upgrade up out of combat instead.
    // PlayerbotAI's "attacked while fishing" wake-up is the one deliberate
    // in-combat call and labels its event so it still lands.
    if (sServerFacade.IsInCombat(bot) && event.getSource() != "fishing pole recovery")
        return false;

    if (event.getSource() == "trade status")
    {
        WorldPacket p(event.GetPacket());
        p.rpos(0);
        uint32 status;
        p >> status;

        if (status != TRADE_STATUS_TRADE_ACCEPT)
        {
            return false;
        }
    }
    else if (event.getSource() == "item push result")
    {
        bool valid = false;
        WorldPacket& data = event.GetPacket();
        if (!data.empty())
        {
            data.rpos(0);

            ObjectGuid guid;
            data >> guid;
            if (guid != bot->getObjectGuid())
            {
                return false;
            }

            uint32 received, created, isShowChatMessage, slotId, itemId, suffixFactor, count;
            uint32 itemRandomPropertyId;
            //uint32 invCount;
            uint8 bagSlot;

            data >> received;                               // 0=looted, 1=from npc
            data >> created;                                // 0=received, 1=created
            data >> isShowChatMessage;                                      // IsShowChatMessage
            data >> bagSlot;
            // item slot, but when added to stack: 0xFFFFFFFF
            data >> slotId;
            data >> itemId;
            data >> suffixFactor;
            data >> itemRandomPropertyId;
            data >> count;
            // data >> invCount; // [-ZERO] count of items in inventory

            ItemQualifier itemQualifier(itemId, (int32)itemRandomPropertyId);
            const ItemPrototype* itemProto = itemQualifier.GetProto();
            if (itemProto && (itemProto->Class == ItemClass::ITEM_CLASS_WEAPON ||
                              itemProto->Class == ItemClass::ITEM_CLASS_ARMOR ||
                              itemProto->Class == ItemClass::ITEM_CLASS_CONTAINER ||
                              itemProto->Class == ItemClass::ITEM_CLASS_QUIVER))
            {
                valid = true;
            }
        }

        if (!valid)
        {
            return false;
        }
    }

    // QueryItemUsageForEquip compares a bag item against whatever is in its
    // destination slot, so the equipped weapons must stay where they are.
    // Unequipping MH/OH first (donor lineage, present since the 204-file
    // forward-port 450365a) made every bag weapon read as an empty-slot
    // upgrade, left the spec-transition comparison in ItemUsageValue with no
    // old weapon to look at, and cost two packet round-trips plus a visible
    // flicker and swing-timer reset on every run. Evaluate against live
    // equipment instead.
    context->ClearExpiredValues("item usage", 10); //Clear old item usage.

    std::list<Item*> items;

    FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    visitor.SetUsage(ItemUsage::ITEM_USAGE_BAD_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    items = visitor.GetResult();

    // EQUIP (a real upgrade over what is equipped) before BAD_EQUIP (fills an
    // empty slot), then by stat weight: a heavier off-spec weapon must not
    // claim a slot ahead of the spec weapon that belongs there. The
    // mainhand-first tiebreaker only decides between two equal-weight EQUIP
    // weapons.
    PlayerbotAI* sortAi = ai;
    AiObjectContext* sortContext = sortAi->GetAiObjectContext();
    items.sort([sortContext, plr = bot](Item* i, Item* j) {
        ItemUsage iUsage = sortContext->GetValue<ItemUsage>("item usage", ItemQualifier(i).GetQualifier())->Get();
        ItemUsage jUsage = sortContext->GetValue<ItemUsage>("item usage", ItemQualifier(j).GetQualifier())->Get();
        if (iUsage != jUsage)
            return iUsage < jUsage;

        uint32 iWeight = sRandomItemMgr.ItemStatWeight(plr, i);
        uint32 jWeight = sRandomItemMgr.ItemStatWeight(plr, j);
        if (iWeight != jWeight)
            return iWeight > jWeight;

        bool iMain = GetItemProtoCompat(i)->InventoryType == INVTYPE_WEAPONMAINHAND;
        bool jMain = GetItemProtoCompat(j)->InventoryType == INVTYPE_WEAPONMAINHAND;

        if (iMain != jMain)
            return iMain; // mainhand comes first

        if (GetItemProtoCompat(i)->ItemLevel != GetItemProtoCompat(j)->ItemLevel)
            return GetItemProtoCompat(i)->ItemLevel > GetItemProtoCompat(j)->ItemLevel;

        return GetItemProtoCompat(i)->ItemId < GetItemProtoCompat(j)->ItemId; });

    bool didEquip = false;
    bool equippedBags = false;
    bool forceUsageChanged = false;
    std::set<uint8> filledSlots;
    std::set<uint32> ownerTradedIds = (event.getSource() == "trade status") ? ai->TakeOwnerTradedItemIds() : std::set<uint32>{};
    // The same bots the pool gear top-up handles: pool bots with no live player
    // master, never hired or guild-claimed. Forever rows often store
    // RequiredLevel 0 even at high item levels, so the core says "can wear it"
    // to a level-10 pool bot holding an item level 62 blue; treat the derived
    // requirement as a real one for those bots only.
    bool const gateOrganicByLevel = TortoiseBots::PoolGearTopUpEligible(
        sPlayerbotAIConfig.autoEquipUpgradeRespectsRequiredLevel,
        IsPoolBotCompat(bot),
        PoolGearTopUpIsHiredCompat(bot),
        PoolGearTopUpIsClaimedCompat(bot),
        ai->HasRealPlayerMaster());

    for (auto& item : items)
    {
        // Re-score against the equipment as it stands now: the visitor scored
        // every candidate before the loop ran, so after equipping one item a
        // later candidate would still carry the usage it got for the slot it
        // would have taken then. Re-resolving the slot below can point it at a
        // different, stronger slot (Ring A took the weak finger, Ring B now
        // resolves to the strong one), and a stale EQUIP would overwrite and
        // downgrade it. Reset forces Calculate() to run again this tick.
        ItemQualifier qualifier(item);
        RESET_AI_VALUE2(ItemUsage, "item usage", qualifier.GetQualifier());
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", qualifier.GetQualifier());
        if (usage != ItemUsage::ITEM_USAGE_EQUIP && usage != ItemUsage::ITEM_USAGE_BAD_EQUIP)
        {
            continue;
        }
        // Issue #490 (bug part): hired bots never auto-equip source-gated
        // gear (pure rule in runtime/HireSourceGuardPolicy.h). BG blues score
        // high against seed greens, so the audit would dress every hire in
        // PvP on first loot. The item stays in the bags; explicit `.bot
        // equip` / master trade still work. Pool bots keep the organic
        // behaviour (earned PvP is wearable).
        if (!TortoiseBots::MayHireAutoTakeSource(
                sRandomItemMgr.GetSourceFlags(GetItemProtoCompat(item)->ItemId),
                TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()),
                sPlayerbotAIConfig.randomGearAllowPvP, sPlayerbotAIConfig.randomGearAllowReputation))
            continue;

        // A `.bot gear` pin holds the bot under its item-level cap: the cap
        // pass unequipped (never destroyed) anything above it, and the audit
        // must not put an above-cap piece back on. Explicit `.bot equip` /
        // master trade still apply: those are orders, not automation.
        if (uint32_t cap = TortoiseBots::BotGearService::Instance().PinnedCap(bot->GetGUIDLow()))
            if (GetItemProtoCompat(item) && GetItemProtoCompat(item)->ItemLevel > cap &&
                (GetItemProtoCompat(item)->Class == ITEM_CLASS_WEAPON || GetItemProtoCompat(item)->Class == ITEM_CLASS_ARMOR))
                continue;

        if (gateOrganicByLevel)
        {
            ItemPrototype const* levelProto = GetItemProtoCompat(item);
            if (levelProto &&
                (levelProto->Class == ITEM_CLASS_WEAPON || levelProto->Class == ITEM_CLASS_ARMOR) &&
                TortoiseBots::GearEffectiveRequiredLevel(levelProto->RequiredLevel, levelProto->ItemLevel) > bot->GetLevel())
                continue;
        }

        // Bags and quivers never go through slot resolution below: core's
        // INVTYPE_BAG probe resolves to a single slot (the first free one, or
        // slot 19 once every slot is taken) and vetoes the equip while a
        // quiver is already worn, so GetPreferredEquipSlot answers NULL_SLOT
        // for exactly the quiver upgrades the audit exists for. EquipItem
        // owns the bag path: GetSmallestBagSlot picks the slot to replace and
        // moves the old bag's contents (ammo, shards, items) out first.
        ItemPrototype const* proto = GetItemProtoCompat(item);
        if (proto->Class == ITEM_CLASS_CONTAINER || proto->Class == ITEM_CLASS_QUIVER)
        {
            EquipItem(ai, GetMaster(), item);
            equippedBags = true;
            continue;
        }

        // Compare against (and equip into) the weaker of the item's eligible
        // slots: core only ever reports the primary one once a ring/trinket
        // pair or a dual-wield hand is full. Re-resolve every iteration, since
        // equipping one item moves the slot the next candidate would land in
        // (a 2H weapon frees the off hand, the first of two rings takes the
        // other finger slot).
        uint8 slot = ItemUsageValue::GetPreferredEquipSlot(bot, item, GetItemProtoCompat(item));
        if (slot == NULL_SLOT || filledSlots.count(slot))
        {
            continue; // no eligible slot, or one item per target slot per run
        }

        uint16 dest;
        if (bot->CanEquipItem(slot, dest, item, true) != EQUIP_ERR_OK || (dest & 0xFF) != slot)
        {
            continue;
        }

        // BAD_EQUIP means "fills an empty slot", so it must never swap out
        // something already equipped; otherwise the next audit trades the
        // two items back and the bot ping-pongs.
        if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP && bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            continue;
        }

        // The displaced main hand for the cascade below: captured before the
        // equip moves it to the bags. Only a 1H upgrade on a dual-wielder
        // displaces a demotable hand (a 2H upgrade blocks the off hand).
        Item* displacedMH = nullptr;
        if (slot == EQUIPMENT_SLOT_MAINHAND && proto->Class == ITEM_CLASS_WEAPON &&
            proto->InventoryType != INVTYPE_2HWEAPON && bot->CanDualWield())
            displacedMH = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        // Issue #872: an autonomous alt auto-equips upgrades only if the old
        // piece fits in the bags (B14). Core SwapItem needs one free slot
        // once the worn piece leaves its slot; without it the audit stops
        // equipping and the bot keeps playing.
        if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
        {
            Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (oldItem)
            {
                ItemPosCountVec dest;
                if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, oldItem, false) != EQUIP_ERR_OK ||
                    !TortoiseBots::AltBotMayAutoEquip(true))
                    continue;
            }
        }
        filledSlots.insert(slot);
        didEquip = true;

        sLog.outDetail("Bot #%d <%s> auto equips item %d (%s)", bot->GetGUIDLow(), CStrCompat(bot->GetName()), GetItemProtoCompat(item)->ItemId, usage == ItemUsage::ITEM_USAGE_EQUIP ? "better than current" : "wrong item but empty slot");
        ai->TellDebug(ai->GetMaster(), "Equipping: " + chat->formatItem(item) + " - " + ItemUsageValue::ReasonForNeed(usage, item, 1, bot), "debug equip");

        Item* displacedPrimary = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        Item* displacedSecondary = (slot == EQUIPMENT_SLOT_MAINHAND && proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType == INVTYPE_2HWEAPON)
            ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND)
            : nullptr;

        // Use the auto-equip packet when core would pick the same slot, the
        // explicit-slot packet when the chosen slot is the weaker secondary
        // one that FindEquipSlot(NULL_SLOT) never returns.
        uint16 defaultDest;
        if (bot->CanEquipItem(NULL_SLOT, defaultDest, item, true) == EQUIP_ERR_OK && (defaultDest & 0xFF) == slot)
        {
            EquipItem(ai, GetMaster(), item);
        }
        else
        {
            EquipItemToSlot(GetMaster(), item, slot);
        }

        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item)
        {
            if (isClaimed && ownerTradedIds.count(proto->ItemId) &&
                SetForceItemUsageIfChanged(ai, item, ForceItemUsage::FORCE_USAGE_EQUIP, false))
            {
                forceUsageChanged = true;
            }
            if (TortoiseBots::ShouldKeepDisplacedClaimedGear(isClaimed))
            {
                if (displacedPrimary && displacedPrimary != displacedMH &&
                    bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) != displacedPrimary &&
                    SetForceItemUsageIfChanged(ai, displacedPrimary, ForceItemUsage::FORCE_USAGE_KEEP, true))
                {
                    forceUsageChanged = true;
                }
                if (displacedSecondary &&
                    bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) != displacedSecondary &&
                    SetForceItemUsageIfChanged(ai, displacedSecondary, ForceItemUsage::FORCE_USAGE_KEEP, true))
                {
                    forceUsageChanged = true;
                }
            }
        }

        // MH->OH cascade (AG-2): the upgrade above pushed the old main hand
        // to the bags; demote it to the off hand when it fits there and
        // beats what is equipped. Donor mod-playerbots priority-1 demotion,
        // minus Titan Grip (no 1.12 API). Claims the off-hand slot for this
        // run so a later candidate cannot overwrite it on a stale usage.
        if (displacedMH && bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) != displacedMH)
        {
            ItemPrototype const* displacedProto = GetItemProtoCompat(displacedMH);
            bool fitsOffHand = displacedProto->InventoryType == INVTYPE_WEAPON ||
                displacedProto->InventoryType == INVTYPE_WEAPONOFFHAND;
            if (fitsOffHand)
            {
                // Explicit keep orders win over automation: a displaced hand
                // pinned to the bags stays there, and a pinned off hand is
                // never bounced for it (normal path refuses both).
                bool displacedKeepInBag = AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", displacedProto->ItemId, ForceItemUsage::FORCE_USAGE_NONE) == ForceItemUsage::FORCE_USAGE_BAG;
                Item* curOH = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                bool curOHForced = curOH && AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", GetItemProtoCompat(curOH)->ItemId, ForceItemUsage::FORCE_USAGE_NONE) == ForceItemUsage::FORCE_USAGE_EQUIP;
                // Broken-over-working never auto-equips elsewhere either: a
                // broken displaced hand only fills an empty off hand.
                bool displacedBroken = ItemDurabilityCompat(displacedMH) == 0 && ItemMaxDurabilityCompat(displacedMH) > 0;
                bool curOHBroken = curOH && ItemDurabilityCompat(curOH) == 0 && ItemMaxDurabilityCompat(curOH) > 0;
                if (displacedKeepInBag || curOHForced || (curOH && displacedBroken && !curOHBroken))
                {
                    if (TortoiseBots::ShouldKeepDisplacedClaimedGear(isClaimed) &&
                        SetForceItemUsageIfChanged(ai, displacedMH, ForceItemUsage::FORCE_USAGE_KEEP, true))
                    {
                        forceUsageChanged = true;
                    }
                    continue;
                }

                uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
                uint16 ohDest;
                if (ai::DualWieldCascade(true,
                    sRandomItemMgr.ShouldEquipWeaponForSlot(bot, bot->GetClass(), specId, displacedProto, EQUIPMENT_SLOT_OFFHAND, true),
                    curOH == nullptr,
                    sRandomItemMgr.ItemStatWeight(bot, displacedMH),
                    curOH ? sRandomItemMgr.ItemStatWeight(bot, curOH) : 0) &&
                    bot->CanEquipItem(EQUIPMENT_SLOT_OFFHAND, ohDest, displacedMH, true) == EQUIP_ERR_OK && (ohDest & 0xFF) == EQUIPMENT_SLOT_OFFHAND)
                {
                    EquipItemToSlot(GetMaster(), displacedMH, EQUIPMENT_SLOT_OFFHAND);
                    filledSlots.insert(EQUIPMENT_SLOT_OFFHAND);
                    sLog.outDetail("Bot #%d <%s> cascades old main hand %d to offhand", bot->GetGUIDLow(), CStrCompat(bot->GetName()), displacedProto->ItemId);
                    if (TortoiseBots::ShouldKeepDisplacedClaimedGear(isClaimed) && curOH &&
                        bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) == displacedMH &&
                        SetForceItemUsageIfChanged(ai, curOH, ForceItemUsage::FORCE_USAGE_KEEP, true))
                    {
                        forceUsageChanged = true;
                    }
                }
            }
            if (TortoiseBots::ShouldKeepDisplacedClaimedGear(isClaimed) &&
                bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND) != displacedMH &&
                SetForceItemUsageIfChanged(ai, displacedMH, ForceItemUsage::FORCE_USAGE_KEEP, true))
            {
                forceUsageChanged = true;
            }
        }
    }

    // Put the higher top-end damage weapon in the main hand. Only when core
    // can actually swap the two (neither weapon is locked to its own hand) and
    // only when each weapon is legal FOR its own slot (slot-aware: a 1H reads
    // as any-slot-legal for protection, but neither of those hands may hold
    // a second weapon, so the swap must not run there). Bag/quiver work
    // above never touches a hand, so it reports success without firing this.
    if (didEquip && bot->CanDualWield())
    {
        Item* mh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        Item* oh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

        if (mh && oh
            && GetItemProtoCompat(mh)->Class == ITEM_CLASS_WEAPON
            && GetItemProtoCompat(oh)->Class == ITEM_CLASS_WEAPON
            && GetItemProtoCompat(mh)->InventoryType != INVTYPE_2HWEAPON
            && GetItemProtoCompat(mh)->InventoryType != INVTYPE_WEAPONMAINHAND
            && GetItemProtoCompat(oh)->InventoryType != INVTYPE_WEAPONOFFHAND
            && AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", GetItemProtoCompat(mh)->ItemId, ForceItemUsage::FORCE_USAGE_NONE) != ForceItemUsage::FORCE_USAGE_EQUIP
            && AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", GetItemProtoCompat(oh)->ItemId, ForceItemUsage::FORCE_USAGE_NONE) != ForceItemUsage::FORCE_USAGE_EQUIP)
        {
            uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);

            if (sRandomItemMgr.ShouldEquipWeaponForSlot(bot, bot->GetClass(), specId, GetItemProtoCompat(mh), EQUIPMENT_SLOT_MAINHAND, bot->CanDualWield())
                && sRandomItemMgr.ShouldEquipWeaponForSlot(bot, bot->GetClass(), specId, GetItemProtoCompat(oh), EQUIPMENT_SLOT_OFFHAND, bot->CanDualWield()))
            {
                float mhMaxDmg = GetItemProtoCompat(mh)->Damage[0].DamageMax;
                float ohMaxDmg = GetItemProtoCompat(oh)->Damage[0].DamageMax;

                if (ohMaxDmg > mhMaxDmg)
                {
                    uint16 destMH, destOH;
                    if (bot->CanEquipItem(EQUIPMENT_SLOT_MAINHAND, destMH, oh, true) == EQUIP_ERR_OK
                        && bot->CanEquipItem(EQUIPMENT_SLOT_OFFHAND, destOH, mh, true) == EQUIP_ERR_OK)
                    {
                        uint16 srcPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND);
                        uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND);
                        bot->SwapItem(srcPos, dstPos);

                        // Only claim success when the swap really happened.
                        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) == oh)
                        {
                            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oh).GetQualifier());
                            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(mh).GetQualifier());

                            sLog.outDetail("Bot #%d <%s> swapped MH/OH weapons to put higher top-end damage (%.1f) in main hand",
                                bot->GetGUIDLow(), CStrCompat(bot->GetName()), ohMaxDmg);
                        }
                    }
                }
            }
        }
    }

    if (forceUsageChanged && !sPlayerbotAIConfig.bExplicitDbStoreSave)
        sPlayerbotDbStore.Save(ai);

    return didEquip || equippedBags;
}
