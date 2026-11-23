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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "ai/playerbot/UnportedFeatures.h"   // the closed list of feature names (core-free: the Turtle side names the same features)
#ifndef TB_UNPORTED_TABLE_ONLY
#include "CinematicMgr.h"       // EndCinematicIfWatchingCompat (a core type: the table-only includers do not need it)
#include "InstanceLockMgr.h"    // CanEnterAreaCompat (InstanceLock, MapDb2Entries)
#endif

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

// ItemProtoHasDisplayIdCompat and QueueUseItemPacketCompat moved to TbCompat_I2.h.

// Whether this prototype would go into the given equipment slot (T `bot->FindEquipSlot(proto, slot, true) == slot`). F's
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
    // The fields the price code of ItemUsageValue.cpp reads (slice 9, package L9). Never set: nothing creates a listing here, so every container of
    // AuctionEntry is empty and these are only named, not read from a real auction.
    uint32 Id = 0;
    uint32 itemGuidLow = 0;
    uint32 itemCount = 0;
    uint32 buyout = 0;
    uint32 owner = 0;
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

// Fall bookkeeping for fall damage: T `bot->SetFallInformation(z)` (time 0), F `SetFallInformation(time, z)`: the same start
// height the core's own CMSG_MOVE_FALL_LAND path writes, so HandleFall computes the damage (S3).
inline void SetFallInformationCompat(Player* player, float z) { player->SetFallInformation(0, z); }


// ===============================================================================================================
// TALENTS, GUID FROM SPAWN DATA, BATTLEGROUND QUEUE, CHAT LINKS (Talentspec.h, GuidPosition.h, ServerFacade.h, PlayerbotAI.h)
// ===============================================================================================================
// The talent points a character has in total (T: the level rule plus bonus points times the talent rate) and the free ones to
// spend. Failure value: 0 ("no talent points"), so nothing is spent.
inline uint32 TotalTalentPointsCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::Talents); return 0; }
inline uint32 FreeTalentPointsCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::Talents); return 0; }
// Whether the premade talent specs of the config are read and checked (R9). They are not ported: the talent tables behind TalentSpec are stand-ins that
// refuse every link, which printed an error per configured spec at init. The config load asks this once, skips the spec loop and the line below is the
// whole report (one line, no flood). Failure value: false.
inline bool PremadeSpecsPortedCompat()
{
    tbcompat::Unported(tbcompat::Feature::TalentSpecs);
    sLog.outString("TortoiseBots: premade talent specs are not ported (no spec is read, no talent is chosen)");
    return false;
}
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

template <class Handler>
inline uint32 ExtractQuestIdCompat(Handler* handler, std::string str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first != std::string::npos)
    {
        size_t last = str.find_last_not_of(" \t\r\n");
        std::string trimmed = str.substr(first, last - first + 1);
        if (!trimmed.empty() && trimmed.size() <= 9 &&
            std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return std::isdigit(c); }))
        {
            unsigned long const value = std::strtoul(trimmed.c_str(), nullptr, 10);
            if (value > 0 && value <= std::numeric_limits<uint32>::max())
                return static_cast<uint32>(value);
        }
    }
    char* source = (char*)str.c_str();
    char* cId = UnportedBlockResultCompat(tbcompat::Feature::ChatLinks, static_cast<char*>(nullptr),
        [&](auto* self) -> char* { return self->ExtractKeyFromLink(&source, "Hquest"); }, handler);
    return cId ? static_cast<uint32>(std::strtoul(cId, nullptr, 10)) : 0u;
}


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
// (Pets: PetLearnSpellCompat, PetToggleAutocastCompat, PetDismissCommandCompat, ForAllGuardiansCompat moved to TbCompat_P11.h in slice 11; found through
// ai/playerbot/TbCompatP11.h.)

// Use a game object by the 1.12 use packet. Failure value: false ("could not use it").
namespace tbcompat { bool UseGameObject(Player* bot, GameObject* object); }
inline bool UseGameObjectCompat(Player* bot, GameObject* object) { return tbcompat::UseGameObject(bot, object); }

