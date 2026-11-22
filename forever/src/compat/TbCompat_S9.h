// ForeverCore side of the both-core helpers and names of slice 9, package L9 (item loot, group loot rolls, the item usage value).
// Owned by package L9. Turtle side: ai/cmangos-compat-s9.h (same names and signatures, each forwards to the call the bot always made).
// Notes: forever/docs/SLICE9-L-NOTES.md (every helper, the core call it makes, the failure value).
//
// What this pair makes real (slice 2 left it as the named features "loot: items, quest items and rolls", "loot tables (drop maps)", "item usage"):
//  * the items of a loot window are read from the core's Loot object and put into the packet shape StoreLootAction reads (TbCompat_I2.h
//    LootWindowReadCompat), taken through WorldSession::HandleAutostoreLootItemOpcode (LootHandler.cpp:77, LootAutostoreCompat), with the
//    slot rights the core itself gives (LootItem::GetUiTypeForPlayer, Loot.cpp:212);
//  * a group-loot roll the core starts for this bot (Player::GetLootRolls, Player.h:2359: the core's own read-only list of the rolls waiting for
//    this player's answer) is announced to the engine as the "loot start roll" event, and the bot's vote goes through WorldSession::
//    HandleLootRoll (LootHandler.cpp:510). THE POLICY (owner decision 2026-11-22: TB parity): the vote TB decided (need/greed/pass, etiquette
//    included) reaches the wire, constrained only by the roll's own vote mask (NEED not offered -> GREED, GREED not offered -> PASS, TbRollVote.h);
//    no auto-equip (the trigger `loot roll won` and the action `equip upgrades` are not registered);
//  * the loot tables (creature / game object / reference / item) are copied from the world database through the guarded reader below
//    (LootTemplate::Entries and ::Groups are private, LootMgr.h:150-151).
//
// Threading: the engine ticks on the world thread outside the map update (World.cpp:2487-2500, findings item 2). Everything here that
// reads a Loot, a LootRoll or a bot's roll list is a READ made on that thread; the memo below is touched only there.
#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "host/TbRollVote.h"
#include "forever/src/host/TbRollMethodsPure.h"

// ===============================================================================================================
// NAMES: the 1.12 spellings of constants the loot, roll and item usage code uses
// ===============================================================================================================
// ItemBondingType (ItemTemplate.h:139): 1.12 "bind when picked up" / "bind when equipped" are F's acquire / equip.
#define BIND_WHEN_PICKED_UP BIND_ON_ACQUIRE
#define BIND_WHEN_EQUIPPED BIND_ON_EQUIP
// ItemSpelltriggerType (ItemTemplate.h:124): the chance-on-hit trigger of 1.12 is the proc trigger.
#define ITEM_SPELLTRIGGER_CHANCE_ON_HIT ITEM_SPELLTRIGGER_ON_PROC
// ItemClass 15 (ItemTemplate.h:489): 1.12 "junk" is F's "miscellaneous".
#define ITEM_CLASS_JUNK ITEM_CLASS_MISCELLANEOUS
// SummarizeItemStats (LootRollAction.cpp) only compares an aura name with these. F has no school-specific spell crit aura and keeps the shield block
// value as a percent aura (SpellAuraDefines.h:144, :237): the nearest F aura stands in, so an item with a different aura reads as "no spell power / block"
// there: LESS. The summary only feeds FindBetterFittingMember, which runs for a need vote.
#define SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL SPELL_AURA_MOD_SPELL_CRIT_CHANCE
#define SPELL_AURA_MOD_SHIELD_BLOCKVALUE SPELL_AURA_MOD_SHIELD_BLOCKVALUE_PCT

// The roll vote the bot's code carries. T `RollVote` (ROLL_PASS, ROLL_NEED, ROLL_GREED, ROLL_DISENCHANT, ROLL_NOT_VALID); F's `RollVote` is a scoped enum
// of the core's own votes, and its unscoped ROLL_* constants are `RollType` (Loot.h:52-61, same numbers 0..3). ROLL_NOT_VALID is F's MAX_ROLL_TYPE (5, the
// value of RollVote::NotValid, Loot.h:69). MAX_ROLL_FROM_CLIENT: the votes a client may send (pass, need, greed).
using BotRollVote = RollType;
#define ROLL_NOT_VALID MAX_ROLL_TYPE
constexpr uint8 MAX_ROLL_FROM_CLIENT = 3;

