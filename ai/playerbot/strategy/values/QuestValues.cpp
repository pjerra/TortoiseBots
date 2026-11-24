
#include "playerbot/playerbot.h"
#include "QuestValues.h"
#include "SharedValueContext.h"
#include "ItemUsageValue.h"
#include "MaintenanceValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TbCompatW2.h"
#include "playerbot/TbCompatW3.h"
#include "playerbot/TbCompatWL4.h"

#include <unordered_set>

using namespace ai;


//What kind of a relation does this entry have with this quest.
EntryQuestRelationMap EntryQuestRelationMapValue::Calculate()
{
	EntryQuestRelationMap rMap;

	auto addRelations = [&rMap](int32 entry)
	{
		// Filter out challenge NPCs like Mysterious Stranger (81030) and Echo (62609) and their quest.
		if (entry == 81030 || entry == 62609)
			return;

		for (bool giver : { true, false })
		{
			uint32 flag = uint32(giver ? TravelDestinationPurpose::QuestGiver : TravelDestinationPurpose::QuestTaker);

			ForEachQuestRelationCompat(entry, giver, [&rMap, entry, flag](uint32 questId)
			{
				if (questId != 80388)
					rMap[entry][questId] |= flag;
			});
		}
	};

	// The keys come from the entries the world spawns, not from the relation tables: every reader asks this map by the
	// entry of a spawn point (TravelMgr's destination build, FindQuestObjectData), so an entry without a spawn can never
	// change an answer, and the cores expose the relation tables whole only per entry.
	std::unordered_set<uint32> creatureEntries;
	auto collectCreatureEntries = [&creatureEntries](CreatureDataPair const& dataPair)
	{
		creatureEntries.insert(SpawnCreatureEntryWL4(dataPair.second));
		return false;
	};
	DoCreatureDataCompat(collectCreatureEntries);
	for (uint32 entry : creatureEntries)
		addRelations(int32(entry));

	std::unordered_set<uint32> gameObjectEntries;
	auto collectGameObjectEntries = [&gameObjectEntries](GameObjectDataPair const& dataPair)
	{
		gameObjectEntries.insert(dataPair.second.id);
		return false;
	};
	DoGameObjectDataCompat(collectGameObjectEntries);
	for (uint32 entry : gameObjectEntries)
		addRelations(-int32(entry));

	//Quest objectives
	auto const& questMap = sObjectMgr.GetQuestTemplates();

	for (auto& [questId, questPtr] : questMap)
	{
		Quest* quest = questPtr.get();

		for (uint32 objective = 0; objective < QuestObjectiveSlotCountWL4(quest); objective++)
		{
			uint32 relationFlag = 1 << (objective+1);

			//Kill objective
			if (int32 objectiveEntry = QuestReqCreatureOrGOIdWL4(quest, objective))
				rMap[objectiveEntry][questId] |= relationFlag;

			//Loot objective
			if (uint32 itemId = QuestReqItemIdWL4(quest, objective))
			{
				while (itemId)
				{
					ItemPrototype const* proto = GetItemProtoByIdCompat(itemId);

					if (proto)
					{
						for (auto& entry : GAI_VALUE2_COMPAT(std::list<int32>, "item drop list", itemId))
						{
							std::string chanceQualifier = std::to_string(entry) + " " + std::to_string(itemId);
							if (proto->Class == ITEM_CLASS_QUEST || GAI_VALUE2_COMPAT(float, "loot chance", chanceQualifier) > 5.0f)
								rMap[entry][questId] |= relationFlag;
						}
					}

					itemId = ItemUsageValue::ItemCreatedFrom(itemId);
				}
			}

			//Buy from vendor objective
			if (uint32 itemId = QuestReqItemIdWL4(quest, objective))
			{
				for (auto& entry : GAI_VALUE2_COMPAT(std::list<int32>, "item vendor list", itemId))
					rMap[entry][questId] |= relationFlag;
			}
		}

		//Target entry of source item of quest.
		if (quest->GetSrcItemId())
			for (int32 targetEntry : QuestRequiredTargetEntriesCompat(quest->GetSrcItemId()))
				rMap[targetEntry][questId] |= (uint8)TravelDestinationPurpose::QuestObjective2;
	}

	//Hard coded:
	return rMap;
}


