// ForeverCore side of the both-core helpers of the player-command chat words, round R5 (PLAYER-COMMANDS-SPEC): who, items, unequip,
// quests, leave, reputation. Turtle side: ai/cmangos-compat-pc5.h. Reached through ai/playerbot/TbCompatPC5.h.
// T = the Turtle core, F = the pinned ForeverCore (ForeverCore 0.1.39, the TrinityCore-based core).
#pragma once

#include "ReputationMgr.h"
#include "Server/Packets/ItemPackets.h"
#include "Server/Packets/PartyPackets.h"

// The bot takes an item out of a slot into its bags (unequip). T the packet CMSG_AUTOSTORE_BAG_ITEM (src bag, src slot, dst bag) through
// HandleAutoStoreBagItemOpcode(WorldPacket&). F the typed packet of the same three numbers (ItemHandler.cpp:780: it checks the
// unequip rules and the bag space itself and answers with the equip error to the bot's session, which nobody reads).
inline void AutoStoreItemCompat(Player* bot, uint8 srcBag, uint8 srcSlot, uint8 dstBag)
{
    WorldPacket raw(CMSG_AUTO_STORE_BAG_ITEM);
    WorldPackets::Item::AutoStoreBagItem packet(std::move(raw));
    packet.ContainerSlotA = srcBag;
    packet.SlotA = srcSlot;
    packet.ContainerSlotB = dstBag;
    bot->GetSession()->HandleAutoStoreBagItemOpcode(packet);
}

// The bot leaves its own group, as if it had pressed "leave party". T CMSG_GROUP_DISBAND carrying PARTY_OP_LEAVE through
// HandleGroupDisbandOpcode(WorldPacket&). F the typed CMSG_LEAVE_GROUP (GroupHandler.cpp:369: no group and no invite = nothing; a
// battleground = nothing; else RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE)), no party index = the bot's first group.
inline void BotLeaveGroupCompat(Player* bot)
{
    WorldPacket raw(CMSG_LEAVE_GROUP);
    WorldPackets::Party::LeaveGroup packet(std::move(raw));
    bot->GetSession()->HandleLeaveGroupOpcode(packet);
}

// The skill cap of one skill. T `GetSkillMax(skill)`, F `GetPureMaxSkillValue(skill)` (the same number: the cap the trainer set).
inline uint16 SkillMaxOfCompat(Player* bot, uint32 skill) { return bot->GetPureMaxSkillValue(skill); }

// The number a trade-skill chat link carries after the skill ("|Htrade:spell:value:max:GUID:bitmap"). T the guid as a 64-bit number, F
// has no such number (the guid is 128 bits, ObjectGuid.h:325): the character's database counter, the low guid the old links carried.
inline uint64 GuidLinkNumberCompat(ObjectGuid const& guid) { return guid.GetCounter(); }

// One rank of the reputation ladder: the reputation value where the rank starts and how many points the rank spans. T the legacy
// ReputationMgr::PointsInRank table and Reputation_Cap (42999: Exalted spans the last 1000 points, so the ladder starts at -42000).
// F keeps the table private to ReputationMgr.cpp; the ranks are the same eight (Hated 36000, Hostile 3000, Unfriendly 3000, Neutral 3000,
// Friendly 6000, Honored 12000, Revered 21000, Exalted 1000), the ladder starts at Reputation_Bottom (-42000).
inline void ReputationRankSpanCompat(ReputationRank rank, int32& base, int32& points)
{
    static int32 const kPointsInRank[MAX_REPUTATION_RANK] = { 36000, 3000, 3000, 3000, 6000, 12000, 21000, 1000 };
    int const r = rank < 0 ? 0 : (rank >= MAX_REPUTATION_RANK ? MAX_REPUTATION_RANK - 1 : int(rank));
    base = ReputationMgr::Reputation_Bottom;
    for (int i = 0; i < r; ++i)
        base += kPointsInRank[i];
    points = kPointsInRank[r];
}
