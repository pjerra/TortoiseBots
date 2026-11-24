// ForeverCore side of the both-core quest helpers (W3: quests).
// Turtle side: ai/cmangos-compat-quest.h (same names and signatures).
#pragma once

#include "QuestPackets.h"
#include "playerbot/policy/quest/QuestFilterPolicy.h"

#include <algorithm>
#include <unordered_set>
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

inline std::string QuestTitleCompat(Quest const* quest)
{
    return quest ? quest->GetLogTitle() : std::string();
}

inline void RemoveQuestFromLogCompat(Player* bot, uint32 questId)
{
    if (!bot || !questId)
        return;
    if (bot->GetSession())
    {
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            if (bot->GetQuestSlotQuestId(slot) == questId)
            {
                WorldPackets::Quest::QuestLogRemoveQuest packet{ WorldPacket(CMSG_QUEST_LOG_REMOVE_QUEST) };
                packet.Entry = static_cast<uint8>(slot);
                bot->GetSession()->HandleQuestLogRemoveQuest(packet);
                return;
            }
        }
    }
    bot->RemoveActiveQuest(questId);
    bot->AbandonQuest(questId);
}

inline std::vector<std::pair<uint32, uint32>> QuestRequiredItemsCompat(Quest const* quest)
{
    std::vector<std::pair<uint32, uint32>> items;
    if (!quest)
        return items;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (obj.Type == QUEST_OBJECTIVE_ITEM && obj.ObjectID > 0 && obj.Amount > 0)
            items.emplace_back(uint32(obj.ObjectID), uint32(obj.Amount));
    }
    return items;
}

inline bool QuestHasItemObjectiveCompat(Quest const* quest)
{
    if (!quest)
        return false;
    if (quest->GetSrcItemId() && quest->GetSrcItemCount())
        return true;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (obj.Type == QUEST_OBJECTIVE_ITEM && obj.ObjectID > 0 && obj.Amount > 0)
            return true;
    }
    return false;
}

inline bool QuestIsDeliverCompat(Quest const* quest)
{
    if (!quest)
        return false;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (obj.Type == QUEST_OBJECTIVE_ITEM && obj.ObjectID > 0 && obj.Amount > 0)
            return true;
    }
    return false;
}

inline bool QuestHasRequiredDeliverItemsCompat(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return true;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (obj.Type == QUEST_OBJECTIVE_ITEM && obj.ObjectID > 0 && obj.Amount > 0)
        {
            if (bot->GetItemCount(uint32(obj.ObjectID)) < uint32(obj.Amount))
                return false;
        }
    }
    return true;
}

inline bool QuestHasObjectiveProgressCompat(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return false;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (bot->GetQuestObjectiveData(obj) > 0)
            return true;
    }
    return false;
}

// The entries a quest's source item must be used on (T ObjectMgr::GetItemRequiredTargetMapBounds). ForeverCore's classic
// quests carry no required-target table, so no entry is added (the source-item step finds no destination - LESS).
inline std::vector<int32> QuestRequiredTargetEntriesCompat(uint32 /*itemId*/)
{
    return {};
}

inline int32 QuestRewOrReqMoneyCompat(Quest const* quest)
{
    if (!quest)
        return 0;
    uint32 const req = quest->GetClassicRequiredMoney();
    return req > 0 ? -static_cast<int32>(req) : 0;
}

inline bool QuestIsActiveCompat(Quest const* quest)
{
    return quest != nullptr;
}

inline uint32 QuestTypeCompat(Quest const* quest)
{
    return quest ? quest->GetQuestInfoID() : 0u;
}

inline bool QuestIsAutoCompleteCompat(Quest const* quest)
{
    return quest && quest->IsTurnIn();
}

inline uint32 QuestAllowableClassesCompat(Quest const* quest)
{
    return quest ? quest->GetAllowableClasses() : 0u;
}

inline uint32 QuestLevelForPlayerCompat(Player const* bot, Quest const* quest)
{
    int32 const ql = bot && quest ? bot->GetQuestLevel(quest) : 0;
    return ql > 0 ? uint32(ql) : 0u;
}

