// Turtle side of the both-core quest helpers (W3: quests).
// ForeverCore side: forever/src/compat/TbCompat_Quest.h (same names and signatures).
#pragma once

#include <utility>
#include <vector>

struct QuestTriageViewCompat
{
    struct Item { uint32 id = 0; uint32 count = 0; };
    bool inactive = false;
    bool hasItemObjective = false;
    bool deliver = false;
    uint32 level = 0;
    uint32 levelForPlayer = 0;
    uint32 type = 0;
    uint32 suggestedPlayers = 0;
    int32 rewardOrRequiredMoney = 0;
    std::vector<Item> items;
};

inline std::string QuestTitleCompat(Quest const* quest) { return quest->GetTitle(); }
inline void RemoveQuestFromLogCompat(Player* bot, uint32 questId) { bot->RemoveQuest(questId); }
inline std::vector<std::pair<uint32, uint32>> QuestRequiredItemsCompat(Quest const* quest)
{
    std::vector<std::pair<uint32, uint32>> items;
    for (uint8 i = 0; i < std::size(quest->ReqItemId); ++i)
        items.push_back({ quest->ReqItemId[i], quest->ReqItemCount[i] });
    return items;
}

inline bool QuestHasItemObjectiveCompat(Quest const* quest)
{
    for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemId[i] && quest->ReqItemCount[i])
            return true;
    for (int i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        if (quest->ReqSourceId[i] && quest->ReqSourceCount[i])
            return true;
    return false;
}

inline bool QuestIsDeliverCompat(Quest const* quest)
{
    return quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER);
}

inline bool QuestHasRequiredDeliverItemsCompat(Player* bot, Quest const* quest)
{
    if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER))
        return true;
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemCount[i] && bot->GetItemCount(quest->ReqItemId[i]) < quest->ReqItemCount[i])
            return false;
    return true;
}

inline bool QuestHasObjectiveProgressCompat(Player* bot, Quest const* quest)
{
    auto const& statusMap = bot->getQuestStatusMap();
    auto itr = statusMap.find(quest->GetQuestId());
    if (itr == statusMap.end())
        return false;
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (itr->second.m_creatureOrGOcount[i] > 0)
            return true;
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (itr->second.m_itemcount[i] > 0)
            return true;
    return false;
}

inline int32 QuestRewOrReqMoneyCompat(Quest const* quest) { return quest->GetRewOrReqMoney(); }
inline bool QuestIsActiveCompat(Quest const* quest) { return quest->IsActive(); }
inline uint32 QuestTypeCompat(Quest const* quest) { return quest->GetType(); }
inline bool QuestIsAutoCompleteCompat(Quest const* quest) { return quest->IsAutoComplete(); }
inline uint32 QuestAllowableClassesCompat(Quest const* quest) { return quest->GetRequiredClasses(); }

inline uint32 QuestLevelForPlayerCompat(Player const* bot, Quest const* quest)
{
    return bot && quest ? bot->GetQuestLevelForPlayer(quest) : 0u;
}

inline QuestTriageViewCompat QuestTriageViewOfCompat(Player* bot, Quest const* quest)
{
    QuestTriageViewCompat view;
    view.inactive = !quest->IsActive();
    view.hasItemObjective = QuestHasItemObjectiveCompat(quest);
    view.deliver = quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER);
    view.level = quest->GetQuestLevel();
    view.levelForPlayer = bot->GetQuestLevelForPlayer(quest);
    view.type = quest->GetType();
    view.suggestedPlayers = quest->GetSuggestedPlayers();
    view.rewardOrRequiredMoney = quest->GetRewOrReqMoney();
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemCount[i])
            view.items.push_back({ quest->ReqItemId[i], quest->ReqItemCount[i] });
    return view;
}

inline uint32 QuestMenuCountCompat(QuestMenu const& menu) { return menu.MenuItemCount(); }
inline uint32 QuestMenuItemIdCompat(QuestMenuItem const& item) { return item.m_qId; }
inline uint8 QuestMenuItemIconCompat(QuestMenuItem const& item) { return item.m_qIcon; }
inline bool QuestMenuItemIsAvailableCompat(QuestMenuItem const& item) { return item.m_qIcon == DIALOG_STATUS_AVAILABLE; }