// ===============================================================================================================
// THE MEMO of what the engine was already told about (per bot; world thread only)
// ===============================================================================================================
// A loot window of a game object and a group-loot roll are announced to the engine ONCE. The core keeps a roll in the player's list until it is
// decided (up to LOOT_ROLL_TIMEOUT), and a window until it is released, so without the memo every poll would announce them again.
namespace tbs9
{
struct BotLootMemo
{
    std::set<ObjectGuid> windows;                          // loot owners (game objects) whose window was announced
    std::set<std::pair<ObjectGuid, uint32>> rolls;         // (loot owner, loot list id) of the rolls announced
    bool bagInit = false;
    std::set<ObjectGuid> bagItems;                         // item guids already seen in bags (item push result poll)
};

inline std::map<ObjectGuid, BotLootMemo>& Memo()
{
    static std::map<ObjectGuid, BotLootMemo> memo;
    return memo;
}

// Forgets what the bot's session no longer holds, so the memo stays as small as the bot's open windows and rolls.
inline void Prune(Player* bot, BotLootMemo& memo)
{
    for (auto it = memo.windows.begin(); it != memo.windows.end();)
    {
        if (!bot->GetLootByWorldObjectGUID(*it))
            it = memo.windows.erase(it);
        else
            ++it;
    }
    for (auto it = memo.rolls.begin(); it != memo.rolls.end();)
    {
        bool live = false;
        for (::LootRoll const* roll : bot->GetLootRolls())
            if (roll && roll->GetLoot() && roll->GetItem() && roll->GetLoot()->GetOwnerGUID() == it->first && roll->GetItem()->LootListId == it->second)
            {
                live = true;
                break;
            }
        if (!live)
            it = memo.rolls.erase(it);
        else
            ++it;
    }
}

// Marks the window of a loot owner as announced (the corpse path raises its own event).
inline void NoteWindowAnnounced(Player const* bot, ObjectGuid const& owner)
{
    Memo()[bot->GetGUID()].windows.insert(owner);
}
}

// Called by LootOpenCompat when it raises the "loot response" event of a corpse itself (TbCompat_I2.h), so the poll below never raises it a second time.
inline void NoteLootWindowAnnouncedCompat(Player const* bot, ObjectGuid const& owner) { tbs9::NoteWindowAnnounced(bot, owner); }

// THE METHOD GATE: the bot rolls where a roll window exists (group loot and need-before-greed, TbRollMethodsPure.h): in free-for-all and round robin
// there is no window, and under master loot the human master distributes. For every other method (or no group) NOTHING is announced and NOTHING is
// voted: the core's own timeout decides. Both the bot's group and the roll's own loot must take rolls (a method changed in flight leaves the roll to
// the core's timeout).
inline bool BotRollsInGroupLootCompat(Player const* bot, Loot const* loot)
{
    Group const* group = bot ? bot->GetGroup() : nullptr;
    return group && TortoiseBots::rollmethods::MethodTakesRolls(int(group->GetLootMethod())) && (!loot || TortoiseBots::rollmethods::MethodTakesRolls(int(loot->GetLootMethod())));
}