// The skill ForeverCore does not name. Failure value: false ("not trained").
template <class AI>
inline bool HasThrownSkillCompat(AI* /*ai*/) { tbcompat::Unported(tbcompat::Feature::Skills); return false; }

// PROPER (R9): the creature template's immunity masks. T keeps them as two masks on the template (`CreatureInfo::school_immune_mask`, bit
// `1 << school`; `mechanic_immune_mask`, bit `1 << (mechanic - 1)`, Creature.cpp:2193). F keeps them in a CreatureImmunities row that the template names
// (`CreatureTemplate::CreatureImmunitiesId`, CreatureData.h:533; the row is `SpellMgr::GetCreatureImmunities(id)`, SpellMgr.h:570 / :750) and
// `Creature::LoadTemplateImmunities` (Creature.cpp:2561) applies as the creature's school (`1 << school`) and mechanic (index = the Mechanics value)
// immunities. The helpers rebuild T's two masks from that row, so the school gate (`SpellSchoolImmune`) and the stun selection
// (`StunMechanicImmune`, bit MECHANIC_STUN - 1) read what they read on T. A template with no row is immune to nothing, as on T.
inline uint32 CreatureSchoolImmuneMaskCompat(CreatureInfo const* info)
{
    CreatureImmunities const* const immunities = info ? SpellMgr::GetCreatureImmunities(info->CreatureImmunitiesId) : nullptr;
    uint32 mask = 0;
    if (immunities)
        for (std::size_t school = 0; school < immunities->School.size() && school < 32; ++school)
            if (immunities->School[school])
                mask |= 1u << school;
    return mask;
}
inline uint32 CreatureMechanicImmuneMaskCompat(CreatureInfo const* info)
{
    CreatureImmunities const* const immunities = info ? SpellMgr::GetCreatureImmunities(info->CreatureImmunitiesId) : nullptr;
    uint32 mask = 0;
    if (immunities)
        for (std::size_t mechanic = 1; mechanic < immunities->Mechanic.size() && mechanic <= 32; ++mechanic)
            if (immunities->Mechanic[mechanic])
                mask |= 1u << (mechanic - 1);
    return mask;
}

// PROPER (R9): whether a player is a hidden GM. T `GetVisibility() == VISIBILITY_OFF` (`.gm visible off`); F `.gm visible off` sets
// PLAYER_EXTRA_GM_INVISIBLE, which `isGMVisible()` reads (Player.h:1341).
inline bool PlayerHiddenGmCompat(Player* player) { return !player->isGMVisible(); }

// PROPER (slice 2, R4; R5 item 10): the heal one effect of a spell would do for the bot, as an EXPECTED value. T: CalculateSpellDamage (which rolls the
// effect's dice with irand, Object.cpp:4440-4482, and applies the caster's spell mods) then SpellHealingBonusDone, 0 when the base is not positive; Turtle
// leaves out the target's healing-taken modifiers. F: the cast's own steps as Spell::EffectHeal does them (SpellEffects.cpp:1258-1298):
//  1. SpellEffectInfo::CalcValue (base points, level scaling, the vanilla dice row). It rolls a random spread (SpellInfo.cpp:625-636: value += basePoints *
//     frand(-delta, delta)) and hands the roll back through `variance`; the roll is taken out again (value - CalcBaseValue * roll), which leaves the average of
//     the dice. A spell-school heal is not rolled there (ClassicRollsWholeAmount: EffectHeal rolls the whole amount later), so nothing is taken out for it (the roll is symmetric around 0, and so is the final `bonus + bonus * variance` of EffectHeal): the choice of a rank is deterministic.
//  2. Unit::SpellHealingBonusDone (Unit.h:1720, the +healing bonus; no Spell object, so no spell mods of a cast in flight),
//  3. Unit::SpellHealingBonusTaken on the target (Unit.h:1722: a Mortal-Strike-style reduction, +healing taken): the one step Turtle's estimate omits, added
//     here so the downrank does not pick a heal that is too small for a wounded target that heals less (0 when the result is not positive).
// A spell the mirror has no core record for heals nothing (0: the downrank rule skips it and keeps the top rank, as before).
inline int32 EstimateHealOfEffectCompat(Player* bot, Unit* target, SpellEntry const* info, uint32 effect)
{
    SpellInfo const* spellInfo = GetSpellInfoCompat(info);
    if (!bot || !target || !spellInfo || effect >= spellInfo->GetEffects().size())
        return 0;
    SpellEffectInfo const& effectInfo = spellInfo->GetEffect(SpellEffIndex(effect));
    float roll = 0.0f;
    double expected = double(effectInfo.CalcValue(bot, nullptr, target, &roll));
    if (roll != 0.0f && !effectInfo.ClassicRollsWholeAmount())
        expected -= double(effectInfo.CalcBaseValue(bot, target, 0, -1)) * double(roll);
    int32 const base = int32(std::lround(expected));
    if (base <= 0)
        return 0;
    int32 const done = bot->SpellHealingBonusDone(target, spellInfo, base, HEAL, effectInfo);
    if (done <= 0)
        return 0;
    int32 const taken = target->SpellHealingBonusTaken(bot, spellInfo, done, HEAL);
    return taken > 0 ? taken : 0;
}

