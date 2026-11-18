// The "not ported yet" list of slice 1 (package R6A): ForeverCore side. Turtle side: ai/cmangos-compat-unported.h, the same
// helper names and signatures, where each one is the call the bot always made.
//
// What this file is. The slice compiles files that also hold features the warrior slice never uses (packet handler tables,
// transports, jump and fall, taxi, quest internals, game objects, chat channels, bank, pools, instance binds, pets, loot...).
// Each such feature has ONE name in the closed table below. On ForeverCore a helper of a feature that does not work yet calls
// `tbcompat::Unported(tbcompat::Feature::X)`, which counts the call and logs the FIRST call of the name once (one line:
// `TortoiseBots: not ported yet: <name>`), and then returns the failure value its callers already handle ("the action did not
// happen": false, nullptr, an empty container, a zero count). Never an invented value. Nothing is hidden: the counts are read
// with `tbcompat::UnportedSummary()` (the host prints them at boot and on `.tb unported`, see SLICE1-R6A-NOTES.md "NEEDS FROM
// host"), and forever/docs/UNPORTED.md is the to-do list of the later slices: each slice that ports a feature deletes its row.
//
// The enum is the closed list: a name that is not in the table does not compile. (The brief spelled the call with a string;
// an enum member is the same name checked by the compiler, and the counter is a plain array index on the hot path.)
//
// Included from forever-compat-shim.h at the end of the R4 block, so every type of the pre-included set is visible here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ai/playerbot/UnportedFeatures.h"   // the closed list of feature names (core-free: the Turtle side names the same features)

namespace tbcompat
{
struct UnportedEntry
{
    char const* name;          // the feature name, as in the log line and in UNPORTED.md
    char const* what;          // one sentence: what the feature is and why it does not work yet
    uint64_t calls;            // how many times a helper of the feature was called since the process started
};

// Counts one call of a helper whose feature does not work on ForeverCore, logs the first call of the name once. Thread safe
// (atomics: the engine runs on the world thread today, but a helper must not assume it).
void Unported(Feature feature);

// The whole table with the call counts so far, in table order.
std::vector<UnportedEntry> UnportedSummary();

// "slice: N features not ported: a, b, c" (the boot line: N = the table's size, the names in table order), and the console
// text of `.tb unported` (one line per feature with its call count). Both only format UnportedSummary().
std::string UnportedBootLine();
std::string UnportedReport();

// Counts one registered handler row of a legacy packet table under the group of what its handler does ("loot roll" ->
// Feature::IncomingLoot, ...). An unknown handler name counts under Feature::PacketHandlerTables.
void UnportedPacketHandler(std::string const& handlerName);
}

#ifndef TB_UNPORTED_TABLE_ONLY   // TbCompat_Unported.cpp wants the table without the helpers (they need the core's types)
// ---------------------------------------------------------------------------------------------------------------
// The helpers. Same names and signatures as ai/cmangos-compat-unported.h; each one says what the Turtle call is, what
// ForeverCore does instead, and the failure value.
// ---------------------------------------------------------------------------------------------------------------

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


// The 1.12 keyring slots and the bank item slots. F has no keyring (keys are ordinary items in the bags) and no bank item
// slots (the bank is the bank bags, BANK_SLOT_BAG_START..END, which the callers scan on their own). The range is empty, so the
// scan finds nothing in them: "no item in those slots". Failure value: an empty range.
inline SlotRangeCompat KeyringSlotsCompat() { tbcompat::Unported(tbcompat::Feature::KeyringAndBankSlots); return { 0, 0 }; }
inline SlotRangeCompat BankItemSlotsCompat() { tbcompat::Unported(tbcompat::Feature::KeyringAndBankSlots); return { 0, 0 }; }

// The loot a creature or game object carries. T: the public member `loot`. F keeps loot per looting player (Loot.h, Creature::
// GetLootForPlayer). Failure value: nullptr ("no loot object"); the callers test the pointer first.
inline Loot* LootOfCreatureCompat(Creature* /*creature*/) { tbcompat::Unported(tbcompat::Feature::LootObjects); return nullptr; }
inline Loot* LootOfGameObjectCompat(GameObject* /*object*/) { tbcompat::Unported(tbcompat::Feature::LootObjects); return nullptr; }

