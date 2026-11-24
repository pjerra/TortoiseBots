// Piece C (roll won): see TbRollWon.h. Core only; no engine header.
#include "host/TbRollWon.h"

#include "Bag.h"
#include "Item.h"
#include "Loot.h"
#include "Player.h"
#include "WorldPacket.h"

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

void PollRollWon(Player* bot, std::function<void(WorldPacket&)> const& raiseEvent)
{
    if (!bot || !bot->IsInWorld())
        return;

    BotWonMemo& memo = WonMemo()[bot->GetGUID()];
    ObjectGuid const botGuid = bot->GetGUID();

    auto raise = [&](SeenRoll const& won)
    {
        ObjectGuid owner;
        owner.SetRawValue(won.id.ownerHi, won.id.ownerLo);
        WorldPacket event;
        event << owner;
        event << uint32(won.id.slot);
        event << uint32(won.itemId);
        event << uint32(0);                                 // random suffix: the core has bonus lists instead
        event << uint32(0);                                 // random property id, same
        event << botGuid;                                   // the winner: the bot itself
        raiseEvent(event);
    };

    if (!memo.outbox.empty())
    {
        raise(memo.outbox.front());
        memo.outbox.erase(memo.outbox.begin());
        return;
    }

    std::vector<SeenRoll> live;
    for (::LootRoll const* roll : bot->GetLootRolls())
    {
        if (!roll || !roll->IsStarted() || !roll->GetLoot() || !roll->GetItem())
            continue;
        SeenRoll seen;
        seen.id.ownerHi = roll->GetLoot()->GetOwnerGUID().GetRawValue(1);
        seen.id.ownerLo = roll->GetLoot()->GetOwnerGUID().GetRawValue(0);
        seen.id.slot = roll->GetItem()->LootListId;
        seen.itemId = roll->GetItem()->itemid;
        seen.count = roll->GetItem()->count;
        if (!memo.bagBaseline.count(seen.itemId))
            memo.bagBaseline[seen.itemId] = WonBagCount(bot, seen.itemId);
        live.push_back(seen);
    }

    auto fateOf = [&](SeenRoll const& remembered) -> Fate
    {
        Fate fate;
        ObjectGuid owner;
        owner.SetRawValue(remembered.id.ownerHi, remembered.id.ownerLo);
        LootItem const* slot = WonSlotOf(bot, owner, remembered.id.slot);
        // The core's own record first: a visible loot names its winner even when the item is already taken.
        // The bags count covers a corpse the bot never opened, whose loot it cannot see. A duplicate of the
        // same item looted elsewhere in the same tick would also read as held: same item, same log line.
        fate.winnerIsBot = slot && slot->rollWinnerGUID == botGuid;
        bool takenByWinner = slot && slot->is_looted && fate.winnerIsBot;
        auto base = memo.bagBaseline.find(remembered.itemId);
        fate.held = takenByWinner || (base != memo.bagBaseline.end() && WonBagCount(bot, remembered.itemId) > base->second);
        fate.lootGone = !slot || slot->is_looted;
        return fate;
    };

    std::vector<SeenRoll> won = Update(memo.tracker, live, fateOf);
    for (SeenRoll const& roll : won)
        memo.outbox.push_back(roll);
    if (!memo.outbox.empty())
    {
        raise(memo.outbox.front());
        memo.outbox.erase(memo.outbox.begin());
    }

    if (!memo.tracker.open.empty() || !memo.tracker.pending.empty() || !memo.outbox.empty())
    {
        std::map<uint32_t, bool> referenced;
        for (auto const& [id, roll] : memo.tracker.open)
            referenced[roll.itemId] = true;
        for (auto const& [id, roll] : memo.tracker.pending)
            referenced[roll.itemId] = true;
        for (SeenRoll const& roll : memo.outbox)
            referenced[roll.itemId] = true;
        for (auto it = memo.bagBaseline.begin(); it != memo.bagBaseline.end();)
            it = referenced.count(it->first) ? std::next(it) : memo.bagBaseline.erase(it);
        return;
    }
    if (live.empty())
        WonMemo().erase(botGuid);
}
}
}
