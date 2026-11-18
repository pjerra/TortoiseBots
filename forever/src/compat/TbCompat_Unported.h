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
    MSG_MOVE_STOP = kLegacyOpcodeBaseCompat + 42,
    MSG_MOVE_FALL_LAND = kLegacyOpcodeBaseCompat + 43,
    MSG_MOVE_SET_FACING = kLegacyOpcodeBaseCompat + 44,
    MSG_MOVE_START_BACKWARD = kLegacyOpcodeBaseCompat + 45,
    MSG_MOVE_START_FORWARD = kLegacyOpcodeBaseCompat + 46,
    MSG_MOVE_KNOCK_BACK = kLegacyOpcodeBaseCompat + 47,
    MSG_MOVE_JUMP = kLegacyOpcodeBaseCompat + 48,
    MSG_MINIMAP_PING = kLegacyOpcodeBaseCompat + 49,
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


// ===============================================================================================================
// TALENTS, GUID FROM SPAWN DATA, BATTLEGROUND QUEUE, CHAT LINKS (Talentspec.h, GuidPosition.h, ServerFacade.h, PlayerbotAI.h)
// ===============================================================================================================
// The talent points a character has in total (T: the level rule plus bonus points times the talent rate) and the free ones to
// spend. Failure value: 0 ("no talent points"), so nothing is spent.
inline uint32 TotalTalentPointsCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::Talents); return 0; }
inline uint32 FreeTalentPointsCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::Talents); return 0; }
// The tab page of a talent tab row (T `tabpage`). The row type is only declared here. Failure value: page 0.
template <class Tab>
inline int TalentTabPageCompat(Tab const* /*tab*/) { tbcompat::Unported(tbcompat::Feature::Talents); return 0; }

// An object guid from a creature or game object spawn record (T `ObjectGuid(HIGHGUID_UNIT, creature entry, spawn counter)`). Failure
// value: the empty guid ("no such object"); the record's position is still read by the caller's WorldPosition part.
template <class DataPair>
inline ObjectGuid GuidOfCreatureDataCompat(DataPair const* /*dataPair*/) { tbcompat::Unported(tbcompat::Feature::SpawnGuid); return ObjectGuid::Empty; }
template <class DataPair>
inline ObjectGuid GuidOfGameObjectDataCompat(DataPair const* /*dataPair*/) { tbcompat::Unported(tbcompat::Feature::SpawnGuid); return ObjectGuid::Empty; }

// Writes a GuidPosition (guid then position) into a packet buffer. Failure value: nothing is written.
template <class Pos, class GuidPos>
inline void GuidPositionWriteCompat(ByteBuffer& /*buffer*/, GuidPos& /*guidPosition*/) { tbcompat::Unported(tbcompat::Feature::GuidPositionWire); }

// The queue type id of the pre-included battleground code under its Turtle spelling.
using BattleGroundQueueTypeId = BattlegroundQueueTypeId;
// (The battleground type of a queue type id, and the chat link extractors of ChatHandler, are blocks: ServerFacade.h and
// PlayerbotAI.h call them through UnportedBlockResultCompat with Feature::BattlegroundQueue / Feature::ChatLinks.)


// ===============================================================================================================
// PROPER both-core helpers added by R6A (ForeverCore has the thing: no Unported call)
// ===============================================================================================================
// The ammo item the bot's ranged weapon draws from. T `GetUInt32Value(PLAYER_AMMO_ID)`, F (Classic 1.60) `Player::GetAmmoId()`.
inline uint32 AmmoItemIdCompat(Player const* player) { return player->GetAmmoId(); }

// Silence, pacify and the school lockout, for the wand trigger. T reads the unit flags (UNIT_FLAG_SILENCED / UNIT_FLAG_PACIFIED)
// and Unit::IsSpellProhibited; F has the pacify flag, a silence AURA (SPELL_AURA_MOD_SILENCE, no flag) and SpellHistory school
// lockouts. Each asks the same question on both cores.
inline bool UnitSilencedCompat(Unit const* unit) { return unit->HasAuraType(SPELL_AURA_MOD_SILENCE); }
inline bool UnitPacifiedCompat(Unit const* unit) { return unit->HasUnitFlag(UNIT_FLAG_PACIFIED); }
inline bool SpellProhibitedCompat(Player const* player, SpellEntry const* spellInfo)
{
    return player->GetSpellHistory()->IsSchoolLocked(spellInfo->GetSpellSchoolMask());
}