inline QuestTriageViewCompat QuestTriageViewOfCompat(Player* bot, Quest const* quest)
{
    QuestTriageViewCompat view;
    if (!quest)
        return view;
    view.inactive = false;
    view.hasItemObjective = QuestHasItemObjectiveCompat(quest);
    view.deliver = QuestIsDeliverCompat(quest);
    int32 const ql = quest->GetClassicQuestLevel();
    view.level = ql > 0 ? uint32(ql) : 0u;
    view.levelForPlayer = bot ? QuestLevelForPlayerCompat(bot, quest) : view.level;
    view.type = quest->GetQuestInfoID();
    view.suggestedPlayers = quest->GetSuggestedPlayers();
    view.rewardOrRequiredMoney = QuestRewOrReqMoneyCompat(quest);
    for (auto const& item : QuestRequiredItemsCompat(quest))
        view.items.push_back({ item.first, item.second });
    return view;
}

inline uint32 QuestMenuCountCompat(QuestMenu const& menu) { return menu.GetMenuItemCount(); }
inline uint32 QuestMenuItemIdCompat(QuestMenuItem const& item) { return item.QuestId; }
inline uint8 QuestMenuItemIconCompat(QuestMenuItem const& item) { return item.QuestIcon; }
inline bool QuestMenuItemIsAvailableCompat(QuestMenuItem const& item) { return item.QuestIcon == 2; }

inline bool ObjectHasQuestCompat(WorldObject const* obj, uint32 questId)
{
    return obj && obj->hasQuest(questId);
}

inline bool QuestIsAcceptedStatusCompat(QuestStatus status)
{
    return status != QUEST_STATUS_NONE;
}

inline bool QuestIsAvailableStatusCompat(QuestStatus status)
{
    return status == QUEST_STATUS_NONE;
}

// Was the quest ever handed in: Turtle sets the log row's `m_rewarded`, ForeverCore writes the rewarded list and drops the
// row from the log (Player::GetQuestRewardStatus, Player.cpp:17321).
inline bool QuestEverRewardedCompat(Player* bot, uint32 questId)
{
    return bot && bot->IsQuestRewarded(questId);
}

// The reward of a quest ForeverCore still keeps in the log is always pending: the core drops the row when it is paid out.
inline bool QuestRewardPendingCompat(Player* bot, uint32 questId)
{
    return bot && bot->IsActiveQuest(questId);
}

// How many quests the log holds: ForeverCore keeps 40 update slots but a player may only fill CONFIG_QUEST_LOG_SIZE of
// them (Player::SatisfyQuestLog, Player.cpp:16858-16872), so the configured value is the capacity the bot can use.
inline uint8 QuestLogCapacityCompat()
{
    return uint8(std::min<uint32>(sWorld.getIntConfig(CONFIG_QUEST_LOG_SIZE), uint32(MAX_QUEST_LOG_SIZE)));
}

inline bool AcceptQuestDirectCompat(Player* bot, Quest const* quest, WorldObject* giver)
{
    if (!bot || !quest || !giver)
        return false;
    if (!bot->CanTakeQuest(quest, false) || !bot->SatisfyQuestLog(false) || !bot->CanAddQuest(quest, false))
        return false;
    bot->AddQuestAndCheckCompletion(quest, giver);
    if (quest->GetSrcSpell() > 0)
        bot->CastSpell(bot, quest->GetSrcSpell(), true);
    return QuestIsAcceptedStatusCompat(bot->GetQuestStatus(quest->GetQuestId()));
}

inline bool CanRewardQuestCompat(Player* bot, Quest const* quest, bool msg)
{
    return bot && quest && bot->CanRewardQuest(quest, msg);
}

inline bool CanRewardQuestChoiceCompat(Player* bot, Quest const* quest, uint32 rewardIndex, bool msg)
{
    if (!bot || !quest)
        return false;
    if (quest->GetRewChoiceItemsCount() == 0)
        return bot->CanRewardQuest(quest, msg);
    if (rewardIndex >= quest->GetRewChoiceItemsCount())
        return false;
    return bot->CanRewardQuest(quest, LootItemType::Item, quest->RewardChoiceItemId[rewardIndex], msg);
}

