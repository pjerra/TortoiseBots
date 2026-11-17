#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "strategy/ItemVisitors.h"
#include "RandomItemMgr.h"

using namespace ai;

void PlayerbotAI::InventoryIterateItems(IterateItemsVisitor* visitor, IterateItemsMask mask)
{
    if ((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BAGS)
        InventoryIterateItemsInBags(visitor);

    if ((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_EQUIP)
        InventoryIterateItemsInEquip(visitor);

    if ((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BANK)
        InventoryIterateItemsInBank(visitor);

    if ((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BUYBACK)
        InventoryIterateItemsInBuyBack(visitor);
}

std::vector<Bag*> PlayerbotAI::GetEquippedAnyBags()
{
    std::vector<Bag*> bags;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            bags.push_back(bag);
        }
    }

    return bags;
}

std::vector<Bag*> PlayerbotAI::GetEquippedQuivers()
{
    std::vector<Bag*> bags;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (GetItemProtoCompat(bag)->Class == ITEM_CLASS_QUIVER)
            {
                bags.push_back(bag);
            }
        }
    }

    return bags;
}

std::vector<Item*> PlayerbotAI::GetInventoryAndEquippedItems()
{
    std::vector<Item*> items;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    items.push_back(pItem);
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            items.push_back(pItem);
        }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            items.push_back(pItem);
        }
    }

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; slot++)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            items.push_back(pItem);
        }
    }

    return items;
}

std::vector<Item*> PlayerbotAI::GetInventoryItems()
{
    std::vector<Item*> items;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    items.push_back(pItem);
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            items.push_back(pItem);
        }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            items.push_back(pItem);
        }
    }

    return items;
}

uint32 PlayerbotAI::GetInventoryItemsCountWithId(uint32 itemId)
{
    uint32 count = 0;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (GetItemProtoCompat(pItem)->ItemId == itemId)
                    {
                        count += pItem->GetCount();
                    }
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (GetItemProtoCompat(pItem)->ItemId == itemId)
            {
                count += pItem->GetCount();
            }
        }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (GetItemProtoCompat(pItem)->ItemId == itemId)
            {
                count += pItem->GetCount();
            }
        }
    }

    return count;
}

bool PlayerbotAI::HasItemInInventory(uint32 itemId)
{

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (GetItemProtoCompat(pItem)->ItemId == itemId)
                    {
                        return true;
                    }
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (GetItemProtoCompat(pItem)->ItemId == itemId)
            {
                return true;
            }
        }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (GetItemProtoCompat(pItem)->ItemId == itemId)
            {
                return true;
            }
        }
    }

    return false;
}

/*
* @return true if has stacks which are not full for these items
*/
bool PlayerbotAI::HasNotFullStacksInBagsForLootItems(LootItemList const& lootItemList)
{
    for (auto lootItem : lootItemList)
    {
        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* pItem = pBag->GetItemByPos(j))
                    {
                        if (GetItemProtoCompat(pItem)->ItemId == lootItem.itemid
                            && pItem->GetCount() < pItem->GetMaxStackCount())
                        {
                            return true;
                        }
                    }
                }
            }
        }

        for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (GetItemProtoCompat(pItem)->ItemId == lootItem.itemid
                    && pItem->GetCount() < pItem->GetMaxStackCount())
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static Loot* LootForWorldObject(WorldObject* wo)
{
    if (!wo) return nullptr;
    if (wo->IsCreature()) return &static_cast<Creature*>(wo)->loot;
    if (wo->IsGameObject()) return &static_cast<GameObject*>(wo)->loot;
    return nullptr;
}

static LootItemList LootItemsForWorldObject(WorldObject* wo)
{
    LootItemList result;
    if (Loot* loot = LootForWorldObject(wo))
        for (LootItem const& item : loot->items)
            result.push_back(item);
    return result;
}

bool PlayerbotAI::HasQuestItemsInWOLootList(WorldObject* wo)
{
    return HasQuestItemsInLootList(LootItemsForWorldObject(wo));
}

bool PlayerbotAI::HasQuestItemsInLootList(LootItemList const& lootItemList)
{
    for (LootItem const& item : lootItemList)
        if (bot->HasQuestForItem(item.itemid)) return true;
    return false;
}