// ===============================================================================================================
// PETS, GAME OBJECTS, ITEM CASTS, MISC FEATURES (the "Unported" helpers: failure value in each comment)
// ===============================================================================================================
// Pets. Failure values: nothing learned / toggled / sent / visited ("the action did not happen").
inline void PetLearnSpellCompat(Pet* /*pet*/, uint32 /*spellId*/) { tbcompat::Unported(tbcompat::Feature::Pets); }
inline void PetToggleAutocastCompat(Pet* /*pet*/, uint32 /*spellId*/, bool /*on*/) { tbcompat::Unported(tbcompat::Feature::Pets); }
inline bool PetDismissCommandCompat(Player* /*bot*/, Pet* /*pet*/) { tbcompat::Unported(tbcompat::Feature::Pets); return false; }
template <class Visitor>
inline void ForAllGuardiansCompat(Player* /*bot*/, Visitor&&) { tbcompat::Unported(tbcompat::Feature::Pets); }

// Use a game object by the 1.12 use packet. Failure value: false ("could not use it").
inline bool UseGameObjectCompat(Player* /*bot*/, GameObject* /*object*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }

// The skill ForeverCore does not name. Failure value: false ("not trained").
template <class AI>
inline bool HasThrownSkillCompat(AI* /*ai*/) { tbcompat::Unported(tbcompat::Feature::Skills); return false; }

// Creature immunity masks (T `CreatureInfo::school_immune_mask` / `mechanic_immune_mask`). Failure value: 0 ("immune to nothing").
inline uint32 CreatureSchoolImmuneMaskCompat(CreatureInfo const* /*info*/) { tbcompat::Unported(tbcompat::Feature::CreatureImmunity); return 0; }
inline uint32 CreatureMechanicImmuneMaskCompat(CreatureInfo const* /*info*/) { tbcompat::Unported(tbcompat::Feature::CreatureImmunity); return 0; }

// Whether a player is a hidden GM (T `GetVisibility() == VISIBILITY_OFF`). Failure value: false ("not hidden").
inline bool PlayerHiddenGmCompat(Player* /*player*/) { tbcompat::Unported(tbcompat::Feature::GmVisibility); return false; }

// The heal one effect of a spell would do for the bot (T: CalculateSpellDamage then SpellHealingBonusDone, 0 when the base is not
// positive). Failure value: 0 ("this effect heals nothing"); the caller skips a spell that heals nothing.
inline int32 EstimateHealOfEffectCompat(Player* /*bot*/, Unit* /*target*/, SpellEntry const* /*info*/, uint32 /*effect*/)
{
    tbcompat::Unported(tbcompat::Feature::HealEstimate);
    return 0;
}

// An item cast with a hand-built Spell: the loop of CastItemTargetAction::Execute. Failure value: false (the cast did not start).
template <class SpellType>
inline bool CastItemOnTargetCompat(Player* /*bot*/, Unit* /*target*/, Item* /*item*/, ItemPrototype const* /*proto*/)
{
    tbcompat::Unported(tbcompat::Feature::ItemCasts);
    return false;
}
// The triggered flags of the n-th spell of an item cast (T: TRIGGERED_OLD_TRIGGERED after the first), and the item cooldowns.
inline void AddSpellAndCategoryCooldownsCompat(Player* /*bot*/, SpellEntry const* /*spellInfo*/, uint32 /*itemId*/) { tbcompat::Unported(tbcompat::Feature::ItemCasts); }
// Base class of ai::BotUseItemSpell: T's Spell. F cannot build a Spell from the mirror SpellEntry, so the class is only a
// declaration there: its constructor counts the feature (nothing can reach it in the slice: the item cast above returns first).
class ItemCastSpellBaseCompat
{
public:
    ItemCastSpellBaseCompat(Unit* /*caster*/, SpellEntry const* /*info*/, bool /*triggered*/, ObjectGuid const& /*originalCaster*/, SpellEntry const* /*triggeredBy*/)
    {
        tbcompat::Unported(tbcompat::Feature::ItemCasts);
    }
    void SetCastItem(Item* /*item*/) {}
};

