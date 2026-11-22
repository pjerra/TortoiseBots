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
#include <functional>
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
// event through `raise` (the caller's trigger.ExternalEvent). The winner signal is the core's own
// (LootItem::rollWinnerGUID, set by LootRoll::Finish before the item reaches the winner's bags); the bags count
// is the fallback for a loot the bot never opened, where the corpse's rollWinnerGUID is not visible to it.
// Defined in TbRollWon.cpp: the body needs the complete Player, which compat headers do not include.
void PollRollWon(Player* bot, std::function<void(WorldPacket&)> const& raiseEvent);
}
}