// Whether the item prototype has this display id (T `proto->DisplayInfoID == displayId`). The mirror prototype has no display
// id. Failure value: false (no item looks like it).
inline bool ItemProtoHasDisplayIdCompat(ItemPrototype const* /*proto*/, uint32 /*displayId*/) { tbcompat::Unported(tbcompat::Feature::ItemDisplayId); return false; }

// Use / imbue an item by the use-item packet. T queues a CMSG_USE_ITEM (bag, slot, spell index, target flags, packed target guid)
// on the bot's session. F's packet is a different structure and a bot session has no client to send it. Failure value: nothing
// is queued ("the item was not used"); the caller is a void function.
inline void QueueUseItemPacketCompat(Player* /*bot*/, uint8 /*bagIndex*/, uint8 /*slot*/, uint8 /*spellIndex*/, uint16 /*targetFlag*/, ObjectGuid const& /*targetGuid*/)
{
    tbcompat::Unported(tbcompat::Feature::ItemUsePacket);
}

// Whether this prototype would go into the given equipment slot (T `slot == bot->FindEquipSlot(proto, NULL_SLOT, true)`). F's
// FindEquipSlot takes an Item. Failure value: false ("not for that slot").
inline bool ItemProtoFitsSlotCompat(Player* /*bot*/, ItemPrototype const* /*proto*/, uint32 /*slot*/) { tbcompat::Unported(tbcompat::Feature::ItemSlotOfPrototype); return false; }

// The suffix of a random-property item name ("of the Bear"). T reads ItemRandomProperties.dbc; F has item bonus lists. Failure
// value: false (no suffix, the name stays plain).
inline bool ItemRandomPropertySuffixCompat(int32 /*randomPropertyId*/, int /*localeIdx*/, std::string& /*suffix*/) { tbcompat::Unported(tbcompat::Feature::ItemRandomProperties); return false; }

// Whether the spell has the summon-critter effect (a companion pet item). Failure value: false.
inline bool SpellSummonsCritterCompat(SpellEntry const* /*spellInfo*/) { tbcompat::Unported(tbcompat::Feature::Companions); return false; }


// The random property id of an item (T `Item::GetItemRandomPropertyId()`), of a looted item (`LootItem::randomPropertyId`) and of
// a new item about to be stored (`Item::GenerateItemRandomPropertyId(itemId)`). F has item bonus lists instead. Failure value: 0
// ("no random property"), which is also what plain items answer on T.
inline int32 ItemRandomPropertyIdCompat(Item* /*item*/) { tbcompat::Unported(tbcompat::Feature::ItemRandomProperties); return 0; }
inline int32 ItemRandomPropertyIdCompat(LootItem const& /*item*/) { tbcompat::Unported(tbcompat::Feature::ItemRandomProperties); return 0; }
inline int32 NewItemRandomPropertyIdCompat(uint32 /*itemId*/) { tbcompat::Unported(tbcompat::Feature::ItemRandomProperties); return 0; }

// Type only. The auction code names `AuctionEntry` in declarations (RandomBotFacade.h, ItemUsageValue.h) and the slice never
// reaches it; ForeverCore has AuctionPosting instead. The record carries the one field the declarations read. Nothing creates one
// on ForeverCore, so the containers of it stay empty (UNPORTED.md "auction (type only)").
struct AuctionEntry
{
    uint32 itemTemplate = 0;
};