bool PlayerbotAI::CanLootSomethingFromWO(WorldObject* wo)
{
    Loot* loot = LootForWorldObject(wo);
    if (!loot || loot->isLooted()) return false;
    if (loot->gold > 0) return true;
    return HasNotFullStacksInBagsForLootItems(LootItemsForWorldObject(wo));
}

void PlayerbotAI::InventoryIterateItemsInBags(IterateItemsVisitor* visitor)
{
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (!visitor->Visit(pItem))
                return;

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (!visitor->Visit(pItem))
                return;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (!visitor->Visit(pItem))
                        return;
}

void PlayerbotAI::InventoryIterateItemsInEquip(IterateItemsVisitor* visitor)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; slot++)
    {
        Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pItem)
            continue;

        if (!visitor->Visit(pItem))
            return;
    }
}

void PlayerbotAI::InventoryIterateItemsInBank(IterateItemsVisitor* visitor)
{
    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; slot++)
    {
        Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pItem)
            continue;

        if (!visitor->Visit(pItem))
            return;
    }

    for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pBag)
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* pItem = pBag->GetItemByPos(j))
                    {
                        if (!pItem)
                            continue;

                        if (!visitor->Visit(pItem))
                            return;
                    }
                }
            }
        }
    }

}

void PlayerbotAI::InventoryIterateItemsInBuyBack(IterateItemsVisitor* visitor)
{
    for (uint8 slot = BUYBACK_SLOT_START; slot < BUYBACK_SLOT_END; slot++)
    {
        Item* pItem = bot->GetItemFromBuyBackSlot(slot);
        if (!pItem)
            continue;

        if (!visitor->Visit(pItem))
            return;
    }
}

bool compare_items(const ItemPrototype* proto1, const ItemPrototype* proto2)
{
    if (proto1->Class != proto2->Class)
        return proto1->Class > proto2->Class;

    if (proto1->SubClass != proto2->SubClass)
        return proto1->SubClass < proto2->SubClass;

    if (proto1->Quality != proto2->Quality)
        return proto1->Quality < proto2->Quality;

    if (proto1->ItemLevel != proto2->ItemLevel)
        return proto1->ItemLevel > proto2->ItemLevel;

    return false;
}

bool compare_items_by_level(const Item* item1, const Item* item2)
{
    return compare_items(GetItemProtoCompat(item1), GetItemProtoCompat(item2));
}

void PlayerbotAI::InventoryTellItems(Player* player, std::map<uint32, int> itemMap, std::map<uint32, bool> soulbound)
{
    std::list<ItemPrototype const*> items;
    for (std::map<uint32, int>::iterator i = itemMap.begin(); i != itemMap.end(); i++)
    {
        items.push_back(sObjectMgr.GetItemPrototype(i->first));
    }

    items.sort(compare_items);

    uint32 oldClass = -1;
    for (std::list<ItemPrototype const*>::iterator i = items.begin(); i != items.end(); i++)
    {
        ItemPrototype const* proto = *i;

        if (proto->Class != oldClass)
        {
            oldClass = proto->Class;
            switch (proto->Class)
            {
            case ITEM_CLASS_CONSUMABLE:
                TellPlayer(player, "--- consumable ---");
                break;
            case ITEM_CLASS_CONTAINER:
                TellPlayer(player, "--- container ---");
                break;
            case ITEM_CLASS_WEAPON:
                TellPlayer(player, "--- weapon ---");
                break;
            case ITEM_CLASS_ARMOR:
                TellPlayer(player, "--- armor ---");
                break;
            case ITEM_CLASS_REAGENT:
                TellPlayer(player, "--- reagent ---");
                break;
            case ITEM_CLASS_PROJECTILE:
                TellPlayer(player, "--- projectile ---");
                break;
            case ITEM_CLASS_TRADE_GOODS:
                TellPlayer(player, "--- trade goods ---");
                break;
            case ITEM_CLASS_RECIPE:
                TellPlayer(player, "--- recipe ---");
                break;
            case ITEM_CLASS_QUIVER:
                TellPlayer(player, "--- quiver ---");
                break;
            case ITEM_CLASS_QUEST:
                TellPlayer(player, "--- quest items ---");
                break;
            case ITEM_CLASS_KEY:
                TellPlayer(player, "--- keys ---");
                break;
            case ITEM_CLASS_MISC:
                TellPlayer(player, "--- other ---");
                break;
            }
        }

        InventoryTellItem(player, proto, itemMap[proto->ItemId], soulbound[proto->ItemId]);
    }
}