// The strategy store (ai_playerbot_*): not in ForeverCore's schema. Begin returns false ("no transaction, store nothing").
inline bool DbStoreBeginCompat(uint32 /*serialId*/) { tbcompat::Unported(tbcompat::Feature::StrategyStoreTransactions); return false; }
inline void DbStoreCommitCompat() {}
// The custom strategy lines of one owner (T: SELECT from ai_playerbot_custom_strategy). Failure value: no lines.
inline void LoadCustomStrategyLinesCompat(std::string const& /*qualifier*/, uint32 /*owner*/, std::list<std::string>& /*lines*/)
{
    tbcompat::Unported(tbcompat::Feature::CustomStrategyStore);
}

// ===============================================================================================================
// DATABASE READS WITH PRINTF FORMATS (findings item 18)
// ===============================================================================================================
// `CharacterDatabase.PQuery("... '%s' ... '%u'", ...)` COMPILES on ForeverCore but its PQuery takes `{}` formats and silently drops
// the arguments. This helper formats with printf (the Turtle rules), escapes nothing by itself (callers pass escaped, quoted
// strings as they do today) and sends the text with Query(). String arguments must be `char const*`.
std::string LegacyFormatCompat(char const* format, ...);
template <class... Args>
inline QueryResult CharacterPQueryCompat(char const* format, Args... args)
{
    static_assert((!std::is_same_v<std::decay_t<Args>, std::string> && ...), "pass char const* to a printf format");
    return CharacterDatabase.Query(LegacyFormatCompat(format, args...).c_str());
}
// A guard for what CharacterPQueryCompat returns: the legacy code wrapped the raw result in a unique_ptr; F's result is shared.
inline QueryResult QueryGuardCompat(QueryResult result) { return result; }
// Escapes a string for a query (T `CharacterDatabase.escape_string(s)`, F `EscapeString(s)`).
inline void EscapeStringCompat(std::string& text) { CharacterDatabase.EscapeString(text); }


// ===============================================================================================================
// QUEST INTERNALS (PlayerbotAIQuests.cpp, GenericTriggers.h QuestLogNearlyFullTrigger)
// ===============================================================================================================
// What the quest triage reads of a quest template. T fills it from the 1.12 fields; F's Quest keeps objectives and a classic level
// range, and the view is not ported: it is EMPTY (level 0 = "scaling quest, never grey", no item objectives, not inactive, no
// money cost, no deliver flag). The trigger that uses it (quest log nearly full) is not registered in the slice.
struct QuestTriageViewCompat
{
    struct Item { uint32 id = 0; uint32 count = 0; };
    bool inactive = false;               // T: !quest->IsActive()
    bool hasItemObjective = false;       // an item objective or a source item with a count
    bool deliver = false;                // T: HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER)
    uint32 level = 0;                     // T: GetQuestLevel()
    uint32 levelForPlayer = 0;            // T: bot->GetQuestLevelForPlayer(quest)
    uint32 type = 0;                     // T: GetType()
    uint32 suggestedPlayers = 0;         // T: GetSuggestedPlayers()
    int32 rewardOrRequiredMoney = 0;     // T: GetRewOrReqMoney() (negative = the quest costs money)
    std::vector<Item> items;             // the required items with a count
};
inline QuestTriageViewCompat QuestTriageViewOfCompat(Player* /*bot*/, Quest const* /*quest*/)
{
    tbcompat::Unported(tbcompat::Feature::QuestInternals);
    return QuestTriageViewCompat();
}

// Quest log bookkeeping of PlayerbotAI. Failure values: no title (empty), nothing dropped, no required items.
inline std::string QuestTitleCompat(Quest const* /*quest*/) { tbcompat::Unported(tbcompat::Feature::QuestInternals); return std::string(); }
inline void RemoveQuestFromLogCompat(Player* /*bot*/, uint32 /*questId*/) { tbcompat::Unported(tbcompat::Feature::QuestInternals); }
inline std::vector<std::pair<uint32, uint32>> QuestRequiredItemsCompat(Quest const* /*quest*/)
{
    tbcompat::Unported(tbcompat::Feature::QuestInternals);
    return {};
}