// ---------------------------------------------------------------------------------------------------------------
// PACKET HANDLER TABLES (PlayerbotAIPackets.cpp, PlayerbotAI.cpp opcode filter)
// ---------------------------------------------------------------------------------------------------------------
// The legacy handler tables and the opcode switch are written with the 1.12 opcode names. These names have no ForeverCore
// opcode (a bot session there neither sends nor receives these packets, and its layouts are 1.12's). So each name below is a
// number from a reserved block above ForeverCore's opcode range (its MAX_OPCODE is 0x3FFF): the tables and the switch compile and
// can never match a real packet. The tables are not registered (RegisterPacketHandlersCompat), so no packet handler of the legacy
// brain runs on ForeverCore: "incoming: ..." features of UNPORTED.md.
enum LegacyOpcodeCompat : uint16
{
    kLegacyOpcodeBaseCompat = 0xF000,
    CMSG_ACTIVATETAXI = kLegacyOpcodeBaseCompat + 1,
    CMSG_ACTIVATETAXIEXPRESS = kLegacyOpcodeBaseCompat + 2,
    CMSG_AREATRIGGER = kLegacyOpcodeBaseCompat + 3,
    CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK = kLegacyOpcodeBaseCompat + 4,
    CMSG_FORCE_RUN_SPEED_CHANGE_ACK = kLegacyOpcodeBaseCompat + 5,
    CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK = kLegacyOpcodeBaseCompat + 6,
    CMSG_FORCE_SWIM_SPEED_CHANGE_ACK = kLegacyOpcodeBaseCompat + 7,
    CMSG_FORCE_TURN_RATE_CHANGE_ACK = kLegacyOpcodeBaseCompat + 8,
    CMSG_FORCE_WALK_SPEED_CHANGE_ACK = kLegacyOpcodeBaseCompat + 9,
    CMSG_GAMEOBJ_USE = kLegacyOpcodeBaseCompat + 10,
    CMSG_GOSSIP_HELLO = kLegacyOpcodeBaseCompat + 11,
    CMSG_GROUP_UNINVITE = kLegacyOpcodeBaseCompat + 12,
    CMSG_GROUP_UNINVITE_GUID = kLegacyOpcodeBaseCompat + 13,
    CMSG_PUSHQUESTTOPARTY = kLegacyOpcodeBaseCompat + 14,
    CMSG_QUESTGIVER_ACCEPT_QUEST = kLegacyOpcodeBaseCompat + 15,
    CMSG_QUESTGIVER_COMPLETE_QUEST = kLegacyOpcodeBaseCompat + 16,
    CMSG_QUESTGIVER_HELLO = kLegacyOpcodeBaseCompat + 17,
    CMSG_TAXICLEARALLNODES = kLegacyOpcodeBaseCompat + 18,
    CMSG_TAXICLEARNODE = kLegacyOpcodeBaseCompat + 19,
    MSG_RAID_READY_CHECK = kLegacyOpcodeBaseCompat + 20,
    SMSG_BATTLEFIELD_STATUS = kLegacyOpcodeBaseCompat + 21,
    SMSG_CAST_RESULT = kLegacyOpcodeBaseCompat + 22,
    SMSG_FORCE_RUN_BACK_SPEED_CHANGE = kLegacyOpcodeBaseCompat + 23,
    SMSG_FORCE_RUN_SPEED_CHANGE = kLegacyOpcodeBaseCompat + 24,
    SMSG_FORCE_SWIM_BACK_SPEED_CHANGE = kLegacyOpcodeBaseCompat + 25,
    SMSG_FORCE_SWIM_SPEED_CHANGE = kLegacyOpcodeBaseCompat + 26,
    SMSG_FORCE_TURN_RATE_CHANGE = kLegacyOpcodeBaseCompat + 27,
    SMSG_FORCE_WALK_SPEED_CHANGE = kLegacyOpcodeBaseCompat + 28,
    SMSG_GROUP_INVITE = kLegacyOpcodeBaseCompat + 29,
    SMSG_GROUP_SET_LEADER = kLegacyOpcodeBaseCompat + 30,
    SMSG_LEVELUP_INFO = kLegacyOpcodeBaseCompat + 31,
    SMSG_LOG_XPGAIN = kLegacyOpcodeBaseCompat + 32,
    SMSG_LOOT_START_ROLL = kLegacyOpcodeBaseCompat + 33,
    SMSG_MESSAGECHAT = kLegacyOpcodeBaseCompat + 34,
    SMSG_QUESTGIVER_QUEST_DETAILS = kLegacyOpcodeBaseCompat + 35,
    SMSG_QUESTUPDATE_ADD_ITEM = kLegacyOpcodeBaseCompat + 36,
    SMSG_QUESTUPDATE_ADD_KILL = kLegacyOpcodeBaseCompat + 37,
    SMSG_QUESTUPDATE_COMPLETE = kLegacyOpcodeBaseCompat + 38,
    SMSG_QUESTUPDATE_FAILED = kLegacyOpcodeBaseCompat + 39,
    SMSG_QUESTUPDATE_FAILEDTIMER = kLegacyOpcodeBaseCompat + 40,
    SMSG_TRADE_STATUS_EXTENDED = kLegacyOpcodeBaseCompat + 41,
};