void PlayerbotAI::InventoryTellItem(Player* player, ItemPrototype const* proto, int count, bool soulbound)
{
    std::ostringstream out;
    out << GetChatHelper()->formatItem(proto, count);
    if (soulbound)
        out << " (soulbound)";
    TellPlayer(player, out.str());
}

#define VISIT_MASK(visitmask) \
    InventoryIterateItems(&visitor, visitmask);\
    found.insert(visitor.GetResult().begin(), visitor.GetResult().end())

#define VISIT \
    InventoryIterateItems(&visitor, mask);\
    found.insert(visitor.GetResult().begin(), visitor.GetResult().end())

#define RETURN_SORT_FOUND \
    std::list<Item*> result; \
    for (std::set<Item*>::iterator i = found.begin(); i != found.end(); ++i)\
        result.push_back(*i); \
    result.sort(compare_items_by_level); \
    return result

#define RETURN_FOUND \
    std::list<Item*> result; \
    for (std::set<Item*>::iterator i = found.begin(); i != found.end(); ++i)\
        result.push_back(*i); \
    return result


std::list<Item*> PlayerbotAI::InventoryParseItems(std::string text, IterateItemsMask mask)
{
    if (text.empty())
    {
        std::list<Item*> result;
        return result;
    }

    AiObjectContext* context = aiObjectContext;

    std::set<Item*> found;
    size_t pos = text.find(" ");
    int count = pos != std::string::npos ? atoi(text.substr(pos + 1).c_str()) : 999;

    //Look for item id's in the command.
    ItemIds ids = GetChatHelper()->parseItems(text);
    if (!ids.empty() && text.find("usage ") == std::string::npos)
    {
        for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
        {
            FindItemByIdVisitor visitor(*i);
            VISIT;
        }

        //We want to stop looking if we found items from links or ids. However if the command is like "all 3" itemId 3 will be found. If so keep looking for more items.
        if (text.find("Hfound:") != -1 || text.find("Hitem:") != -1 || pos == std::string::npos)
        {
            RETURN_SORT_FOUND;
        }
    }

    if (text == "all" || text == "*")
    {
        FindAllItemVisitor visitor;
        VISIT_MASK(mask);
        RETURN_SORT_FOUND;
    }
    else if (text == "equip")
    {
        if (!((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_EQUIP))
            return {};

        FindAllItemVisitor visitor;
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_EQUIP);
        RETURN_SORT_FOUND;
    }
    else if (text == "inventory")
    {
        if (!((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BAGS))
            return {};

        FindAllItemVisitor visitor;
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        RETURN_SORT_FOUND;
    }
    else if (text == "bank")
    {
        if (!((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BANK))
            return {};

        FindAllItemVisitor visitor;
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BANK);
        RETURN_SORT_FOUND;
    }
    else if (text == "buyback")
    {
        if (!((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_BUYBACK))
            return {};

        FindAllItemVisitor visitor;
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BUYBACK);
        RETURN_SORT_FOUND;
    }
    else if (text == "food" || text == "conjured food")
    {
        FindFoodVisitor visitor(bot, 11, (text == "conjured food"));
        VISIT;
    }
    else if (text == "drink" || text == "water" || text == "conjured drink" || text == "conjured water")
    {
        FindFoodVisitor visitor(bot, 59, (text == "conjured drink" || text == "conjured water"));
        VISIT;
    }
    else if (text == "mana potion")
    {
        FindPotionVisitor visitor(bot, SPELL_EFFECT_ENERGIZE);
        VISIT;
    }
    else if (text == "healing potion")
    {
        FindPotionVisitor visitor(bot, SPELL_EFFECT_HEAL);
        VISIT;
    }
    else if (text == "mount")
    {
        FindMountVisitor visitor(bot);
        VISIT;
    }
    else if (text == "pet")
    {
        FindPetVisitor visitor(bot);
        VISIT;
    }
    else if (text == "ammo")
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (pItem)
        {
            FindAmmoVisitor visitor(bot, GetItemProtoCompat(pItem)->SubClass);
            VISIT;
        }
    }
    else if (text == "recipe")
    {
        FindRecipeVisitor visitor(bot);
        VISIT;
    }
    else if (text == "open")
    {
        FindOpenItemVisitor visitor(bot);
        VISIT;
    }
    else if (text == "quest")
    {
        FindQuestItemVisitor visitor(bot);
        VISIT;
    }
    else if (text.find("usage ") == 0)
    {
        FindItemUsageVisitor visitor(bot, ItemUsage(stoi(text.substr(6))));
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    }
    else if (text == "tradeskill")
    {
        FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_SKILL);
        VISIT;
    }
    else if (text == "use")
    {
        FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_USE);
        VISIT;
    }
    else if (text == "ah")
    {
        FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_AH);
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

        visitor = FindItemUsageVisitor(bot, ItemUsage::ITEM_USAGE_BROKEN_AH);
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    }
    else if (text == "vendor")
    {
        bool vendorAH = AI_VALUE(uint8, "bag space") > 80 && !urand(0, 10);
        FindVendorItemsVisitor visitor(bot, vendorAH);
        VISIT_MASK(IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    }

    uint32 quality = GetChatHelper()->parseItemQuality(text);
    if (quality != MAX_ITEM_QUALITY)
    {
        FindItemsByQualityVisitor visitor(quality, count);
        VISIT;
    }

    uint32 itemClass = MAX_ITEM_CLASS, itemSubClass = 0;
    GetChatHelper()->parseItemClass(text, &itemClass, &itemSubClass);
    if (itemClass != MAX_ITEM_CLASS)
    {
        FindItemsByClassVisitor visitor(itemClass, itemSubClass);
        VISIT;
    }

    uint32 fromSlot = GetChatHelper()->parseSlot(text);
    if (fromSlot != EQUIPMENT_SLOT_END)
    {
        if ((uint8)mask & (uint8)IterateItemsMask::ITERATE_ITEMS_IN_EQUIP)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, fromSlot);
            if (item)
                found.insert(item);
        }

        FindItemBySlotVisitor visitor(bot, fromSlot);
        VISIT;
    }

    ItemIds outfit = InventoryFindOutfitItems(text);
    if (!outfit.empty())
    {
        FindItemByIdsVisitor visitor(outfit);
        VISIT;
    }

    if (found.size() == 0 && quality == MAX_ITEM_QUALITY && itemClass == MAX_ITEM_CLASS && fromSlot == EQUIPMENT_SLOT_END && outfit.empty())
    {
        FindNamedItemVisitor visitor(bot, text);
        VISIT;
    }

    RETURN_SORT_FOUND;
}