// CastItemOnTargetCompat moved to TbCompat_I2.h.
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

// The strategy store (ai_playerbot_db_store): transactions run in SliceDbStore via CharacterTransactGuardedCompat.
inline bool DbStoreBeginCompat(uint32 /*serialId*/) { return true; }
inline void DbStoreCommitCompat() {}
inline uint64 DbStoreBotGuidCompat(Player* bot) { return bot ? bot->GetGUID().GetCounter() : 0; }
QueryResult DbStoreQueryValuesCompat(uint64 guid);
void DbStoreDeleteValuesCompat(uint64 guid);
QueryResult DbStoreQueryPresetsCompat(uint64 guid);
// The custom strategy lines of one owner (T: SELECT from ai_playerbot_custom_strategy); implemented in forever/src/slice/SliceCustomStrategy.cpp.
void LoadCustomStrategyLinesCompat(std::string const& qualifier, uint32 owner, std::list<std::string>& lines);

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
// The pet a character brings back on login (characters.summonedPetNumber): the host writes it right after SavePetToDB, before a map change can put the
// pet away. T queues the statement; F runs it at once (`DirectPExecute`) for that reason.
inline void SaveSummonedPetNumberCompat(uint32 petNumber, uint32 guid)
{
    CharacterDatabase.DirectPExecute("UPDATE characters SET summonedPetNumber = {} WHERE guid = {}", petNumber, guid);
}


// Quest internals moved to TbCompat_Quest.h (W3).


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

// PROPER: the navmesh query object of a map (T `MMapManager::GetNavMeshQuery(mapId)`, one mesh per map; F keys it by mesh map, instance
// map and instance id, so the map's own id is passed for the first two, as the continents and TbCompat_C.cpp do). nullptr when no mesh
// is loaded for it. Defined in TbCompat_Unported.cpp (the MMapManager header is not in the pre-included set).
dtNavMeshQuery const* NavMeshQueryCompat(uint32 mapId, uint32 instanceId = 0);
// The position a player reads on the zone map (T `Map2ZoneCoordinates`, F `DB2Manager::Map2ZoneCoordinates`: the UI map position). Defined in TbCompat_Unported.cpp.
// DIFFERENT ANSWER, display only.
void Map2ZoneCoordinatesCompat(float& x, float& y, uint32 zone);
// The navmesh-island probe (MovementAction's ProbePlaceUnstickIsolated) needs the core's own walkable-poly pick (T
// `PathInfo::FindWalkPoly`), which F's PathGenerator has no call for; the probe's query and poly pick are NOT proper. Failure values:
// nullptr (the probe then answers "not isolated": no unstick teleport) and the invalid poly ref 0.
inline dtNavMeshQuery const* NavMeshProbeQueryCompat(uint32 /*mapId*/) { tbcompat::Unported(tbcompat::Feature::NavmeshProbe); return nullptr; }
template <class... Args>
inline dtPolyRef NavMeshFindWalkPolyCompat(Args&&...) { tbcompat::Unported(tbcompat::Feature::NavmeshProbe); return 0; }


