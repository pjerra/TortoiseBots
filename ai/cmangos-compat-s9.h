// The Turtle side of the both-core helpers the ForeverCore port added in slice 9, package L9 (item loot, group loot rolls, the item usage value).
// Same names and signatures as forever/src/compat/TbCompat_S9.h; each forwards to the call the bot always made, word for word (the notes,
// forever/docs/SLICE9-L-NOTES.md, name the original line of every helper). Owned by package L9: no other package edits this file.
#pragma once

#include <string>

// ===============================================================================================================
// NAMES
// ===============================================================================================================
// The roll vote the bot's code carries: T's own enum (ROLL_PASS, ROLL_NEED, ROLL_GREED, ROLL_DISENCHANT, ROLL_NOT_VALID).
using BotRollVote = RollVote;

// ===============================================================================================================
// PACKET EVENTS (WorldPacketTrigger::Check): T is fed by the packet handler tables, nothing to poll.
// ===============================================================================================================
template <class TriggerType>
inline void PollPacketEventCompat(Player* /*bot*/, std::string const& /*name*/, TriggerType& /*trigger*/) {}

// LootOpenCompat raises the window event of a corpse itself on ForeverCore; on T the packet does.
inline void NoteLootWindowAnnouncedCompat(Player const* /*bot*/, ObjectGuid const& /*owner*/) {}

// ===============================================================================================================
// ROLLS (LootRollAction.cpp)
// ===============================================================================================================
// The method gate of the roll poll and vote (ForeverCore rolls only in GROUP_LOOT): T has no gate, the rolls of its packets are answered as they were.
inline bool BotRollsInGroupLootCompat(Player const* /*bot*/, Loot const* /*loot*/) { return true; }

// `group->CountRollVote(bot, lootGuid, slot, vote)`.
inline bool RollVoteCompat(Player* bot, Group* group, ObjectGuid const& lootGuid, uint32 slot, BotRollVote vote)
{
    return group->CountRollVote(bot, lootGuid, slot, vote);
}

// The vote the bot decided (ForeverCore votes greed only, see TbCompat_S9.h).
inline BotRollVote GreedOnlyVoteCompat(BotRollVote vote) { return vote; }

// `loot->LootItemInSlot(slot, bot->GetGUIDLow())`.
inline LootItem* LootItemOfSlotCompat(Loot* loot, uint32 slot, Player* bot) { return loot ? loot->LootItemInSlot(slot, bot->GetGUIDLow()) : nullptr; }

// `(loot && slot < loot->items.size()) ? &loot->items[slot] : nullptr`.
inline LootItem* LootItemAtIndexCompat(Loot* loot, uint32 slot) { return (loot && slot < loot->items.size()) ? &loot->items[slot] : nullptr; }

// `item->AllowedForPlayer(member, lootTarget)`.
inline bool LootItemAllowedForCompat(LootItem const* item, Player const* member, Creature* lootTarget, Loot const* /*loot*/)
{
    return item->AllowedForPlayer(member, lootTarget);
}

// `member->IsWithinLootXPDist(target)`.
inline bool MemberInLootRangeCompat(Player* member, Creature* target) { return member->IsWithinLootXPDist(target); }

// `creature->GetGroupLootTimer()`.
inline uint32 GroupLootTimerCompat(Creature* creature) { return creature->GetGroupLootTimer(); }

// The "emote" roll: `WorldPacket data(MSG_RANDOM_ROLL); data << min << max; bot->GetSession()->HandleRandomRollOpcode(data);`.
inline void RandomRollCompat(Player* bot, int32 minimum, int32 maximum)
{
    WorldPacket data(MSG_RANDOM_ROLL);
    data << minimum;
    data << maximum;
    bot->GetSession()->HandleRandomRollOpcode(data);
}