// ===============================================================================================================
// PACKET EVENTS the core never sends to a socketless session (WorldPacketTrigger::Check)
// ===============================================================================================================
// T: the WorldPacketTrigger is fed by the packet handler tables. F: a bot session sends and receives no such packets (Unported feature "packet handler
// tables"), so two of the triggers read the core's own state instead, the way the packet would have told them:
//  * "loot start roll" (SMSG_LOOT_START_ROLL on T: [creature guid][slot u32][item id u32][random suffix u32][random property i32][timeout u32]): one event
//    per roll of Player::GetLootRolls() that has started and was not announced yet. The owner guid is the corpse or chest, the slot the loot list id, the
//    timeout the core's LOOT_ROLL_TIMEOUT (60 s, Loot.h). One event per poll (the trigger holds one): the next roll comes with the next check.
//  * "loot response" (SMSG_LOOT_RESPONSE): only for the window of a GAME OBJECT (a chest the bot opened by a spell or a use order), which has no
//    LootOpenCompat of its own. A corpse window is raised by LootOpenCompat (and marked announced there), and windows that the area-loot of the core
//    opened for neighbouring corpses are never announced (the engine would take items from corpses it did not walk to).
// Nothing is announced while the trigger still holds the previous event (the Check of an unconsumed event is non-empty).
template <class TriggerType>
inline void PollItemPush(Player* bot, tbs9::BotLootMemo& memo, TriggerType& trigger)
{
    std::set<ObjectGuid> live;
    std::vector<Item*> added;
    auto visit = [&](Item* item)
    {
        if (!item)
            return;
        ObjectGuid const guid = item->GetGUID();
        live.insert(guid);
        if (memo.bagInit && !memo.bagItems.count(guid))
            added.push_back(item);
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        visit(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* pBag = bot->GetBagByPos(bag))
            for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
                visit(pBag->GetItemByPos(slot));
    }
    if (!memo.bagInit)
    {
        memo.bagItems = std::move(live);
        memo.bagInit = true;
        return;
    }
    for (auto it = memo.bagItems.begin(); it != memo.bagItems.end();)
        it = live.count(*it) ? std::next(it) : memo.bagItems.erase(it);
    for (Item* item : added)
    {
        memo.bagItems.insert(item->GetGUID());
        ItemTemplate const* tpl = item->GetTemplate();
        if (!tpl)
            continue;
        uint32 const cls = tpl->GetClass();
        if (cls != ITEM_CLASS_WEAPON && cls != ITEM_CLASS_ARMOR && cls != ITEM_CLASS_CONTAINER && cls != ITEM_CLASS_QUIVER)
            continue;
        WorldPacket event;
        event << bot->GetGUID();
        event << uint32(0);                                // received
        event << uint32(0);                                // created
        event << uint32(0);                                // isShowChatMessage
        event << uint8(item->GetBagSlot());
        event << uint32(item->GetSlot());
        event << uint32(item->GetEntry());
        event << uint32(0);                                // suffixFactor
        event << uint32(0);                                // itemRandomPropertyId
        event << uint32(item->GetCount());
        trigger.ExternalEvent(event, nullptr);
        return;
    }
}

template <class TriggerType>
inline void PollPacketEventCompat(Player* bot, std::string const& name, TriggerType& trigger)
{
    if (!bot || !bot->IsInWorld())
        return;
    if (name == "item push result")
    {
        PollItemPush(bot, tbs9::Memo()[bot->GetGUID()], trigger);
        return;
    }
    if (name == "quest share")
    {
        PollQuestShareCompat(bot, trigger);
        return;
    }
    if (name != "loot start roll" && name != "loot response")
        return;

    // The cheap exit of every tick: the bot has no roll and no window, so there is nothing to announce and nothing to remember.
    if (bot->GetLootRolls().empty() && bot->GetAELootView().empty())
    {
        auto it = tbs9::Memo().find(bot->GetGUID());
        if (it != tbs9::Memo().end())
        {
            it->second.windows.clear();
            it->second.rolls.clear();
            if (!it->second.bagInit)
                tbs9::Memo().erase(it);
        }
        return;
    }

    tbs9::BotLootMemo& memo = tbs9::Memo()[bot->GetGUID()];
    tbs9::Prune(bot, memo);

    if (name == "loot start roll")
    {
        for (::LootRoll const* roll : bot->GetLootRolls())
        {
            if (!roll || !roll->IsStarted() || !roll->GetLoot() || !roll->GetItem() || !BotRollsInGroupLootCompat(bot, roll->GetLoot()))
                continue;
            ObjectGuid const owner = roll->GetLoot()->GetOwnerGUID();
            uint32 const slot = roll->GetItem()->LootListId;
            if (memo.rolls.count({ owner, slot }))
                continue;

            WorldPacket event;
            event << owner;
            event << uint32(slot);
            event << uint32(roll->GetItem()->itemid);
            event << uint32(0);                              // random suffix: F has item bonus lists instead (Unported "item random properties")
            event << int32(0);                               // random property id, same
            event << uint32(60000);                          // LOOT_ROLL_TIMEOUT
            memo.rolls.insert({ owner, slot });
            trigger.ExternalEvent(event, nullptr);
            return;
        }
        return;
    }

    for (std::pair<ObjectGuid const, Loot*> const& view : bot->GetAELootView())
    {
        Loot const* loot = view.second;
        if (!loot || !loot->GetOwnerGUID().IsGameObject() || memo.windows.count(loot->GetOwnerGUID()))
            continue;
        WorldPacket event;
        event << loot->GetOwnerGUID();
        memo.windows.insert(loot->GetOwnerGUID());
        trigger.ExternalEvent(event, nullptr);
        return;
    }
}