// ===============================================================================================================
// PLAYERBOTAI.CPP (cinematics, loot window, facing, instance bind, mounts, camp, speed cheat, logout, movement stop, addon chat,
// portals, taxi follow, text emotes, spawn lookups, guild rank, camera)
// ===============================================================================================================
// Cinematic: T ends a cinematic the bot is watching (a headless bot cannot finish it). The engine asks this on every tick (PlayerbotAI.cpp:410),
// so counting every call buried the report (102k calls in 4 minutes, R6B). F answers it for real: its CinematicMgr says whether the player is on a
// cinematic (CinematicMgr.h:38) and EndCinematic ends it; only a bot that IS watching one reaches the feature counter (the camera body it
// would otherwise look through is not ported).
inline void EndCinematicIfWatchingCompat(Player* bot)
{
    if (CinematicMgr* cinematic = bot->GetCinematicMgr(); cinematic && cinematic->IsOnCinematic())
    {
        tbcompat::Unported(tbcompat::Feature::CinematicCamera);
        cinematic->EndCinematic();
    }
}

// The loot window of a player (T: `GetLootGuid()` and the loot manager). F keeps it in the player data (Player.h:2349 GetLootGUID, the
// target of the open loot window), so the question is answered for real: empty for a bot with no loot window, which is every bot while
// no loot is ported. It is asked on every moving tick (PlayerbotAI.cpp:485), so counting the question buried the report (204k calls in
// 4 minutes, R6B); only ReleaseLootIfFarCompat, reached with a non-empty guid, counts.
inline ObjectGuid LootGuidCompat(Player* bot) { return bot->GetLootGUID(); }
template <class GuidPos>
inline void ReleaseLootIfFarCompat(Player* /*bot*/, GuidPos& lootObject, bool /*shouldRelease*/)
{
    // The caller builds `lootObject` from the bot's own position, so it reads as set even when the guid is empty (GuidPosition::operator bool);
    // only a real loot window (a non-empty guid) reaches the feature, which is why this was 250k calls in 4 minutes (R8).
    if (!static_cast<ObjectGuid const&>(lootObject).IsEmpty())
        tbcompat::Unported(tbcompat::Feature::LootObjects);
}

// PROPER: the clients are told about a new facing by F's SetFacingTo itself (a facing spline), so the extra facing broadcast of T
// (MSG_MOVE_SET_FACING from the mover) has nothing left to do.
inline void BroadcastFacingCompat(Player* /*bot*/) {}

// The debug aura command: T builds a SpellAuraHolder with one Aura per aura effect and stores it on the unit; F's
// Unit::AddAura(SpellInfo const*, effMask, target) creates the same auras from the core's spell record (Unit.cpp:13078-13109),
// with the unit as caster and target as on T. The mask covers the aura effects of the first three slots, the slots the mirror
// spell entry keeps (T's MAX_EFFECT_INDEX loop).
inline bool ApplyDebugAuraCompat(Unit* unit, SpellEntry const* spellInfo)
{
    SpellInfo const* core = tbcompat::SpellCore(spellInfo);
    if (!unit || !core)
        return false;

    uint32 effMask = 0;
    for (SpellEffectInfo const& effect : core->GetEffects())
    {
        if (effect.EffectIndex >= MAX_EFFECT_INDEX)
            continue;
        if (effect.IsAreaAuraEffect() || effect.Effect == SPELL_EFFECT_APPLY_AURA || effect.Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA)
            effMask |= 1u << effect.EffectIndex;
    }

    return unit->AddAura(core, effMask, unit) != nullptr;
}