uint32 PlayerbotAI::InventoryGetItemCount(FindItemVisitor* visitor, IterateItemsMask mask)
{
    InventoryIterateItems(visitor, mask);
    uint32 count = 0;
    std::list<Item*>& items = visitor->GetResult();
    for (std::list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
        Item* item = *i;
        count += item->GetCount();
    }
    return count;
}

ItemIds PlayerbotAI::InventoryFindOutfitItems(std::string name)
{
    AiObjectContext* context = GetAiObjectContext();
    std::list<std::string>& outfits = AI_VALUE(std::list<std::string>&, "outfit list");
    for (std::list<std::string>::iterator i = outfits.begin(); i != outfits.end(); ++i)
    {
        std::string outfit = *i;
        if (name == InventoryParseOutfitName(outfit))
            return InventoryParseOutfitItems(outfit);
    }
    return std::set<uint32>();
}

void PlayerbotAI::AccelerateRespawn(Creature* creature, float accelMod)
{
    if (!creature || accelMod <= 0.0f) return;
    uint32 delay = creature->GetRespawnDelay();
    if (accelMod >= 1.0f) delay = 0;
    else delay = static_cast<uint32>(delay * std::max(0.0f, 1.0f - accelMod));
    creature->SetRespawnDelay(delay);
}


std::string PlayerbotAI::InventoryParseOutfitName(std::string outfit)
{
    int pos = outfit.find("=");
    if (pos == -1) return "";
    return outfit.substr(0, pos);
}

ItemIds PlayerbotAI::InventoryParseOutfitItems(std::string text)
{
    ItemIds itemIds;

    std::string::size_type pos = text.find("=") + 1;
    while (pos < text.size())
    {
        std::string::size_type endPos = text.find(',', pos);
        if (endPos == std::string::npos)
            endPos = text.size();

        std::string idC = text.substr(pos, endPos - pos);
        uint32 id = atol(idC.c_str());
        pos = endPos + 1;
        if (id)
            itemIds.insert(id);
    }

    return itemIds;
}


//Find Poison ...Natsukawa
Item* PlayerbotAI::FindPoison() const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

         if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
            continue;

         if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == 6)
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

               if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
                  continue;

               if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == 6)
                  return pItem;
            }
         }
   }
   return NULL;
}