//Get all the objective entries for a specific quest.
void FindQuestObjectData::GetObjectiveEntries()
{
	relationMap = GAI_VALUE_COMPAT(EntryQuestRelationMap, "entry quest relation");
}

//Data worker. Checks for a specific creature what quest they are needed for and puts them in the proper place in the quest map.
bool FindQuestObjectData::operator()(CreatureDataPair const& dataPair)
{
	uint32 entry = SpawnCreatureEntryWL4(dataPair.second);

	for (auto& [questId, flag] : relationMap[entry])
	{
		data[questId][flag][entry].push_back(GuidPosition(&dataPair));
	}

	return false;
}

//GameObject data worker. Checks for a specific gameObject what quest they are needed for and puts them in the proper place in the quest map.
bool FindQuestObjectData::operator()(GameObjectDataPair const& dataPair)
{
	int32 entry = dataPair.second.id * -1;

	for (auto& [questId,flag] : relationMap[entry])
	{
		data[questId][flag][entry].push_back(GuidPosition(&dataPair));
	}

	return false;
}

//Goes past all creatures and gameobjects and creatures the full quest guid map.
questGuidpMap QuestGuidpMapValue::Calculate()
{
	FindQuestObjectData worker;
	DoCreatureDataCompat(worker);
	DoGameObjectDataCompat(worker);
	return worker.GetResult();
}

//Selects all questgivers for a specific level (range).
questGiverMap QuestGiversValue::Calculate()
{
	uint32 level = 0;
	std::string q = getQualifier();
	bool hasQualifier = !q.empty();

	if (hasQualifier)
		level = stoi(q);

	questGuidpMap const questMap = GAI_VALUE_COMPAT(questGuidpMap, "quest guidp map");

	questGiverMap guidps;

	for (auto const& [questId, questRelationGuidps]: questMap)
	{
		auto it = questRelationGuidps.find((uint8)TravelDestinationPurpose::QuestGiver);
		if (it == questRelationGuidps.end())
			continue;

		for (auto const& entry : it->second)
		{
			for (auto const& guidp : entry.second)
			{
				if (hasQualifier)
				{
					Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

					if (quest && ((int32)level < QuestMinLevelWL4(quest) || (int32)level > QuestLevelCompat(quest) + 10))
						continue;
				}

				guidps[questId].push_back(guidp);
			}
		}
	}

	return guidps;
}

std::list<GuidPosition> ActiveQuestGiversValue::Calculate()
{
	questGiverMap const qGivers = GAI_VALUE2_COMPAT(questGiverMap, "quest givers", bot->GetLevel());

	std::list<GuidPosition> retQuestGivers;

	for (auto const& [questId, guidPs] : qGivers)
	{
		Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

		if (!quest || !QuestIsActiveCompat(quest))
		{
			continue;
		}

		if (!bot->CanTakeQuest(quest, false))
			continue;

		QuestStatus status = bot->GetQuestStatus(questId);

		if (status != QUEST_STATUS_NONE)
			continue;

		for (auto& guidp : guidPs)
		{
			CreatureInfo const* creatureInfo = guidp.GetCreatureTemplate();

			if (creatureInfo)
			{
				if (!ai->IsFriendlyTo(creatureInfo->faction))
					continue;
			}

			if (guidp.isDead(bot->GetInstanceId()))
				continue;

			retQuestGivers.push_back(guidp);
		}
	}

	return retQuestGivers;
}

std::list<GuidPosition> ActiveQuestTakersValue::Calculate()
{
	questGuidpMap const questMap = GAI_VALUE_COMPAT(questGuidpMap, "quest guidp map");

	std::list<GuidPosition> retQuestTakers;

		QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

	for (auto& [questId, questStatusData]: questStatusMap)
	{
		Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

		if (!quest || !QuestIsActiveCompat(quest))
		{
			continue;
		}

		QuestStatus status = questStatusData.Status;

		if ((status != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId)) && (!QuestIsAutoCompleteCompat(quest) || !bot->CanTakeQuest(quest, false)))
			continue;

		auto q = questMap.find(questId);

		if (q == questMap.end())
			continue;

		auto qt = q->second.find((uint8)TravelDestinationPurpose::QuestTaker);

		if (qt == q->second.end())
			continue;

		for (auto& [entry, guidps] : qt->second)
		{
			if (entry > 0)
			{
				CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);

				if (info)
				{
					if (!ai->IsFriendlyTo(info->faction))
						continue;
				}
			}

			for (auto& guidp : guidps)
			{
				if (guidp.isDead(bot->GetInstanceId()))
					continue;

				retQuestTakers.push_back(guidp);
			}
		}
	}

	return retQuestTakers;
}