// The stats a 1.12 random property adds to an item: SummarizeItemStats' own block, word for word (a template: the summary type is the policy's).
template <class Summary>
inline void AddRandomPropertyStatsCompat(Summary& summary, int32 randomPropertyId)
{
    if (!randomPropertyId)
        return;

    if (ItemRandomPropertiesEntry const* randProp = sItemRandomPropertiesStore.LookupEntry(abs(randomPropertyId)))
    {
        for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
        {
            uint32 enchantId = randProp->enchant_id[i - PROP_ENCHANTMENT_SLOT_0];
            if (!enchantId)
                continue;
            if (SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchantId))
            {
                for (int s = 0; s < 3; ++s)
                {
                    if (pEnchant->type[s] == 5) // Stat
                    {
                        uint32 statMod = pEnchant->spellid[s];
                        int32 amount = pEnchant->amount[s];
                        if (statMod == ITEM_MOD_AGILITY) summary.agility += amount;
                        else if (statMod == ITEM_MOD_STRENGTH) summary.strength += amount;
                        else if (statMod == ITEM_MOD_INTELLECT || statMod == ITEM_MOD_SPIRIT) summary.spell += amount;
                        else if (statMod == ITEM_MOD_STAMINA) summary.stamina += amount;
                    }
                }
            }
        }
    }
}

// The bag floor of the corpse take: T has none.
inline bool LootBagFloorCompat(Player* /*bot*/, uint32 /*itemId*/, uint32 /*count*/) { return false; }

// ===============================================================================================================
// ITEM USAGE (ItemUsageValue.cpp)
// ===============================================================================================================
// `Item* item = sAuctionMgr.GetAItem(auction.itemGuidLow); return item ? item->GetCount() : 0;`
inline uint32 AuctionItemCountCompat(AuctionEntry const& auction)
{
    Item* item = sAuctionMgr.GetAItem(auction.itemGuidLow);
    return item ? item->GetCount() : 0;
}
inline float AhDepositMinCompat() { return float(sWorld.getConfig(CONFIG_UINT32_AUCTION_DEPOSIT_MIN)); }
inline float AhDepositRateCompat() { return sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_DEPOSIT); }

// `QuestStatusData& qData = player->getQuestStatusMap()[quest->GetQuestId()]; qData.m_status == QUEST_STATUS_INCOMPLETE`.
inline bool QuestIncompleteCompat(Player* player, Quest const* quest)
{
    QuestStatusData& qData = player->getQuestStatusMap()[quest->GetQuestId()];
    return qData.m_status == QUEST_STATUS_INCOMPLETE;
}

// `quest->ReqSourceId[j]` / `ReqSourceCount[j]`, QUEST_SOURCE_ITEM_IDS_COUNT slots.
constexpr int QuestSourceSlotsCompat = QUEST_SOURCE_ITEM_IDS_COUNT;
inline bool QuestSourceItemCompat(Quest const* quest, int index, uint32& itemId, uint32& count)
{
    itemId = quest->ReqSourceId[index];
    count = quest->ReqSourceCount[index];
    return true;
}

// The skill line stores' fields.
inline uint32 SkillLineIdCompat(SkillLineEntry const* line) { return line->id; }
inline int32 SkillLineCategoryCompat(SkillLineEntry const* line) { return line->categoryId; }
inline uint32 SkillLineAbilitySkillCompat(SkillLineAbilityEntry const* ability) { return ability->skillId; }
inline uint32 SkillLineAbilitySpellCompat(SkillLineAbilityEntry const* ability) { return ability->spellId; }

inline uint32 ItemRequiredHonorRankCompat(ItemPrototype const* proto) { return proto->RequiredHonorRank; }
inline uint32 ItemDisenchantIdCompat(ItemPrototype const* proto) { return proto->DisenchantID; }
inline uint32 PlayerAmmoIdCompat(Player* player) { return player->GetUInt32Value(PLAYER_AMMO_ID); }
inline uint8 EquipSlotOfProtoCompat(Player* bot, ItemPrototype const* proto) { return bot->FindEquipSlot(proto, NULL_SLOT, true); }

// The vendor item lists (ItemUsageValue::PopulateSoldByVendorItemIds): T reads them.
inline bool VendorItemListsUnportedCompat() { return false; }
