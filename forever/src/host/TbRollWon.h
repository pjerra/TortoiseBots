// Piece C (roll won): the host side of the won-roll poll. The headless session never sees SMSG_LOOT_ROLL_WON,
// so this poll remembers the bot's open rolls and raises the `loot roll won` trigger event once when a roll
// disappears with the bot as winner. The event packet has the SMSG_LOOT_ROLL_WON shape RollWonLogAction parses
// (owner, slot, item id, suffix, property, winner), with the winner set to the bot itself.
//
// Threading: world thread only (WorldPacketTrigger::Check runs on the bot's AI tick, the same thread the L9
// roll poll reads Player::GetLootRolls on).
#pragma once

#include "Define.h"
#include "Loot.h"
#include "ObjectGuid.h"
#include "WorldPacket.h"
#include "host/TbRollWonPure.h"
#include <map>
#include <vector>

class LootItem;
class Player;

namespace TortoiseBots
{
namespace rollwon
{

// One bot's poll record: the decision state, the bag counts at first sight of each rolled item, and the wins
// already decided but not yet raised (the trigger holds one event per tick).
struct BotWonMemo
{
    State tracker;
    std::map<uint32_t, uint32_t> bagBaseline;   // item id -> bags count when first remembered
    std::vector<SeenRoll> outbox;               // decided wins waiting for their tick
};

std::map<ObjectGuid, BotWonMemo>& WonMemo();

// The bags count of one item id (backpack and bags, the same traversal as the item push poll).
uint32_t WonBagCount(Player* bot, uint32_t itemId);

// The loot slot of one remembered roll, or null when the loot is gone or offers it no more.
LootItem const* WonSlotOf(Player* bot, ObjectGuid owner, uint32_t slot);

// One poll tick: remember the open rolls, resolve the disappeared ones, raise at most one `loot roll won`
// event. The winner signal is the core's own (LootItem::rollWinnerGUID, set by LootRoll::Finish before the
// item reaches the winner's bags); the bags count is the fallback for a loot the bot never opened, where the
// corpse's rollWinnerGUID is not visible to it.
template <class TriggerType>
void PollRollWon(Player* bot, TriggerType& trigger)
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
        trigger.ExternalEvent(event, nullptr);
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