// Whether a bot may enter an area trigger's map: the instance is full, an encounter is in progress, or the bot is locked to a
// different instance. T asks its dungeon persistent state and player bind; F asks the instance locks, in the same order its own
// InstanceMap::CannotEnter applies (Map.cpp:3145-3175), so a bot and a player disagree on nothing.
// Only random bots ask (PlayerbotAI::CanEnterArea), and a host bot is never one.
template <class Area>
inline bool CanEnterAreaCompat(Player* bot, Area const* area)
{
    if (!bot || !area)
        return false;

    uint32 const mapId = area->GetMapId();
    MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
    if (!mapEntry)
        return false;

    // The instance the bot would enter: its own lock, its group's, or the group's recent one (the core's teleport path uses the same).
    Map* map = sMapMgr->FindMap(mapId, sMapMgr->FindInstanceIdForPlayer(mapId, bot));
    if (!map || !map->IsDungeon())
        return true;

    // must not check when teleporting around the same map
    if (bot->GetMapId() == mapId)
        return true;

    InstanceMap* instance = map->ToInstanceMap();
    if (instance->GetPlayersCountExceptGMs() >= instance->GetMaxPlayers())
        return false;

    // An encounter in progress closes the instance. T checks it for every dungeon; the core's own admission only does so for raids
    // (Map.cpp:3161), and the difference stays because this is the bot's area-trigger rule, not the core's teleport rule.
    if (InstanceScript* script = instance->GetInstanceScript(); script && script->IsEncounterInProgress())
        return false;

    // A player locked to another instance of the same map may not join this one.
    if (InstanceLock const* lock = instance->GetInstanceLock())
        if (sInstanceLockMgr.CanJoinInstanceLock(bot->GetGUID(), MapDb2Entries(mapEntry, map->GetMapDifficulty()), lock) != TRANSFER_ABORT_NONE)
            return false;

    return true;
}

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
// The movespeed cheat: T `UpdateSpeed(type, true, 10)` forces a 10x rate for walk, run and swim; F sets that rate directly
// (Unit::SetSpeedRate is where the forced update writes it, Unit.cpp:9480), and the core recomputes its aura part on top.
inline void SpeedCheatCompat(Player* bot)
{
    bot->SetSpeedRate(MOVE_WALK, 10.0f);
    bot->SetSpeedRate(MOVE_RUN, 10.0f);
    bot->SetSpeedRate(MOVE_SWIM, 10.0f);
}

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
// R9: "blocked" (true) when the flag cannot be read: the original answers true for an object the bot may not touch, so an unknown object is not touched.
inline bool GameObjectNoInteractCompat(GameObject* /*go*/) { tbcompat::Unported(tbcompat::Feature::GameObjectUse); return true; }
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


// ===============================================================================================================
// WORLDPOSITION.CPP, GUIDPOSITION.CPP
// ===============================================================================================================
// T's guid type of a game object based transport. F has no such tag: the label can never match (HighGuid::Count is no real guid
// type). The same switch lists HIGHGUID_MO_TRANSPORT, which is F's transport tag.
#define HIGHGUID_TRANSPORT HighGuid::Count

// The spawn record behind a guid counter. Failure value: nullptr ("no spawn record"); WorldPosition(guid) then answers the empty
// position, as it does for a guid it does not know.
inline GameObjectDataPair const* GameObjectDataPairOfCounterCompat(uint64 /*counter*/) { tbcompat::Unported(tbcompat::Feature::SpawnIdOfGuid); return nullptr; }
inline CreatureDataPair const* CreatureDataPairOfCounterCompat(uint64 /*counter*/) { tbcompat::Unported(tbcompat::Feature::SpawnIdOfGuid); return nullptr; }

// The highest level of a creature template (T `level_max`). Failure value: 0 ("level unknown").
inline uint32 CreatureMaxLevelCompat(CreatureInfo const* /*info*/) { tbcompat::Unported(tbcompat::Feature::CreatureLevels); return 0; }