std::list<GuidPosition> ActiveQuestObjectivesValue::Calculate()
{
	questGuidpMap const questMap = GAI_VALUE_COMPAT(questGuidpMap, "quest guidp map");

	std::list<GuidPosition> retQuestObjectives;

		QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

	for (auto& [questId, questStatusData] : questStatusMap)
	{
		Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

		if (!quest || !QuestIsActiveCompat(quest))
		{
			continue;
		}

		if (questStatusData.Status != QUEST_STATUS_INCOMPLETE)
			continue;

		for (uint32 objective = 0; objective < QuestObjectiveSlotCountWL4(quest); objective++)
		{
			uint32 hasKill = 0;
			uint32 hasItem = 0;
			QuestObjectiveCountersWL4(bot, questId, quest, objective, hasKill, hasItem);

			if (uint32 reqCount = QuestReqItemCountWL4(quest, objective))
			{
				if (hasItem >= reqCount)
					continue;
			}

			if (uint32 reqCount = QuestReqCreatureOrGOCountWL4(quest, objective))
			{
				if (hasKill >= reqCount)
					continue;
			}

			auto q = questMap.find(questId);

			if (q == questMap.end())
				continue;

			auto qt = q->second.find((uint8)TravelDestinationPurpose(1<<(objective+1)));

			if (qt == q->second.end())
				continue;

			for (auto& entry : qt->second)
			{
				for (auto& guidp : entry.second)
				{
					if (guidp.isDead(bot->GetInstanceId()))
						continue;

					retQuestObjectives.push_back(guidp);
				}
			}
		}
	}

	return retQuestObjectives;
}

bool NeedForQuestValue::Calculate()
{
	if (!Qualified::isValidNumberString(getQualifier()))
		return false;

	int32 entry = stoi(getQualifier());

	const PlayerTravelInfo& info(bot);

	std::list<GuidPosition> retQuestObjectives;

		QuestStatusMap& questStatusMap = bot->getQuestStatusMap();

	for (auto& questStatus : questStatusMap)
	{
		uint32 questId = questStatus.first;

		Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

		if (!quest || !QuestIsActiveCompat(quest))
		{
			continue;
		}

		QuestStatusData statusData = questStatus.second;

		if (statusData.Status != QUEST_STATUS_INCOMPLETE)
			continue;

		for (uint32 objective = 0; objective < QuestObjectiveSlotCountWL4(quest); objective++)
		{
			std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };

			if (!AI_VALUE2(bool, "need quest objective", Qualified::MultiQualify(qualifier, ",")))
				continue;

			DestinationList destinations = sTravelMgr.GetDestinations(info, uint32(TravelDestinationPurpose::QuestAllObjective), {int32(questId)}, false, 0);

			for (auto destination : destinations)
			{
                if (entry == destination->GetEntry())
					return true;

			}
		}
	}

	return false;
}

uint8 FreeQuestLogSlotValue::Calculate()
{
	uint8 numQuest = 0;
	for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
	{
		uint32 questId = GetQuestSlotIdCompat(bot, slot);

		if (!questId)
			continue;

		Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
		if (!quest)
			continue;

		numQuest++;
	}

	return MAX_QUEST_LOG_SIZE - numQuest;
}

bool HasFinishedQuestValue::Calculate()
{
	if (!HasRewardableFinishedQuest(ai))
		return false;

	// This value ranks the turn-in request above the grind errand (6.36 vs 6.35,
	// see TravelStrategy), so it must stand down while the quest purpose is
	// parked: a hand-in search that came back with no reachable taker would
	// otherwise re-fire as soon as ChooseTravelTargetAction's park expired, abort
	// the grind errand again, and repeat forever for a quest whose ender never
	// becomes walkable. The park's own bool cannot carry this - any successful
	// travel pick writes it back to false (setNewTarget clears the whole value
	// map) while the timestamp stands - so the timestamp decides here. The
	// nearby-service hand-in and the log-upkeep latch use the raw predicate
	// (HasRewardableFinishedQuest), so a bot standing next to its taker still
	// gets paid during the park.
	return AI_VALUE2(time_t, "manual time", "no travel purpose until::quest") <= time(0);
}