// A static check that a core-free constant of the legacy policy files equals the core's opcode of the same name. T: the check.
// F: the name is a reserved number (see above), there is no wire value to compare: true.
constexpr bool OpcodesAgreeCompat(uint32 /*policyValue*/, uint32 /*coreOpcode*/) { return true; }

// Reads a packed guid in the 1.12 layout (T `p >> guid.ReadAsPacked()`). F's packed guid has another layout and the 1.12 packets do
// not arrive; failure value: false, the guid stays empty ("not for this bot", every caller compares it with the bot's guid).
inline bool PackedGuidReadCompat(tbcompat::Feature feature, WorldPacket& /*packet*/, ObjectGuid& /*guid*/) { tbcompat::Unported(feature); return false; }

// Registers the handler rows of one legacy table. T: AddHandler for each row. F: nothing is registered; each row is counted under
// the group of what its handler does, and the direct cases of HandleBotOutgoingPacket (chat, spell failure and delay, speed
// change, knock back) are counted once, so the boot-time list shows what is dead.
template <class Helper, class Spec>
inline void RegisterPacketHandlersCompat(Helper& /*helper*/, Spec const* specs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        tbcompat::UnportedPacketHandler(std::string(specs[i].handler));
    tbcompat::Unported(tbcompat::Feature::PacketHandlerTables);
}


// Runs a block of the legacy code that ForeverCore cannot compile or run yet: `UnportedBlockCompat(feature, [&](auto* player, ...) {
// legacy code }, bot, ...)`. T (cmangos-compat-unported.h): calls the block with the arguments. F: counts the feature and runs nothing.
// The block is a generic lambda, so what it does with its `auto` arguments is not looked at on ForeverCore: only names that do not
// depend on them (types, constants) must exist there. Failure value: the Result form returns the value the caller gave.
template <class Block, class... Args>
inline void UnportedBlockCompat(tbcompat::Feature feature, Block&&, Args&&...) { tbcompat::Unported(feature); }
template <class Result, class Block, class... Args>
inline Result UnportedBlockResultCompat(tbcompat::Feature feature, Result failure, Block&&, Args&&...) { tbcompat::Unported(feature); return failure; }

// The packet cases HandleBotOutgoingPacket handles directly (not through a table). On F none of them can match a packet, so
// they are counted once per bot with the tables.
inline void RegisterDirectPacketCasesCompat()
{
    tbcompat::Unported(tbcompat::Feature::IncomingChat);
    tbcompat::Unported(tbcompat::Feature::IncomingSpellState);
    tbcompat::Unported(tbcompat::Feature::IncomingSpeedChange);
    tbcompat::Unported(tbcompat::Feature::IncomingKnockBack);
}


// The world's average update time in ms (T `sWorld.GetAverageDiff()`), added to a jump's landing time. F keeps no such average (the
// host provides one later, R4M request 5). Failure value: 0 (no allowance for server lag).
inline uint32 WorldAverageDiffCompat() { tbcompat::Unported(tbcompat::Feature::WorldAverageDiff); return 0; }

// Fall bookkeeping for fall damage: T `bot->SetFallInformation(z)` (time 0), F `SetFallInformation(time, z)` with a different
// meaning of the pair. Failure value: nothing is recorded ("no fall damage is computed from this jump").
inline void SetFallInformationCompat(Player* /*player*/, float /*z*/) { tbcompat::Unported(tbcompat::Feature::JumpFall); }

// @@END@@
#endif // TB_UNPORTED_TABLE_ONLY