inline bool ObjectHasQuestCompat(WorldObject const* obj, uint32 questId)
{
    return obj && obj->HasQuest(questId);
}

inline bool QuestIsAcceptedStatusCompat(QuestStatus status)
{
    return status != QUEST_STATUS_NONE && status != QUEST_STATUS_AVAILABLE;
}

inline bool QuestIsAvailableStatusCompat(QuestStatus status)
{
    return status == QUEST_STATUS_AVAILABLE || status == QUEST_STATUS_NONE;
}

inline bool AcceptQuestDirectCompat(Player* bot, Quest const* quest, WorldObject* giver)
{
    if (!bot || !quest || !giver)
        return false;
    WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
    uint32 unk1 = 0;
    p << giver->getObjectGuid().GetRawValue() << quest->GetQuestId() << unk1;
    p.rpos(0);
    bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);
    return QuestIsAcceptedStatusCompat(bot->GetQuestStatus(quest->GetQuestId()));
}

// The giver of a quest: an NPC, an object, a player sharing it or an item starting it.
inline Object* QuestGiverObjectCompat(Player* bot, ObjectGuid giverGuid)
{
    return bot ? bot->GetObjectByTypeMask(giverGuid, TYPEMASK_CREATURE_GAMEOBJECT_PLAYER_OR_ITEM) : nullptr;
}

inline bool AcceptQuestByGiverCompat(Player* bot, Quest const* quest, ObjectGuid giverGuid)
{
    if (!bot || !quest)
        return false;
    WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
    uint32 unk1 = 0;
    p << giverGuid.GetRawValue() << quest->GetQuestId() << unk1;
    p.rpos(0);
    bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);
    return QuestIsAcceptedStatusCompat(bot->GetQuestStatus(quest->GetQuestId()));
}

inline bool CanRewardQuestCompat(Player* bot, Quest const* quest, bool msg)
{
    return bot->CanRewardQuest(quest, msg);
}

inline bool CanRewardQuestChoiceCompat(Player* bot, Quest const* quest, uint32 rewardIndex, bool msg)
{
    return bot->CanRewardQuest(quest, rewardIndex, msg);
}

inline void RewardQuestDirectCompat(Player* bot, Quest const* quest, uint32 rewardIndex, WorldObject* questGiver, bool announce)
{
    bot->RewardQuest(quest, rewardIndex, questGiver, announce);
}

inline void PushQuestToPartyCompat(Player* bot, uint32 questId)
{
    WorldPacket p;
    p << questId;
    bot->GetSession()->HandlePushQuestToParty(p);
}

inline bool QuestIsVanillaEligibleCompat(Quest const* /*quest*/) { return true; }

inline std::string GameObjectNameCompat(GameObjectInfo const* info) { return info ? info->name : std::string(); }

inline bool PlayerSharingQuestCompat(Player* player)
{
    return player && !player->GetDividerGuid().IsEmpty();
}

inline bool SyncQuestWithPlayerAllowedCompat() { return true; }

template <class TriggerType>
inline void PollQuestShareCompat(Player* /*bot*/, TriggerType& /*trigger*/) {}

inline bool AcceptQuestShareDirectCompat(Player* bot, Player* requester, uint32 questId, Quest const* qInfo)
{
    if (!bot || !qInfo || !bot->GetDividerGuid())
        return false;
    if (!bot->CanTakeQuest(qInfo, false))
    {
        bot->SetDividerGuid(ObjectGuid());
        return false;
    }
    if (!bot->GetDividerGuid().IsEmpty() && requester)
    {
        requester->SendPushToPartyResponse(bot, QUEST_PARTY_MSG_ACCEPT_QUEST);
        bot->SetDividerGuid(ObjectGuid());
    }
    if (bot->CanAddQuest(qInfo, false))
    {
        bot->AddQuest(qInfo, requester);
        if (bot->CanCompleteQuest(questId))
            bot->CompleteQuest(questId);
        if (qInfo->GetSrcSpell() > 0)
            bot->CastSpell(bot, qInfo->GetSrcSpell(), true);
        return true;
    }
    return false;
}