uint32 DialogStatusValue::getDialogStatus(Player* bot, int32 questgiver, uint32 questId)
{
	uint32 dialogStatus = DIALOG_STATUS_NONE;

	// Check markings for quest-finisher
	ForEachQuestRelationCompat(questgiver, false, [&](uint32 relationQuestId)
	{
		if (questId && relationQuestId != questId)
			return;

		Quest const* pQuest = sObjectMgr.GetQuestTemplate(relationQuestId);

		uint32 dialogStatusNew = DIALOG_STATUS_NONE;

		if (!pQuest || !QuestIsActiveCompat(pQuest))
		{
			return;
		}

		QuestStatus status = bot->GetQuestStatus(relationQuestId);

		if ((status == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(relationQuestId)) ||
			(QuestIsAutoCompleteCompat(pQuest) && bot->CanTakeQuest(pQuest, false)))
		{
			if (QuestIsAutoCompleteCompat(pQuest) && pQuest->IsRepeatable())
			{
				dialogStatusNew = DIALOG_STATUS_REWARD_REP;
			}
			else
			{
				dialogStatusNew = DIALOG_STATUS_REWARD2;
			}
		}
		else if (status == QUEST_STATUS_INCOMPLETE)
		{
			dialogStatusNew = DIALOG_STATUS_INCOMPLETE;
		}

		if (dialogStatusNew > dialogStatus)
		{
			dialogStatus = dialogStatusNew;
		}
	});

	// check markings for quest-giver
	ForEachQuestRelationCompat(questgiver, true, [&](uint32 relationQuestId)
	{
		if (questId && relationQuestId != questId)
			return;

		Quest const* pQuest = sObjectMgr.GetQuestTemplate(relationQuestId);

		uint32 dialogStatusNew = DIALOG_STATUS_NONE;

		if (!pQuest || !QuestIsActiveCompat(pQuest))
		{
			return;
		}

		QuestStatus status = bot->GetQuestStatus(relationQuestId);

		if (status == QUEST_STATUS_NONE)                    // For all other cases the mark is handled either at some place else, or with involved-relations already
		{
			if (bot->CanSeeStartQuest(pQuest))
			{
				if (bot->SatisfyQuestLevel(pQuest, false))
				{
					int32 lowLevelDiff = WorldI32ConfigW2(CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF);
					if (QuestIsAutoCompleteCompat(pQuest) || (pQuest->IsRepeatable() && QuestEverRewardedCompat(bot, relationQuestId)))
					{
						dialogStatusNew = DIALOG_STATUS_REWARD_REP;
					}
                    else if (lowLevelDiff < 0 || bot->GetLevel() <= QuestLevelForPlayerCompat(bot, pQuest) + uint32(lowLevelDiff) || pQuest->GetRequiredClasses())
					{
						dialogStatusNew = DIALOG_STATUS_AVAILABLE;
					}
					else
					{
						dialogStatusNew = DIALOG_STATUS_CHAT;
					}
				}
				else
				{
					dialogStatusNew = DIALOG_STATUS_UNAVAILABLE;
				}
			}
		}

		if (dialogStatusNew > dialogStatus)
		{
			dialogStatus = dialogStatusNew;
		}
	});

	return dialogStatus;
}

bool NeedQuestRewardValue::Calculate()
{
	uint32 questId = stoi(getQualifier());
	Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);

	if (pQuest->GetRequiredClasses()) //Always try to do class quests.
        return true;

	for (uint8 i = 0; i < pQuest->GetRewChoiceItemsCount(); ++i)
	{
		ItemUsage usage = AI_VALUE2_LAZY(ItemUsage, "item usage", QuestRewChoiceItemIdsW3(pQuest)[i]);
		if (usage == ItemUsage::ITEM_USAGE_EQUIP || usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)
			return true;
	}

	return false;
}

bool NeedQuestObjectiveValue::CanGetItemSomewhere(const uint32 itemId, const uint32 reqCount, Player* bot)
{
    if (!GAI_VALUE2_COMPAT(std::list<int32>, "item drop list", itemId).empty()) //Can get it from drop.
        return true;

    if (GAI_VALUE2_COMPAT(std::list<int32>, "item vendor list", itemId).empty()) //Can not get it from vendor.
        return true;

    ItemPrototype const* proto = GetItemProtoByIdCompat(itemId);

    if (!proto)
        return true;

    if (bot->GetMoney() > proto->BuyPrice * reqCount) //Bot has money to buy item from vendor.
        return true;

	return false;
}

