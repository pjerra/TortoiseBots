#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"

using namespace ai;

namespace
{
static uint32 GetQuestSlotQuestId(Player* player, uint16 slot)
{
    return GetQuestSlotIdCompat(player, slot);
}

} // namespace

void PlayerbotAI::DropQuest(uint32 questIdToDrop)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
            continue;

        if (questId == questIdToDrop)
        {
            if (Quest const* q = sObjectMgr.GetQuestTemplate(questIdToDrop))
                sPlayerbotAIConfig.logEvent(this, "QuestDropped", q->GetTitle(), std::to_string(questIdToDrop));

            bot->RemoveQuest(questId);


            return;
        }
    }
}

std::vector<const Quest*> PlayerbotAI::GetAllCurrentQuests()
{
    std::vector<const Quest*> result;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
        {
            continue;
        }

        result.push_back(sObjectMgr.GetQuestTemplate(questId));
    }

    return result;
}

std::vector<const Quest*> PlayerbotAI::GetCurrentIncompleteQuests()
{
    std::vector<const Quest*> result;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
        {
            continue;
        }

        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
        {
            result.push_back(sObjectMgr.GetQuestTemplate(questId));
        }
    }

    return result;
}

std::set<uint32> PlayerbotAI::GetAllCurrentQuestIds()
{
    std::set<uint32> result;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
        {
            continue;
        }

        result.insert(questId);
    }

    return result;
}

std::set<uint32> PlayerbotAI::GetCurrentIncompleteQuestIds()
{
    std::set<uint32> result;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
        {
            continue;
        }

        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
        {
            result.insert(questId);
        }
    }

    return result;
}

const Quest* PlayerbotAI::GetCurrentIncompleteQuestWithId(uint32 pQuestId)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);
        if (pQuestId == questId && status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
            return sObjectMgr.GetQuestTemplate(questId);
    }

    return nullptr;
}

bool PlayerbotAI::HasCurrentIncompleteQuestWithId(uint32 pQuestId)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);
        if (pQuestId == questId && status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
            return true;
    }

    return false;
}

/*
* @return vector of pair<quest, count>
*/
std::vector<std::pair<const Quest*, uint32>> PlayerbotAI::GetCurrentQuestsRequiringItemId(uint32 itemId)
{
    std::vector<std::pair<const Quest*, uint32>> result;

    if (!itemId)
    {
        return result;
    }

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = GetQuestSlotQuestId(bot, slot);
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);
        const Quest* quest = sObjectMgr.GetQuestTemplate(questId);
        for (uint8 i = 0; i < std::size(quest->ReqItemId); ++i)
        {
            if (quest->ReqItemId[i] == itemId)
            {
                result.push_back(std::pair(quest, quest->ReqItemCount[i]));
                break;
            }
        }
    }

    return result;
}
