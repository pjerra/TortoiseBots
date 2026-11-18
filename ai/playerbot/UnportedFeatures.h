// The closed list of "not ported yet" features of slice 1 (package R6A). Core-free: ForeverCore counts the calls of the helpers
// that answer for one of these (forever/src/compat/TbCompat_Unported.h); the Turtle side (ai/cmangos-compat-unported.h) names the
// same features so a call site reads the same on both cores, and there the helpers are the original calls. One row per feature:
// id (the enumerator), name (the text of the log line, the boot line and forever/docs/UNPORTED.md), what it is.
// Add a row here, never a string at a call site.
#pragma once

#include <cstdint>

// name (the text of the log line and of the summary), what it is. Keep the order: it is the order of the summary.
#define TB_UNPORTED_FEATURES(X) \
    X(PacketHandlerTables, "packet handler tables", "the legacy opcode-to-handler tables (1.12 opcode names); ForeverCore's bot session neither sends nor receives these packets, so nothing is registered") \
    X(ItemRandomProperties, "item random properties", "the random property / suffix of an item (1.12 ItemRandomProperties.dbc); ForeverCore uses item bonus lists") \
    X(KeyringAndBankSlots, "bank and keyring slots", "the 1.12 keyring slots and bank item slots; ForeverCore has neither (keys live in bags, the bank is bank bags)") \
    X(LootObjects, "loot objects", "the loot of a creature or game object as a public member (1.12 Creature::loot); ForeverCore keeps loot per looting player") \
    X(ItemDisplayId, "item display id", "ItemPrototype::DisplayInfoID, used to find consumables that look the same; the mirror prototype has no display id") \
    X(ItemUsePacket, "item use packet", "imbue / use an item by queueing a CMSG_USE_ITEM packet with a packed guid (1.12 layout)") \
    X(ItemSlotOfPrototype, "equip slot of an item prototype", "Player::FindEquipSlot on a prototype; ForeverCore asks about an Item") \
    X(Companions, "companions", "spells with the summon critter effect (1.12 SPELL_EFFECT_SUMMON_CRITTER); ForeverCore has no such effect") \
    X(Auction, "auction (type only)", "AuctionEntry named in the declarations of the auction code (RandomBotFacade.h, ItemUsageValue.h); the slice never reaches that code and ForeverCore has AuctionPosting instead") \
    X(IncomingLoot, "incoming: loot", "loot roll, loot start, loot response packets (1.12 layouts)") \
    X(IncomingQuest, "incoming: quest", "quest accept, complete, confirm, share, details and update packets (1.12 layouts)") \
    X(IncomingTaxi, "incoming: taxi", "activate taxi and taxi node packets") \
    X(IncomingGroup, "incoming: group and party", "group invite, uninvite, set leader, party command, ready check, summon request and duel request packets") \
    X(IncomingTradeAndItems, "incoming: trade and item errors", "trade status, buy errors, item push, inventory change failure, master use item packets") \
    X(IncomingBattleground, "incoming: battleground status", "the battleground status packet") \
    X(IncomingGuildPetition, "incoming: guild and petition", "guild invite and petition signature packets") \
    X(IncomingExperience, "incoming: experience and honor", "level up, xp gain and honor gain packets") \
    X(IncomingEmotes, "incoming: emotes", "text emote and emote packets") \
    X(IncomingObjectUse, "incoming: gossip, game object and area trigger", "gossip hello, game object use and area trigger packets") \
    X(IncomingDeath, "incoming: release spirit and resurrect", "repop, reclaim corpse and resurrect request packets") \
    X(IncomingCastFailed, "incoming: cast failed", "SMSG_CAST_RESULT, the asynchronous failure of a cast (findings item 9); the bot never hears that a cast failed after it started") \
    X(IncomingSpeedChange, "incoming: forced speed change", "the six forced-speed packets and the client acks the bot answers them with (findings item 9)") \
    X(IncomingKnockBack, "incoming: knock back", "the knock-back packet and the jump the bot answers it with") \
    X(IncomingChat, "incoming: chat message", "chat messages (1.12 SMSG_MESSAGECHAT layout): party and whisper commands typed to a bot") \
    X(IncomingSpellState, "incoming: spell failure and delay", "spell failure and spell delayed packets (1.12 layouts)") \
    X(JumpFall, "jump, fall and knockback", "jumping, fall damage bookkeeping and knock-back handling by 1.12 movement packets (MSG_MOVE_JUMP, SetFallInformation, forced movement acks); ForeverCore moves by spline and movement generators") \
    X(WorldAverageDiff, "world update time average", "sWorld.GetAverageDiff(): the average world update time, a host value (R4M request 5); ForeverCore keeps no such average") \
    X(Talents, "talents", "talent points and trees (1.12 TalentTab.dbc, Player::GetFreeTalentPoints, GetBonusTalentCount); ForeverCore has no equivalent accessors in the pre-included set") \
    X(SpawnGuid, "guid from spawn data", "an object guid built from a spawn record (creature data id + spawn id, 1.12 guid layout); ForeverCore keys spawns by spawn id and map, not by guid counter") \
    X(GuidPositionWire, "wire form of a GuidPosition", "writing a GuidPosition into a packet buffer (1.12 layout)") \
    X(BattlegroundQueue, "battleground queue", "battleground template id from a queue type id (BattleGroundMgr::BGTemplateId); ForeverCore's queue type id is a struct and the mapping is not ported") \
    X(ChatLinks, "chat link parsing", "extracting quest and spell ids from chat links (1.12 ChatHandler helpers ExtractKeyFromLink, ExtractSpellIdFromLink)") \
    X(Pets, "pets and guardians", "pet spell learning and autocast, pet command packets, the controlled-guardian walk (1.12 Pet, CallForAllControlledUnits)") \
    X(GameObjectUse, "game object use", "using a game object by the use packet (lightwell, party portal, ritual objects); ForeverCore uses typed packets and a different interact check") \
    X(ItemCasts, "item and totem casts", "casting an item spell with a hand-built Spell (SpellCastTargets setters, triggered flags, SetCastItem, item cooldowns); ForeverCore constructs a Spell from SpellInfo") \
    X(CreatureImmunity, "creature immunity data", "CreatureInfo school and mechanic immune masks; ForeverCore keeps immunities in CreatureImmunities rows") \
    X(GmVisibility, "visibility (GM)", "Player::GetVisibility() == VISIBILITY_OFF (a hidden GM); ForeverCore has no such visibility value") \
    X(HealEstimate, "heal estimate (spell damage bonus)", "Player::CalculateSpellDamage and SpellHealingBonusDone on a SpellEntry; the mirror entry is not a SpellInfo") \
    X(CustomStrategyStore, "custom strategy store", "reading custom strategy lines from ai_playerbot_custom_strategy; that table does not exist in ForeverCore characters schema") \
    X(StrategyStoreTransactions, "strategy store transactions", "BeginTransaction(serial) / CommitTransaction() around saving strategies; ForeverCore transactions are objects and the ai_playerbot tables do not exist") \
    X(Skills, "skills", "skill lines ForeverCore does not name (thrown) and skill writes (SetSkill with the 1.12 argument list)")

namespace tbcompat
{
enum class Feature : uint16_t
{
#define TB_UNPORTED_ENUM(id, name, what) id,
    TB_UNPORTED_FEATURES(TB_UNPORTED_ENUM)
#undef TB_UNPORTED_ENUM
    Count
};

}