inline void RewardQuestDirectCompat(Player* bot, Quest const* quest, uint32 rewardIndex, WorldObject* questGiver, bool announce)
{
    if (!bot || !quest)
        return;
    uint32 rewardItemId = 0;
    if (quest->GetRewChoiceItemsCount() > 0 && rewardIndex < quest->GetRewChoiceItemsCount())
        rewardItemId = quest->RewardChoiceItemId[rewardIndex];
    bot->RewardQuest(quest, LootItemType::Item, rewardItemId, questGiver, announce);
}

inline void PushQuestToPartyCompat(Player* bot, uint32 questId)
{
    if (!bot || !bot->GetSession())
        return;
    WorldPackets::Quest::PushQuestToParty packet{ WorldPacket(CMSG_PUSH_QUEST_TO_PARTY) };
    packet.QuestID = questId;
    bot->GetSession()->HandlePushQuestToParty(packet);
}

// Visits the quest ids a creature (positive entry) or a game object (negative entry) gives (starter) or takes (ender).
// Turtle keeps four relation multimaps; ForeverCore keeps the same rows in QuestRelations behind
// ObjectMgr::GetCreatureQuestRelations / GetCreatureQuestInvolvedRelations / GetGOQuestRelations / GetGOQuestInvolvedRelations
// (ObjectMgr.h:1261-1266). Those results skip the quests the pool leaves out of rotation, which is the closest answer to
// Turtle's Quest::IsActive here (Quest::IsTakingQuestEnabled, QuestDef.cpp:466).
template <class Fn>
inline void ForEachQuestRelationCompat(int32 entry, bool starter, Fn const& fn)
{
    if (!entry)
        return;

    if (entry > 0)
    {
        QuestRelationResult const relations = starter
            ? sObjectMgr.GetCreatureQuestRelations(uint32(entry))
            : sObjectMgr.GetCreatureQuestInvolvedRelations(uint32(entry));
        for (QuestRelationResult::Iterator itr = relations.begin(); itr != relations.end(); ++itr)
            fn(*itr);
        return;
    }

    QuestRelationResult const relations = starter
        ? sObjectMgr.GetGOQuestRelations(uint32(-entry))
        : sObjectMgr.GetGOQuestInvolvedRelations(uint32(-entry));
    for (QuestRelationResult::Iterator itr = relations.begin(); itr != relations.end(); ++itr)
        fn(*itr);
}

// Spawned creature/GO entry sets (O(spawns) to build) so every quest filter check is O(1).
struct QuestEligibilitySets
{
    std::unordered_set<uint32> spawnedCreatures;
    std::unordered_set<uint32> spawnedGOs;
    std::unordered_set<uint32> questsWithGiver;
    bool built = false;
};

inline QuestEligibilitySets& QuestEligibilitySetsCompat()
{
    static QuestEligibilitySets sets;
    return sets;
}

// Called at world load and after quest reloads, so no bot tick pays for the build.
inline void RebuildQuestEligibilityCompat()
{
    QuestEligibilitySets& s = QuestEligibilitySetsCompat();
    s = QuestEligibilitySets();
    for (auto const& pair : ObjectMgr::instance()->GetAllCreatureData())
        s.spawnedCreatures.insert(pair.second.id);
    for (auto const& pair : ObjectMgr::instance()->GetAllGameObjectData())
        s.spawnedGOs.insert(pair.second.id);
    if (QuestRelations const* cRel = ObjectMgr::instance()->GetCreatureQuestRelationMapHACK())
    {
        for (auto const& kv : *cRel)
            if (s.spawnedCreatures.count(kv.first))
                s.questsWithGiver.insert(kv.second);
    }
    if (QuestRelations const* gRel = ObjectMgr::instance()->GetGOQuestRelationMapHACK())
    {
        for (auto const& kv : *gRel)
            if (s.spawnedGOs.count(kv.first))
                s.questsWithGiver.insert(kv.second);
    }
    s.built = true;
}