Item* PlayerbotAI::FindConsumable(uint32 displayId) const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

         if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
            continue;

         if ((pItemProto->Class == ITEM_CLASS_CONSUMABLE || pItemProto->Class == ITEM_CLASS_TRADE_GOODS) && pItemProto->DisplayInfoID == displayId)
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

               if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
                  continue;

               if ((pItemProto->Class == ITEM_CLASS_CONSUMABLE || pItemProto->Class == ITEM_CLASS_TRADE_GOODS) && pItemProto->DisplayInfoID == displayId)
                  return pItem;
            }
         }
   }
   return NULL;
}

Item* PlayerbotAI::FindBandage() const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

         if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
            continue;
         if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_FOOD)
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = GetItemProtoCompat(pItem);

               if (!pItemProto || bot->CanUseItem(GetItemTemplateCompat(pItemProto)) != EQUIP_ERR_OK)
                  continue;

               if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_FOOD)
                  return pItem;
            }
         }
   }
   return nullptr;
}

static const uint32 uPriorizedSharpStoneIds[6] =
{
    ELEMENTAL_SHARPENING_DISPLAYID, DENSE_SHARPENING_DISPLAYID,
    SOLID_SHARPENING_DISPLAYID, HEAVY_SHARPENING_DISPLAYID, COARSE_SHARPENING_DISPLAYID, ROUGH_SHARPENING_DISPLAYID
};

static const uint32 uPriorizedWeightStoneIds[6] =
{
    ELEMENTAL_SHARPENING_DISPLAYID, DENSE_WEIGHTSTONE_DISPLAYID,
    SOLID_WEIGHTSTONE_DISPLAYID, HEAVY_WEIGHTSTONE_DISPLAYID, COARSE_WEIGHTSTONE_DISPLAYID, ROUGH_WEIGHTSTONE_DISPLAYID
};

/**
 * FindStoneFor()
 * return Item* Returns sharpening/weight stone item eligible to enchant a bot weapon
 *
 * params:weapon Item* the weap�n the function should search and return a enchanting item for
 * return nullptr if no relevant item is found in bot inventory, else return a sharpening or weight
 * stone based on the weapon subclass
 *
 */
Item* PlayerbotAI::FindStoneFor(Item* weapon) const
{
    Item* stone;
    ItemPrototype const* pProto = GetItemProtoCompat(weapon);
    if (pProto && pProto->Class == ITEM_CLASS_WEAPON && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD || pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD2
        || pProto->SubClass == ITEM_SUBCLASS_WEAPON_AXE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_AXE2
        || pProto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER))
    {
        for (uint8 i = 0; i < countof(uPriorizedSharpStoneIds); ++i)
        {
            stone = FindConsumable(uPriorizedSharpStoneIds[i]);
            if (stone)
                return stone;
        }
    }
    else if (pProto && pProto->Class == ITEM_CLASS_WEAPON && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE2))
    {
        for (uint8 i = 0; i < countof(uPriorizedWeightStoneIds); ++i)
        {
            stone = FindConsumable(uPriorizedWeightStoneIds[i]);
            if (stone)
                return stone;
        }
    }

    return nullptr;
}

static const uint32 uPriorizedWizardOilIds[4] =
{
    MINOR_WIZARD_OIL, LESSER_WIZARD_OIL, BRILLIANT_WIZARD_OIL, WIZARD_OIL
};

static const uint32 uPriorizedManaOilIds[3] =
{
   MINOR_MANA_OIL, LESSER_MANA_OIL, BRILLIANT_MANA_OIL,
};

