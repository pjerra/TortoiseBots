// Piece C (roll won): see TbRollWon.h. Core only; no engine header.
#include "host/TbRollWon.h"

#include "Bag.h"
#include "Item.h"
#include "Loot.h"
#include "Player.h"

#include <map>

namespace TortoiseBots
{
namespace rollwon
{
namespace
{
struct MemoHolder
{
    std::map<ObjectGuid, BotWonMemo> memo;
};
MemoHolder& Holder()
{
    static MemoHolder holder;
    return holder;
}
}

std::map<ObjectGuid, BotWonMemo>& WonMemo() { return Holder().memo; }

uint32_t WonBagCount(Player* bot, uint32_t itemId)
{
    if (!bot)
        return 0;
    uint32_t count = 0;
    auto visit = [&](Item* item)
    {
        if (item && item->GetEntry() == itemId)
            count += item->GetCount();
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        visit(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* pack = bot->GetBagByPos(bag))
            for (uint32 slot = 0; slot < pack->GetBagSize(); ++slot)
                visit(pack->GetItemByPos(slot));
    }
    return count;
}

LootItem const* WonSlotOf(Player* bot, ObjectGuid owner, uint32_t slot)
{
    if (!bot)
        return nullptr;
    Loot* loot = bot->GetLootByWorldObjectGUID(owner);
    if (!loot)
        return nullptr;
    for (LootItem const& item : loot->items)
        if (item.LootListId == slot)
            return &item;
    return nullptr;
}
}
}