// ===============================================================================================================
// TAXI (TaxiRouteGuard.h, MovementActions.cpp UseTaxi, PlayerbotAI.cpp)
// ===============================================================================================================
// Finish a started bot flight at once at the destination node (the test shortcut AiPlayerbot.BotInstantFlight). Failure value:
// false ("the bot did not land"; the flight keeps flying as normal).
inline bool TaxiInstantFinishCompat(Player* /*bot*/, uint32 /*destNode*/) { tbcompat::Unported(tbcompat::Feature::Taxi); return false; }


// ===============================================================================================================
// TRANSPORTS, GAME OBJECT PORTALS, NAVMESH PROBE (MovementActions.cpp)
// ===============================================================================================================
// The spline of a bot standing on a transport. T: points are transport-local offsets and the spline is told which transport
// (`SetTransport(guidLow)`), so the dispatch turns its world points into offsets just before launching. F: the spline turns
// WORLD points into transport-local ones itself when the unit is on a transport (MoveSplineInit.cpp:310, TransformForTransport), so
// both calls do nothing: the points stay world points. PROPER both-core helpers (the answer is the same: the bot walks the path).
template <class Init>
inline void SplineSetTransportCompat(Init& /*init*/, GenericTransport* /*transport*/) {}
inline void SplinePointToTransportCompat(GenericTransport* /*transport*/, float& /*x*/, float& /*y*/, float& /*z*/) {}

// The game object portal of a travel path node: the spell id of a spellcaster game object entry (T: `spellcaster.spellId`, only for
// GAMEOBJECT_TYPE_SPELLCASTER). Failure value: false ("not a portal object").
inline bool PortalSpellOfGameObjectCompat(uint32 /*entry*/, uint32& /*spellId*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }
// Use the portal object if the bot can interact with it (T: GetGameObjectIfCanInteractWith, then the use packet). Failure value: false.
inline bool UsePortalObjectCompat(Player* /*bot*/, GameObject* /*object*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }

// Where the flight the player is on ends (T: the last point of Taxi::Map of the player's taxi path). Failure value: false ("no known
// destination").
inline bool TaxiDestinationCompat(Player* /*player*/, uint32& /*mapId*/, float& /*x*/, float& /*y*/, float& /*z*/) { tbcompat::Unported(tbcompat::Feature::Taxi); return false; }

// The navmesh query object of a map and the core's walkable-poly pick (the navmesh-island probe, WorldPosition's path walks). T: the
// MMapManager singleton and `PathInfo::FindWalkPoly`. F's MMapManager is keyed by map, instance and phase and has no such pick.
// Failure values: nullptr ("no navmesh for that map": every caller then answers false) and the invalid poly ref 0.
inline dtNavMeshQuery const* NavMeshQueryCompat(uint32 /*mapId*/) { tbcompat::Unported(tbcompat::Feature::NavmeshProbe); return nullptr; }
template <class... Args>
inline dtPolyRef NavMeshFindWalkPolyCompat(Args&&...) { tbcompat::Unported(tbcompat::Feature::NavmeshProbe); return 0; }


// ===============================================================================================================
// PLAYERBOTAI.CPP (cinematics, loot window, facing, instance bind, mounts, camp, speed cheat, logout, movement stop, addon chat,
// portals, taxi follow, text emotes, spawn lookups, guild rank, camera)
// ===============================================================================================================
// Cinematic: T ends a cinematic the bot is watching (a headless bot cannot finish it). Failure value: nothing is ended.
inline void EndCinematicIfWatchingCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::CinematicCamera); }

// The loot window of a player (T: `GetLootGuid()` and the loot manager). Failure value: the empty guid ("no loot window open") and
// nothing released.
inline ObjectGuid LootGuidCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::LootObjects); return ObjectGuid::Empty; }
template <class GuidPos>
inline void ReleaseLootIfFarCompat(Player* /*bot*/, GuidPos& /*lootObject*/, bool /*shouldRelease*/) { tbcompat::Unported(tbcompat::Feature::LootObjects); }