// ===============================================================================================================
// ROLLS (LootRollAction.cpp)
// ===============================================================================================================
// Defined below with the bag floor of the corpse take.
inline bool LootBagFloorCompat(Player* bot, uint32 itemId, uint32 count);

// The vote. T `Group::CountRollVote(bot, lootGuid, slot, vote)` (the entry point CMSG_LOOT_ROLL ends in). F: the typed packet into
// WorldSession::HandleLootRoll (LootHandler.cpp:510), which finds the roll in the player's own list (Player::GetLootRoll, Player.cpp:10032) and calls
// LootRoll::PlayerVote (Loot.cpp:550: refused when the player has already voted or is not a voter of this roll). `lootGuid` is the owner (corpse or
// chest) the engine keys its rolls by; the packet wants the Loot's own guid. Returns false when the bot holds no roll on that slot (any more).
// The HANDLER returns nothing: a vote the core refuses (already voted) is not seen here. That cannot loop: the roll leaves the engine's own list at
// once (RollOnItemInSlot), and the poll announces a roll once.
inline bool RollVoteCompat(Player* bot, Group* /*group*/, ObjectGuid const& ownerGuid, uint32 slot, BotRollVote vote)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld() || slot > 255)
        return false;
    Loot* loot = nullptr;
    for (std::pair<ObjectGuid const, Loot*> const& view : bot->GetAELootView())
        if (view.second && view.second->GetOwnerGUID() == ownerGuid)
            loot = view.second;
    ::LootRoll* roll = nullptr;
    if (!loot)
    {
        // The bot holds a roll on a corpse it never opened: the roll knows its loot.
        for (::LootRoll* candidate : bot->GetLootRolls())
            if (candidate && candidate->GetLoot() && candidate->GetItem() && candidate->GetLoot()->GetOwnerGUID() == ownerGuid &&
                candidate->GetItem()->LootListId == slot)
            {
                roll = candidate;
                break;
            }
    }
    ObjectGuid const lootGuid = loot ? loot->GetGUID() : (roll ? roll->GetLoot()->GetGUID() : ObjectGuid::Empty);
    if (lootGuid.IsEmpty())
        return false;
    ::LootRoll* live = bot->GetLootRoll(lootGuid, uint8(slot));
    if (!live || !live->GetLoot() || !live->GetItem())
        return false;
    // The method gate: no vote outside GROUP_LOOT (the roll was never announced there; a method changed in flight is left to the core's timeout).
    if (!BotRollsInGroupLootCompat(bot, live->GetLoot()))
        return false;
    // The bag floor, before ANY vote: if winning would need a new slot with two or fewer free (or the item does not fit), cast NO vote (not a pass, not a
    // greed): the core's timeout decides, and the bot cannot win an item it has no room for. The same floor as the corpse take (LootBagFloorCompat).
    if (LootBagFloorCompat(bot, live->GetItem()->itemid, live->GetItem()->count))
        return false;
    // Forever refuses a vote the roll's mask does not offer (LootRoll::PlayerVote): NEED on a greed-only item dies there, so fall back NEED -> GREED -> PASS.
    vote = BotRollVote(TbRollVoteMaskFallback(int(vote), live->GetVoteMask()));
    WorldPackets::Loot::LootRoll packet{ WorldPacket(CMSG_LOOT_ROLL) };
    packet.LootObj = lootGuid;
    packet.LootListID = uint8(slot);
    packet.RollType = uint8(vote);
    bot->GetSession()->HandleLootRoll(packet);
    return true;
}

