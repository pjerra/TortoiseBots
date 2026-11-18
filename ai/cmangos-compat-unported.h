// The Turtle side of the "not ported yet" list of slice 1 (package R6A). Same helper names and signatures as
// forever/src/compat/TbCompat_Unported.h; here every helper IS the call the bot always made (identity), so the Turtle build
// behaves exactly as before. Each helper is the legacy expression or block moved here word for word.
// Included from ai/cmangos-compat-shim.h after the R4 files.
#pragma once

#include "playerbot/UnportedFeatures.h"


// ===============================================================================================================
// ITEMS (PlayerbotAIItems.cpp, ChatHelper.cpp, ItemVisitors.h, UseItemAction.h, ItemUsageValue.h)
// ===============================================================================================================

// A run of slot numbers for `for (int i : XxxSlotsCompat())`. Same type on both cores; only the numbers differ.
struct SlotRangeCompat
{
    int first;
    int last;   // one past the end
    struct Iter
    {
        int value;
        int operator*() const { return value; }
        Iter& operator++() { ++value; return *this; }
        bool operator!=(Iter const& other) const { return value != other.value; }
    };
    Iter begin() const { return { first }; }
    Iter end() const { return { last }; }
};


// Keyring and bank item slot ranges: the slot constants of the Turtle core.
inline SlotRangeCompat KeyringSlotsCompat() { return { KEYRING_SLOT_START, KEYRING_SLOT_END }; }
inline SlotRangeCompat BankItemSlotsCompat() { return { BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END }; }

// The loot of a creature or game object: the public member.
inline Loot* LootOfCreatureCompat(Creature* creature) { return &creature->loot; }
inline Loot* LootOfGameObjectCompat(GameObject* object) { return &object->loot; }

inline bool ItemProtoHasDisplayIdCompat(ItemPrototype const* proto, uint32 displayId) { return proto->DisplayInfoID == displayId; }

// PlayerbotAI::ImbueItem's packet, moved word for word (the CMANGOS branch: the MANGOS branch was never compiled by this build).
inline void QueueUseItemPacketCompat(Player* bot, uint8 bagIndex, uint8 slot, uint8 spell_index, uint16 targetFlag, ObjectGuid const& targetGUID)
{
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_USE_ITEM, 20));
    *packet << bagIndex;
    *packet << slot;
    *packet << spell_index;
    *packet << targetFlag;
    if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_GAMEOBJECT))
        *packet << targetGUID.WriteAsPacked();
    bot->GetSession()->QueuePacket(packet.release());
}

inline bool ItemProtoFitsSlotCompat(Player* bot, ItemPrototype const* proto, uint32 slot)
{
    uint8 eslot = bot->FindEquipSlot(proto, NULL_SLOT, true);
    return slot == eslot;
}

inline bool ItemRandomPropertySuffixCompat(int32 randomPropertyId, int loc_idx, std::string& suffix)
{
    ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(abs(randomPropertyId));
    if (!item_rand)
        return false;
    if (loc_idx < 0)
        loc_idx = 0;
    suffix = item_rand->nameSuffix[loc_idx];
    return true;
}

inline bool SpellSummonsCritterCompat(SpellEntry const* spellInfo)
{
    for (int i = 0; i < 3; i++)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_SUMMON_CRITTER)
            return true;
    }
    return false;
}


inline int32 ItemRandomPropertyIdCompat(Item* item) { return item->GetItemRandomPropertyId(); }
inline int32 ItemRandomPropertyIdCompat(LootItem const& item) { return item.randomPropertyId; }
inline int32 NewItemRandomPropertyIdCompat(uint32 itemId) { return Item::GenerateItemRandomPropertyId(itemId); }


// ===============================================================================================================
// PACKET HANDLER TABLES
// ===============================================================================================================
constexpr bool OpcodesAgreeCompat(uint32 policyValue, uint32 coreOpcode) { return policyValue == coreOpcode; }

inline bool PackedGuidReadCompat(tbcompat::Feature, WorldPacket& packet, ObjectGuid& guid)
{
    packet >> guid.ReadAsPacked();
    return true;
}

template <class Helper, class Spec>
inline void RegisterPacketHandlersCompat(Helper& helper, Spec const* specs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        helper.AddHandler(specs[i].opcode, specs[i].handler, specs[i].delay);
}

// Runs a block of the legacy code that ForeverCore cannot compile or run yet. T: the block. F (TbCompat_Unported.h): counts the
// feature and runs nothing; the block is a generic lambda, so what it does with its `auto` arguments is not looked at there.
template <class Block, class... Args>
inline void UnportedBlockCompat(tbcompat::Feature, Block&& block, Args&&... args) { block(std::forward<Args>(args)...); }
template <class Result, class Block, class... Args>
inline Result UnportedBlockResultCompat(tbcompat::Feature, Result, Block&& block, Args&&... args) { return block(std::forward<Args>(args)...); }
inline void RegisterDirectPacketCasesCompat() {}

inline uint32 WorldAverageDiffCompat() { return sWorld.GetAverageDiff(); }
inline void SetFallInformationCompat(Player* player, float z) { player->SetFallInformation(z); }

// @@END@@