// Drops the sets without rebuilding: the next eligibility check rebuilds them. A quest-table reload while the module is
// off takes this path, so a re-enable cannot read pre-reload rows.
inline void ClearQuestEligibilityCompat()
{
    QuestEligibilitySetsCompat() = QuestEligibilitySets();
}

inline bool QuestIsVanillaEligibleCompat(Quest const* quest)
{
    if (!quest)
        return false;
    uint32 const questId = quest->GetQuestId();

    QuestEligibilitySets& sets = QuestEligibilitySetsCompat();
    if (!sets.built)
        RebuildQuestEligibilityCompat();
    std::unordered_set<uint32> const& spawnedCreatures = sets.spawnedCreatures;
    std::unordered_set<uint32> const& spawnedGOs = sets.spawnedGOs;
    std::unordered_set<uint32> const& questsWithGiver = sets.questsWithGiver;

    bool const hasGiver = questsWithGiver.count(questId) > 0;
    bool hasEnder = false;
    for (auto const& kv : ObjectMgr::instance()->GetCreatureQuestInvolvedRelationReverseBounds(questId))
    {
        if (spawnedCreatures.count(kv.second))
        {
            hasEnder = true;
            break;
        }
    }
    if (!hasEnder)
    {
        for (auto const& kv : ObjectMgr::instance()->GetGOQuestInvolvedRelationReverseBounds(questId))
        {
            if (spawnedGOs.count(kv.second))
            {
                hasEnder = true;
                break;
            }
        }
    }

    return ai::IsVanillaOrCustomQuest(questId, quest->GetExpansion(), hasGiver, hasEnder);
}

inline std::string GameObjectNameCompat(GameObjectInfo const* info) { return info ? info->name : std::string(); }

inline bool PlayerSharingQuestCompat(Player* player)
{
    return player && !player->GetPlayerSharingQuest().IsEmpty();
}

inline Object* QuestGiverObjectCompat(Player* bot, ObjectGuid giverGuid)
{
    return bot ? ObjectAccessor::GetWorldObject(*bot, giverGuid) : nullptr;
}

inline bool AcceptQuestByGiverCompat(Player* bot, Quest const* quest, ObjectGuid giverGuid)
{
    return bot && AcceptQuestDirectCompat(bot, quest, ObjectAccessor::GetWorldObject(*bot, giverGuid));
}

inline bool SyncQuestWithPlayerAllowedCompat() { return false; }

template <class TriggerType>
inline void PollQuestShareCompat(Player* bot, TriggerType& trigger)
{
    if (!bot || !bot->IsInWorld())
        return;
    if (bot->GetPlayerSharingQuest().IsEmpty() || bot->GetSharedQuestID() == 0)
        return;
    WorldPacket event;
    event << uint32(bot->GetSharedQuestID());
    trigger.ExternalEvent(event, nullptr);
}

inline bool AcceptQuestShareDirectCompat(Player* bot, Player* requester, uint32 questId, Quest const* qInfo)
{
    if (!bot || !qInfo)
        return false;
    ObjectGuid const sharerGuid = bot->GetPlayerSharingQuest();
    if (sharerGuid.IsEmpty())
        return false;
    Player* sharer = ObjectAccessor::FindPlayer(sharerGuid);
    if (!sharer)
        sharer = requester;
    if (!bot->CanTakeQuest(qInfo, false))
    {
        bot->ClearQuestSharingInfo();
        return false;
    }
    if (sharer)
        sharer->SendPushToPartyResponse(bot, QuestPushReason::Accepted, qInfo);
    bot->ClearQuestSharingInfo();
    if (bot->CanAddQuest(qInfo, false))
    {
        bot->AddQuestAndCheckCompletion(qInfo, sharer ? static_cast<Object*>(sharer) : static_cast<Object*>(bot));
        if (qInfo->GetSrcSpell() > 0)
            bot->CastSpell(bot, qInfo->GetSrcSpell(), true);
        return QuestIsAcceptedStatusCompat(bot->GetQuestStatus(questId));
    }
    return false;
}