// Does this bot roll on loot while its bags are nearly full? T: nothing changes. F: a roll the bot cannot store is a roll it would still win; the
// existing `bag space` rule (RollAction, >= 100 percent) stands, and so does LootBagFloorCompat for the corpse take.

// The loot of an owner for a bot, and the item of a slot in it (the core's `LootItemInSlot` marks the loot changed; this reader does not).
inline LootItem* LootItemOfSlotCompat(Loot* loot, uint32 slot, Player* /*bot*/)
{
    if (!loot || slot >= loot->items.size())
        return nullptr;
    return &loot->items[slot];
}

// The item at an index of the loot's list (the same expression on both cores).
inline LootItem* LootItemAtIndexCompat(Loot* loot, uint32 slot) { return (loot && slot < loot->items.size()) ? &loot->items[slot] : nullptr; }

// T `item->AllowedForPlayer(member, lootTarget)`. F: `LootItem::AllowedForPlayer(player, loot)` (Loot.h:183).
inline bool LootItemAllowedForCompat(LootItem const* item, Player const* member, Creature* /*lootTarget*/, Loot const* loot)
{
    return item && member && loot && item->AllowedForPlayer(member, loot);
}

// T `member->IsWithinLootXPDist(target)`: the group reward distance. F: Player::IsAtGroupRewardDistance (Player.h:2554, same rule: same map, 74 yd).
inline bool MemberInLootRangeCompat(Player* member, Creature* target) { return target && member->IsAtGroupRewardDistance(target); }

// T `creature->GetGroupLootTimer()` (ms left of the roll). F keeps the roll's end time private (LootRoll::m_endTime): 0, which the caller reads as
// "the window already resolved: vote now rather than hold forever" (ShouldWaitForPlayers). The bot votes at once instead of waiting for real players;
// a roll with real players in the group is not part of the M2 party (bots only).
inline uint32 GroupLootTimerCompat(Creature* /*creature*/) { return 0; }

// The "emote" roll of the roll command (MSG_RANDOM_ROLL): WorldSession::HandleRandomRollOpcode(RandomRollClient&) (GroupHandler.cpp:445).
inline void RandomRollCompat(Player* bot, int32 minimum, int32 maximum)
{
    WorldPackets::Misc::RandomRollClient packet{ WorldPacket(CMSG_RANDOM_ROLL) };
    packet.Min = minimum;
    packet.Max = maximum;
    bot->GetSession()->HandleRandomRollOpcode(packet);
}

// The stats a 1.12 random property adds to an item (SummarizeItemStats). F has no random property (the id reads 0, TbCompat_Unported.h
// ItemRandomPropertyIdCompat), so nothing is added.
template <class Summary>
inline void AddRandomPropertyStatsCompat(Summary& /*summary*/, int32 /*randomPropertyId*/) {}

// ===============================================================================================================
// THE BAG FLOOR (StoreLootAction): a bot does not take an item that needs a NEW bag slot when it has two or fewer free (lead decision: keep bag room;
// `.tb run start` refuses fewer than 12 free slots, package C9; the carrier of a plan item needs the last two). Stacking into an existing stack is
// always allowed. T: never (the legacy rules stand).
// ===============================================================================================================
inline bool LootBagFloorCompat(Player* bot, uint32 itemId, uint32 count)
{
    ItemPosCountVec dest;
    uint32 noSpaceForCount = 0;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count, &noSpaceForCount) != EQUIP_ERR_OK)
        return true;                                        // does not fit at all: the core would refuse it as well
    bool needsNewSlot = false;
    for (ItemPosCount const& position : dest)
    {
        if (!bot->GetItemByPos(position.pos))
            needsNewSlot = true;
    }
    return needsNewSlot && bot->GetFreeInventorySlotCount() <= 2;
}