// PROPER: the clients are told about a new facing by F's SetFacingTo itself (a facing spline), so the extra facing broadcast of T
// (MSG_MOVE_SET_FACING from the mover) has nothing left to do.
inline void BroadcastFacingCompat(Player* /*bot*/) {}

// Whether a bot may enter an area trigger's map (instance full, encounter in progress, bound to another save). Failure value: true
// ("no restriction known": the dungeon is treated as unbound, ForeverCore's own teleport checks still apply).
template <class Area>
inline bool CanEnterAreaCompat(Player* /*bot*/, Area const* /*area*/) { tbcompat::Unported(tbcompat::Feature::InstanceBind); return true; }

// PROPER: take the bot off its mount. T removes the mount auras, unmounts and recomputes the run speed (forced and not); F's
// Dismount() and a speed update do the same.
inline void DismountCompat(Player* player)
{
    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
    player->Dismount();
    player->UpdateSpeed(MOVE_RUN);
}

// Skill writes of the auto tools kit and the camp kit (T SetSkill(skill, value, max)); the lockpicking and survival lines.
inline void EnsureLockpickSkillCompat(Player* /*bot*/, uint32 /*lockpickMax*/) { tbcompat::Unported(tbcompat::Feature::Skills); }
inline void EnsureCampSurvivalSkillCompat(Player* /*bot*/, uint32 /*cap*/) { tbcompat::Unported(tbcompat::Feature::Camp); }
inline void SetRestBonusCompat(Player* /*bot*/, float /*bonus*/) { tbcompat::Unported(tbcompat::Feature::Camp); }
inline void ClearWaterBreathingCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::Camp); }
// The zone of a fishing area (T `AreaEntry::GetById(id)->ZoneId`). Failure value: 0 ("no zone found": the caller keeps the area id).
inline uint32 FishingZoneOfAreaCompat(uint32 /*areaId*/) { tbcompat::Unported(tbcompat::Feature::Camp); return 0; }
// The movespeed cheat (T `UpdateSpeed(type, true, 10)` for walk, run and swim). Failure value: no change.
inline void SpeedCheatCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::SpeedCheat); }

// PROPER: whether the session's account logs out at once. T compares its security level with the InstantLogout setting; F has the
// permission RBAC_PERM_INSTANT_LOGOUT for exactly that (MiscHandler.cpp:251).
inline bool SessionHasInstantLogoutCompat(WorldSession* session) { return session->HasPermission(rbac::RBAC_PERM_INSTANT_LOGOUT); }

// T clears MOVEFLAG_ONTRANSPORT after RemovePassenger; F has no such flag (the transport is in the movement info) and its
// RemovePassenger clears that state, so there is nothing to drop (only reached inside the transport blocks, which do not run on F).
template <class P>
inline void RemoveOnTransportFlagCompat(P* /*bot*/) {}

// The movement generator interrupt and the movement heartbeat of T. Failure value: nothing is sent or interrupted.
inline void InterruptCurrentMovementCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::MovementInterrupt); }
inline void SendHeartBeatCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::MovementInterrupt); }

// PROPER: drop stale transport state. T: no transport but MOVEFLAG_ONTRANSPORT still set -> clear the flag and the transport data.
// F keeps the transport in the movement info (transport.guid), with no such flag.
inline void ClearStaleTransportStateCompat(Player* bot)
{
    if (!bot->GetTransport() && !bot->m_movementInfo.transport.guid.IsEmpty())
        bot->m_movementInfo.transport.Reset();
}

// PROPER: stop the bot where it stands. T sets the movement flags, stops, synthesizes MSG_MOVE_STOP through the movement handler and
// puts the idle generator in; F's Unit::StopMoving() stops the spline and tells the clients, then the generators are cleared.
inline void StopMovingCompat(Player* bot)
{
    bot->StopMoving();
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        MotionClearCompat(*bot->GetMotionMaster(), false, true);
        bot->GetMotionMaster()->MoveIdle();
    }
}

