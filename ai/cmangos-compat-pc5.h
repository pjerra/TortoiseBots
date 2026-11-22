// Turtle side of the both-core helpers of the player-command chat words, round R5 (PLAYER-COMMANDS-SPEC): who, items, unequip,
// quests, leave, reputation. ForeverCore side: forever/src/compat/TbCompat_PC5.h. Reached through ai/playerbot/TbCompatPC5.h.
#pragma once

// The bot takes an item out of a slot into its bags (unequip): the packet the legacy code built.
inline void AutoStoreItemCompat(Player* bot, uint8 srcBag, uint8 srcSlot, uint8 dstBag)
{
    WorldPacket packet(CMSG_AUTOSTORE_BAG_ITEM, 3);
    packet << srcBag << srcSlot << dstBag;
    bot->GetSession()->HandleAutoStoreBagItemOpcode(packet);
}

// The bot leaves its own group: the disband packet the legacy code built.
inline void BotLeaveGroupCompat(Player* bot)
{
    WorldPacket p;
    std::string member = bot->GetName();
    p << uint32(PARTY_OP_LEAVE) << member << uint32(0);
    bot->GetSession()->HandleGroupDisbandOpcode(p);
}

// The skill cap of one skill.
inline uint16 SkillMaxOfCompat(Player* bot, uint32 skill)
{
#ifdef MANGOS
    return bot->GetMaxSkillValue(skill);
#endif
#ifdef CMANGOS
    return bot->GetSkillMax(skill);
#endif
}

// The number a trade-skill chat link carries after the skill: the guid as a 64-bit number.
inline uint64 GuidLinkNumberCompat(ObjectGuid const& guid) { return guid.GetRawValue(); }

// One rank of the reputation ladder: where it starts and how many points it spans (the legacy table).
inline void ReputationRankSpanCompat(ReputationRank rank, int32& base, int32& points)
{
    base = ReputationMgr::Reputation_Cap + 1;
    for (int i = MAX_REPUTATION_RANK - 1; i >= rank; --i)
        base -= ReputationMgr::PointsInRank[i];
    points = ReputationMgr::PointsInRank[rank];
}