// ===============================================================================================================
// ITEM USAGE (ItemUsageValue.cpp)
// ===============================================================================================================
// `Item* item = sAuctionMgr->GetAItem(...)` when itemGuidLow is set, or the populated itemCount on ForeverCore.
inline uint32 AuctionItemCountCompat(AuctionEntry const& auction)
{
    if (auction.itemCount)
        return auction.itemCount;
    if (auction.itemGuidLow)
        if (Item* item = sAuctionMgr->GetAItem(ObjectGuid::Create<HighGuid::Item>(auction.itemGuidLow)))
            return item->GetCount();
    return 0;
}

// Is the quest in the bot's log still incomplete (T `getQuestStatusMap()[id].m_status == QUEST_STATUS_INCOMPLETE`, F `GetQuestStatus`, Player.h:1812).
inline bool QuestIncompleteCompat(Player* player, Quest const* quest) { return player->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_INCOMPLETE; }

// The items a quest hands out at its start (T ReqSourceId / ReqSourceCount, four slots; F keeps one: GetSrcItemId / GetSrcItemCount, QuestDef.h:670).
constexpr int QuestSourceSlotsCompat = 4;
inline bool QuestSourceItemCompat(Quest const* quest, int index, uint32& itemId, uint32& count)
{
    if (index != 0 || !quest->GetSrcItemId())
        return false;
    itemId = quest->GetSrcItemId();
    count = quest->GetSrcItemCount();
    return true;
}

// DB2 field names of the skill line stores (DB2Structure.h:3694-3737): T SkillLineEntry::id / categoryId, SkillLineAbilityEntry::skillId / spellId.
inline uint32 SkillLineIdCompat(SkillLineEntry const* line) { return line->ID; }
inline int32 SkillLineCategoryCompat(SkillLineEntry const* line) { return line->CategoryID; }
inline uint32 SkillLineAbilitySkillCompat(SkillLineAbilityEntry const* ability) { return ability->SkillLine; }
inline uint32 SkillLineAbilitySpellCompat(SkillLineAbilityEntry const* ability) { return uint32(ability->Spell); }

// T ItemPrototype::RequiredHonorRank, a 1.12 field. F has no honor ranks: 0 ("requires no rank").
inline uint32 ItemRequiredHonorRankCompat(ItemPrototype const* /*proto*/) { return 0; }

// T ItemPrototype::DisenchantID. F selects the disenchant loot by item level and quality at the moment of the cast (ItemDisenchantLootEntry), the mirror
// prototype has no id: 0 ("not disenchantable"), so the bot never keeps an item for a disenchant it cannot plan: LESS.
inline uint32 ItemDisenchantIdCompat(ItemPrototype const* /*proto*/) { return 0; }

// T `player->GetUInt32Value(PLAYER_AMMO_ID)`: the equipped ammo. F has no ammo slot in the data of this client: 0 ("no ammo equipped").
inline uint32 PlayerAmmoIdCompat(Player* /*player*/) { return 0; }

// T `bot->FindEquipSlot(proto, NULL_SLOT, true)`; F's FindEquipSlot takes an Item (Player.h): NULL_SLOT ("no slot"), counted under the existing row
// "equip slot of an item prototype". The caller (IsFutureUpgrade) then answers "not a future upgrade": LESS.
inline uint8 EquipSlotOfProtoCompat(Player* /*bot*/, ItemPrototype const* /*proto*/)
{
    tbcompat::Unported(tbcompat::Feature::ItemSlotOfPrototype);
    return NULL_SLOT;
}

// The vendor item lists (ItemUsageValue::PopulateSoldByVendorItemIds, called at config load): the legacy queries name a 1.12 table that does not exist here,
// which would abort the server. True = the caller returns with the lists empty (counted under "random item tables").
inline bool VendorItemListsUnportedCompat() { tbcompat::Unported(tbcompat::Feature::RandomItems); return true; }