// Addon messages to a player and the addon chat packet (T SendAddonMessage, BuildChatPacket with CHAT_MSG_ADDON). Failure value: the
// message is not sent.
inline void SendAddonMessageCompat(Player& /*to*/, char const* /*prefix*/, std::string const& /*message*/) { tbcompat::Unported(tbcompat::Feature::AddonChat); }
template <class Facade>
inline void SendAddonChatCompat(Facade& /*facade*/, Player* /*from*/, Player* /*to*/, std::string const& /*text*/) { tbcompat::Unported(tbcompat::Feature::AddonChat); }

// Party portal game objects (the mage portal the bot follows). Failure values: not a portal object / not blocked / cannot interact.
inline bool PartyPortalSpellOfObjectCompat(GameObject* /*go*/, uint32& /*spellId*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }
inline bool GameObjectNoInteractCompat(GameObject* /*go*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }
inline bool GameObjectCanInteractCompat(Player* /*bot*/, GameObject* /*go*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return false; }
inline void SendGameObjectUseCompat(Player* /*bot*/, GameObject* /*go*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); }

// Taxi follow. Failure values: no endpoints / no node position.
inline bool TaxiSourceDestinationCompat(Player* /*player*/, uint32& /*src*/, uint32& /*dst*/) { tbcompat::Unported(tbcompat::Feature::Taxi); return false; }
inline bool TaxiNodePositionCompat(uint32 /*node*/, uint32& /*mapId*/, float& /*x*/, float& /*y*/, float& /*z*/) { tbcompat::Unported(tbcompat::Feature::Taxi); return false; }
// PROPER: the flight master the bot can talk to (T takes the NPC flag only, F also the second flag word).
inline Creature* FlightMasterIfCanInteractCompat(Player* bot, ObjectGuid const& guid) { return bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE); }

// PROPER: play a text emote as the bot (T builds the CMSG text emote packet by hand and feeds the handler; F's handler takes a typed
// packet). F has no donor sound table, so the sound index is "none" (-1), as the legacy comment says of the pinned core.
inline void PlayTextEmoteCompat(Player* bot, uint32 emote, uint32 /*variant*/, ObjectGuid const& target)
{
    WorldPackets::Chat::CTextEmote packet{ WorldPacket(CMSG_SEND_TEXT_EMOTE) };
    packet.Target = target;
    packet.EmoteID = int32(emote);
    packet.SoundIndex = -1;
    bot->GetSession()->HandleTextEmoteOpcode(packet);
}

// Unit and object lookups in a map by guid (T Map::GetUnit / GetAnyTypeCreature / GetWorldObject). PROPER: F's Map finds creatures,
// game objects and players by guid; the type switch is the guid's own.
inline Unit* UnitInMapCompat(Map* map, ObjectGuid const& guid)
{
    if (guid.IsPlayer())
        return map->GetPlayer(guid);
    if (guid.IsPet())
        return map->GetPet(guid);
    return map->GetCreature(guid);
}
inline Creature* AnyTypeCreatureInMapCompat(Map* map, ObjectGuid const& guid) { return map->GetCreature(guid); }
inline WorldObject* WorldObjectInMapCompat(Map* map, ObjectGuid const& guid)
{
    if (guid.IsGameObject() || guid.IsMOTransport())
        return map->GetGameObject(guid);
    if (guid.IsPlayer())
        return map->GetPlayer(guid);
    if (guid.IsPet())
        return map->GetPet(guid);
    if (guid.IsCorpse())
        return map->GetCorpse(guid);
    return map->GetCreature(guid);
}

// The bot's rank in its guild and the number of ranks (T Guild::GetRanksSize, GetMemberSlot()->RankId). Failure value: false ("no
// rank data": the caller answers with the unscaled size).
template <class GuildType>
inline bool GuildRanksOfBotCompat(GuildType* /*guild*/, ObjectGuid const& /*botGuid*/, uint32& /*maxRank*/, uint32& /*botRank*/) { tbcompat::Unported(tbcompat::Feature::GuildRanks); return false; }

// PROPER: the object a player's client looks through (a farsight or cinematic camera body). T `GetCamera().GetBody()`, F
// `GetViewpoint()` (nullptr without a farsight; the caller ignores the player itself and objects not in the world).
inline WorldObject* PlayerViewObjectCompat(Player* player) { return player->GetViewpoint(); }

// @@END@@
#endif // TB_UNPORTED_TABLE_ONLY