// The steep-slope exclusion of a pathfinder (T `PathFinder::ExcludeSteepSlopes()`). Failure value: nothing excluded ("the path may use
// steep ground").
template <class PathFinderType>
inline void ExcludeSteepSlopesCompat(PathFinderType& /*pathfinder*/) { tbcompat::Unported(tbcompat::Feature::NavmeshProbe); }

// The checks that the legacy policy constants equal the core's path type and navmesh flag bits. T: compared. F: the bits mean other
// things (R4M decision 8, UNPORTED.md "navmesh and path internals"), no equality is claimed.
constexpr bool PathTypeBitsMatchCoreCompat(uint32, uint32, uint32, uint32, uint32, uint32, uint32, uint32) { return true; }
constexpr bool NavFlagsMatchCoreCompat(uint32, uint32, uint32, uint32, uint32) { return true; }

// PROPER: the navmesh flags a walkable-ground query excludes: magma, slime, water and steep slopes (F merges magma and slime).
inline uint16 NavExcludeFlagsCompat() { return NAV_MAGMA_SLIME | NAV_WATER | NAV_GROUND_STEEP; }


// Pool and game event of a spawn found by its guid counter. Failure values: 0 ("not part of a pool", "no event").
inline uint16 PoolOfSpawnCompat(bool /*creature*/, uint64 /*counter*/) { tbcompat::Unported(tbcompat::Feature::PoolsAndEvents); return 0; }
inline int16 GameEventOfSpawnCompat(uint16 /*poolId*/, bool /*creature*/, bool /*gameObject*/, uint64 /*counter*/) { tbcompat::Unported(tbcompat::Feature::PoolsAndEvents); return 0; }
// PROPER: whether a game event is running (F's manager is a pointer-style singleton).
inline bool GameEventActiveCompat(int16 eventId) { return sGameEventMgr->IsActiveEvent(eventId); }


// ===============================================================================================================
// CHAT (PlayerbotAIChat.cpp)
// ===============================================================================================================
// Say something in the bot's guild (T: the speak right of its rank, then Guild::BroadcastToGuild). Failure value: false (not said).
inline bool GuildSayCompat(Player* /*bot*/, Guild* /*guild*/, std::string const& /*msg*/) { tbcompat::Unported(tbcompat::Feature::GuildRanks); return false; }
// Join a named channel of the bot's team and say something in it (T: ChannelMgr by team, GetOrCreateChannel, Join, AsyncSay). Failure
// value: false (not said).
inline bool SayToChannelCompat(Player* /*bot*/, std::string const& /*name*/, std::string const& /*msg*/) { tbcompat::Unported(tbcompat::Feature::AddonChat); return false; }
// A chat packet from the bot to a player (T BuildChatPacket with CHAT_TAG_NONE). Failure value: not sent.
template <class Facade>
inline void SendChatPacketCompat(Facade& /*facade*/, Player* /*from*/, Player* /*to*/, uint32 /*chatType*/, std::string const& /*text*/, uint32 /*language*/) { tbcompat::Unported(tbcompat::Feature::AddonChat); }


// ===============================================================================================================
// SPELLS (PlayerbotAISpells.cpp)
// ===============================================================================================================
// Hand an object the bot summoned on another map back to its owner (T: the four summon object slots of the player). Failure value:
// nothing is handed back.
inline void ReleaseForeignSummonObjectsCompat(Player* /*bot*/, SpellEntry const* /*spellInfo*/) { tbcompat::Unported(tbcompat::Feature::SummonSlots); }
// Whether the spell's icon id is this one (T `SpellIconID == icon`). Failure value: false.
inline bool SpellIconIdIsCompat(SpellEntry const* /*spell*/, uint32 /*icon*/) { tbcompat::Unported(tbcompat::Feature::SpellData); return false; }
// The charges of an item's spell by index (T `Item::GetSpellCharges(index)`). Failure value: 0 charges.
inline int32 ItemSpellChargesCompat(Item const* /*item*/, uint8 /*index*/) { tbcompat::Unported(tbcompat::Feature::SpellData); return 0; }

// @@END@@
#endif // TB_UNPORTED_TABLE_ONLY