bool NeedQuestObjectiveValue::Calculate()
{
	uint32 questId = getMultiQualifierInt(getQualifier(),0,",");
	if (!bot->IsActiveQuest(questId))
		return false;

	QuestStatusData& questStatus = bot->getQuestStatusMap().at(questId);

	if (getQualifier().find("reward") != std::string::npos)
    {
        return QuestRewardPendingCompat(bot, questId); //Still need the reward
    }

	if (questStatus.Status != QUEST_STATUS_INCOMPLETE)
		return false;

	if (getQualifier().find(",") == std::string::npos) //Status of entire quest.
		return true;


	Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);

	uint32 objective = getMultiQualifierInt(getQualifier(), 1, ",");

	uint32  reqCount = QuestReqItemCountWL4(pQuest, objective);
	uint32  hasCount = 0;
	uint32  hasKill = 0;
	QuestObjectiveCountersWL4(bot, questId, pQuest, objective, hasKill, hasCount);

	bool needItems = false;
    bool needCreatures = false;

	if (reqCount && hasCount < reqCount)
	{
		uint32 itemId = QuestReqItemIdWL4(pQuest, objective);

        if (CanGetItemSomewhere(itemId, reqCount, bot))
            needItems = true;
	}

	reqCount = QuestReqCreatureOrGOCountWL4(pQuest, objective);

	if (reqCount && hasKill < reqCount)
        needCreatures = true;

	return needItems || needCreatures;
}

bool CanUseItemOn::Calculate()
{
	uint32 itemId = getMultiQualifierInt(getQualifier(), 0, ",");
	std::string guidPString = getMultiQualifierStr(getQualifier(), 1, ",");
	GuidPosition guidP(guidPString);

	Unit* unit = nullptr;
	GameObject* gameObject = nullptr;

	if (!guidP)
		return false;

	switch (itemId)
    {
        case 17117:                                                                                                                 //Rat Catcher's Flute
            return guidP.IsCreature() && guidP.GetEntry() == 13016;                                                                 //Deeprun Rat
    }

	if (guidP.IsUnit())
		unit = guidP.GetUnit(bot->GetInstanceId());
	else if (guidP.IsGameObject())
		gameObject = guidP.GetGameObject(bot->GetInstanceId());

	ItemPrototype const* proto = GetItemProtoByIdCompat(itemId);

	if (!proto)
		return false;

	bool tryRandomCast = !urand(0, 20);

	uint32 spellId = proto->Spells[0].SpellId;
	if (spellId)
	{
		SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellId);

		if (unit)
		{
			if (spellInfo->Effect[0] == SPELL_EFFECT_DUMMY && spellInfo->Id == 19938)  // Awaken Lazy Peon (hardcoded in core!)
			{
				// 17743 = Lazy Peon Sleep | 10556 = Lazy Peon
				if (!unit->HasAura(17743) || unit->GetEntry() != 10556)
					return false;

				return true;
			}

			if (ai->CanCastSpell(spellId, unit, 0, false, nullptr, true))
				return tryRandomCast;
		}
		else if (gameObject)
		{
			if (ai->CanCastSpell(spellId, gameObject, 0, false, true))
				return tryRandomCast;
		}
	}

	return false;
};

bool HasNearbyQuestTakerValue::Calculate()
{
	std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible rpg targets");

	int32 travelEntry = AI_VALUE(TravelTarget*, "travel target")->GetEntry();

	for (auto& target : possibleTargets)
	{
		if (target.GetEntry() == travelEntry)
			continue;

		if(AI_VALUE2(bool, "can turn in quest npc", target.GetEntry()))
			return true;
	}

	std::list<ObjectGuid> possibleObjects = bot->GetMap()->IsDungeon() ? AI_VALUE(std::list<ObjectGuid>, "nearest game objects") : AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");

	for (auto& target : possibleObjects)
	{
		if (target.GetEntry() == (travelEntry * -1))
			continue;

		if (AI_VALUE2(bool, "can turn in quest npc", target.GetEntry()))
			return true;
	}

	return false;
};