Item* PlayerbotAI::FindOilFor(Item* weapon) const
{
    Item* oil = nullptr;
    ItemPrototype const* pProto = GetItemProtoCompat(weapon);

    const std::vector<uint32> uPriorizedWizardOilIds = { MINOR_WIZARD_OIL, LESSER_WIZARD_OIL, BRILLIANT_WIZARD_OIL, WIZARD_OIL };
    const std::vector<uint32> uPriorizedManaOilIds = { MINOR_MANA_OIL, LESSER_MANA_OIL, BRILLIANT_MANA_OIL };

    if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD || pProto->SubClass == ITEM_SUBCLASS_WEAPON_STAFF || pProto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER))
    {
        for (uint8 i = 0; i < uPriorizedWizardOilIds.size(); i++)
        {
            oil = FindConsumable(uPriorizedWizardOilIds[i]);
            if (!oil && i < uPriorizedManaOilIds.size())
            {
                oil = FindConsumable(uPriorizedManaOilIds[i]);
            }

            if(oil)
            {
                break;
            }
        }
    }
    else if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE2))
    {
        for (uint8 i = 0; i < uPriorizedManaOilIds.size(); i++)
        {
            oil = FindConsumable(uPriorizedManaOilIds[i]);
            if (!oil && i < uPriorizedWizardOilIds.size())
            {
                oil = FindConsumable(uPriorizedWizardOilIds[i]);
            }

            if (oil)
            {
                break;
            }
        }
    }

    return oil;
}

//  on self
void PlayerbotAI::ImbueItem(Item* item)
{
   ImbueItem(item, TARGET_FLAG_SELF, ObjectGuid());
}

//  item on unit
void PlayerbotAI::ImbueItem(Item* item, Unit* target)
{
   if (!target)
      return;

   ImbueItem(item, TARGET_FLAG_UNIT, target->getObjectGuid());
}

//  item on equipped item
void PlayerbotAI::ImbueItem(Item* item, uint8 targetInventorySlot)
{
   if (targetInventorySlot >= EQUIPMENT_SLOT_END)
      return;

   Item* const targetItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetInventorySlot);
   if (!targetItem)
      return;

   ImbueItem(item, TARGET_FLAG_ITEM, targetItem->getObjectGuid());
}

// generic item use method
void PlayerbotAI::ImbueItem(Item* item, uint16 targetFlag, ObjectGuid targetGUID)
{
   if (!item)
      return;

   uint8 bagIndex = item->GetBagSlot();
   uint8 slot = item->GetSlot();
   uint8 cast_count = 0;
   ObjectGuid item_guid = item->getObjectGuid();

   uint32 spellId = 0;
   uint8 spell_index = 0;
   for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
   {
      if (GetItemProtoCompat(item)->Spells[i].SpellId > 0)
      {
         spellId = GetItemProtoCompat(item)->Spells[i].SpellId;
         spell_index = i;
         break;
      }
   }

#ifdef CMANGOS
   std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_USE_ITEM, 20));
#endif
#ifdef MANGOS
   WorldPacket* packet = new WorldPacket(CMSG_USE_ITEM);
#endif

   *packet << bagIndex;
   *packet << slot;
   *packet << spell_index;
   *packet << targetFlag;

#ifdef CMANGOS
   if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_GAMEOBJECT))
#endif
#ifdef MANGOS
   if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_OBJECT))
#endif
      *packet << targetGUID.WriteAsPacked();

#ifdef CMANGOS
   bot->GetSession()->QueuePacket(packet.release());
#endif
#ifdef MANGOS
   bot->GetSession()->QueuePacket(packet);
#endif
}

void PlayerbotAI::EnchantItemT(uint32 spellid, uint8 slot, Item* item)
{
    Item* pItem = nullptr;
    if (item)
        pItem = item;
    else
        pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

   if (!pItem)
    return;

   if (pItem->GetSlot() != slot)
       return;

#ifdef CMANGOS
   if (pItem->GetOwner() == nullptr)
       return;
#endif

#ifdef MANGOS
   SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellid);
#else
   SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellid);
#endif
   if (!spellInfo)
      return;

   uint32 enchantid = spellInfo->EffectMiscValue[0];
   if (!enchantid)
   {
      sLog.outError("%u: Invalid enchantid %s %s" , enchantid , " report to devs", bot->GetName());
      return;
   }

   if (!((1 << GetItemProtoCompat(pItem)->SubClass) & spellInfo->EquippedItemSubClassMask) &&
      !((1 << GetItemProtoCompat(pItem)->InventoryType) & spellInfo->EquippedItemInventoryTypeMask))
   {

      sLog.outError("%s: items could not be enchanted, wrong item type equipped", bot->GetName());

      return;
   }

   EnchantmentSlot enchantSlot = PERM_ENCHANTMENT_SLOT;

   bot->ApplyEnchantment(pItem, enchantSlot, false);
   pItem->SetEnchantment(enchantSlot, enchantid, 0, 0);
   bot->ApplyEnchantment(pItem, enchantSlot, true);

   sLog.outDetail("%s: items was enchanted successfully!", bot->GetName());
}
