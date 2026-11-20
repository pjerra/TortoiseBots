// Out-of-line parts of the W4-A2 compat pair (forever/src/compat/TbCompat_WL4.h): the three travel data sets ForeverCore
// builds for itself, the reduced value answers, and the registry check of the purpose gate. Compiled only with the slice
// (TORTOISEBOTS_SLICE1); it includes the bot brain, so the OFF build leaves it out (CMakeLists.txt filters it like
// TbCompat_C.cpp).
#include "compat/botpch_forever.h"

#include "TbCompat_WL4.h"

#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/policy/travel/TravelPurposeGatePolicy.h"

#include <mutex>
#include <set>

using namespace ai;

namespace
{
    // ===============================================================================================================
    // The quest relation map: who starts and who ends a classic quest, plus the creature/game-object objectives.
    // ===============================================================================================================

    void TravelAddQuestRelationRows(EntryQuestRelationMap& out, char const* sql, int32 sign, bool giver)
    {
        QueryResult result = WorldQueryCompat(sql);
        if (!result)
            return;

        uint32 const flag = uint32(giver ? TravelDestinationPurpose::QuestGiver : TravelDestinationPurpose::QuestTaker);
        do
        {
            Field* fields = result->Fetch();
            uint32 const entry = fields[0].GetUInt32();
            uint32 const questId = fields[1].GetUInt32();

            // The three retail leftovers on the classic quest table (challenge NPCs and their quest).
            if (entry == 81030 || entry == 62609 || questId == 80388)
                continue;

            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || quest->GetExpansion() != 0)
                continue;

            out[sign * int32(entry)][questId] |= flag;
        } while (result->NextRow());
    }

    EntryQuestRelationMap const& TravelQuestRelationMapData()
    {
        static EntryQuestRelationMap const map = []()
        {
            EntryQuestRelationMap out;

            TravelAddQuestRelationRows(out, "SELECT id, quest FROM creature_queststarter", 1, true);
            TravelAddQuestRelationRows(out, "SELECT id, quest FROM creature_questender", 1, false);
            TravelAddQuestRelationRows(out, "SELECT id, quest FROM gameobject_queststarter", -1, true);
            TravelAddQuestRelationRows(out, "SELECT id, quest FROM gameobject_questender", -1, false);

            // Creature and game-object objectives of classic quests, by objective slot (QuestObjective1..4).
            for (auto const& [questId, questPtr] : sObjectMgr.GetQuestTemplates())
            {
                Quest const* quest = questPtr.get();
                if (!quest || quest->GetExpansion() != 0)
                    continue;

                QuestObjectives const& objectives = quest->GetObjectives();
                for (std::size_t slot = 0; slot < objectives.size() && slot < 4; ++slot)
                {
                    QuestObjective const& objective = objectives[slot];
                    int32 entry = 0;
                    if (objective.Type == QUEST_OBJECTIVE_MONSTER)
                        entry = objective.ObjectID;
                    else if (objective.Type == QUEST_OBJECTIVE_GAMEOBJECT)
                        entry = -objective.ObjectID;
                    else
                        continue;

                    if (entry && entry != -81030 && entry != -62609)
                        out[entry][questId] |= 1u << (slot + 1);
                }
            }

            return out;
        }();

        return map;
    }

    // ===============================================================================================================
    // The spawn index: every creature and game-object spawn point of a template, area resolved once.
    // ===============================================================================================================

    EntryGuidps const& TravelEntryGuidpsData()
    {
        static EntryGuidps const index = []()
        {
            EntryGuidps out;

            for (auto const& spawn : sObjectMgr.GetAllCreatureData())
            {
                AsyncGuidPosition point(&spawn);
                if (!point.isValid() || point.IsEventUnspawned())
                    continue;

                point.FetchArea();   // boot only: the area flag is cached, no rule below loads terrain again
                out[point.GetEntry()].push_back(point);
            }

            for (auto const& spawn : sObjectMgr.GetAllGameObjectData())
            {
                AsyncGuidPosition point(&spawn);
                if (!point.isValid() || point.IsEventUnspawned())
                    continue;

                point.FetchArea();
                out[-int32(point.GetEntry())].push_back(point);
            }

            sLog.outString(">> Loaded %u travel destination spawn entries.", uint32(out.size()));
            return out;
        }();

        return index;
    }

    // ===============================================================================================================
    // The purpose map: what a template can be visited for. The reduced classic set (grind, town, vendor, repair); the
    // trainer, auction, mail, boss and gather purposes belong to slices this core does not build yet.
    // ===============================================================================================================

    EntryTravelPurposeMap const& TravelEntryPurposeMapData()
    {
        static EntryTravelPurposeMap const map = []()
        {
            EntryQuestRelationMap const& relations = TravelQuestRelationMapData();
            EntryTravelPurposeMap out;

            std::vector<uint32> const serviceFlags =
            {
                UNIT_NPC_FLAG_INNKEEPER, UNIT_NPC_FLAG_GOSSIP, UNIT_NPC_FLAG_BANKER, UNIT_NPC_FLAG_STABLEMASTER,
                UNIT_NPC_FLAG_PETITIONER, UNIT_NPC_FLAG_TABARDDESIGNER, UNIT_NPC_FLAG_TRAINER
            };

            for (auto const& [entry, info] : sObjectMgr.GetCreatureTemplates())
            {
                // The legacy template filter: an invisible marker NPC is not a place to walk to.
                if (info.flags_extra & CREATURE_FLAG_EXTRA_TRIGGER)
                    continue;

                DestinationPurose purpose = 0;

                auto relation = relations.find(int32(entry));
                if (relation != relations.end())
                    for (auto const& [questId, flag] : relation->second)
                        purpose |= flag;

                uint32 const npcFlags = CreatureNpcFlagsWL4(&info);
                for (uint32 flag : serviceFlags)
                {
                    if (npcFlags & flag)
                    {
                        purpose |= uint32(TravelDestinationPurpose::GenericRpg);
                        break;
                    }
                }

                if (npcFlags & UNIT_NPC_FLAG_REPAIR)
                    purpose |= uint32(TravelDestinationPurpose::Repair);

                // A flagged-but-stockless NPC is not a vendor destination: the trip would sell nothing and each hello
                // prints a core "empty trading item list" error.
                if ((npcFlags & UNIT_NPC_FLAG_VENDOR) && sObjectMgr.GetNpcVendorItemList(entry))
                    purpose |= uint32(TravelDestinationPurpose::Vendor);

                // Coinless wildlife is a grind destination too (XP, grey loot, skins); critters pay nothing.
                if (CreatureGoldMinWL4(entry) > 0 || info.type != CREATURE_TYPE_CRITTER)
                    purpose |= uint32(TravelDestinationPurpose::Grind);

                if (purpose > 0)
                    out[int32(entry)] = purpose;
            }

            for (auto const& [entry, info] : sObjectMgr.GetGameObjectTemplates())
            {
                DestinationPurose purpose = 0;

                auto relation = relations.find(-int32(entry));
                if (relation != relations.end())
                    for (auto const& [questId, flag] : relation->second)
                        purpose |= flag;

                if (purpose > 0)
                    out[-int32(entry)] = purpose;
            }

            sLog.outString(">> Loaded %u travel destination purpose entries.", uint32(out.size()));
            return out;
        }();

        return map;
    }

    // ===============================================================================================================
    // The purpose gate's facts: does the bot's own registry hold the keys of the slice that serves a purpose?
    // ===============================================================================================================

    struct TravelRegisteredKeys
    {
        bool vendor = false;
        bool quest = false;
        bool checked = false;
    };

    TravelRegisteredKeys const& TravelRegistryFacts(PlayerbotAI* ai)
    {
        static std::mutex mutex;
        static TravelRegisteredKeys facts;

        std::lock_guard<std::mutex> guard(mutex);
        if (!facts.checked && ai && ai->GetAiObjectContext())
        {
            std::set<std::string> actions, strategies;
            ai->GetAiObjectContext()->GetSupportedActions(actions);
            ai->GetAiObjectContext()->GetSupportedStrategies(strategies);

            facts.vendor = actions.count("sell") && actions.count("repair") && strategies.count("maintenance");
            facts.quest = strategies.count("rpg quest") > 0;
            facts.checked = true;
        }

        return facts;
    }

    travel_purpose::Group GroupOf(TravelDestinationPurpose purpose)
    {
        switch (purpose)
        {
        case TravelDestinationPurpose::Vendor:
            return travel_purpose::Group::Vendor;
        case TravelDestinationPurpose::Repair:
            return travel_purpose::Group::Repair;
        case TravelDestinationPurpose::GenericRpg:
            return travel_purpose::Group::Town;
        case TravelDestinationPurpose::Grind:
            return travel_purpose::Group::Grind;
        default:
            return travel_purpose::Group::Quest;
        }
    }

    bool IsQuestPurpose(TravelDestinationPurpose purpose)
    {
        uint32 const flag = uint32(purpose);
        return (flag & uint32(TravelDestinationPurpose::QuestGiver)) ||
            (flag & uint32(TravelDestinationPurpose::QuestTaker)) ||
            (flag & uint32(TravelDestinationPurpose::QuestAllObjective));
    }
}

namespace ai
{
    EntryQuestRelationMap const& TravelQuestRelationMapCompat()
    {
        return TravelQuestRelationMapData();
    }

    EntryGuidps const& TravelEntryGuidpsCompat()
    {
        return TravelEntryGuidpsData();
    }

    EntryTravelPurposeMap const& TravelEntryPurposeMapCompat()
    {
        return TravelEntryPurposeMapData();
    }

    EntryGuidps TravelEntryGuidpsLegacyCompat()
    {
        return TravelEntryGuidpsData();
    }

    EntryTravelPurposeMap TravelEntryPurposeMapLegacyCompat()
    {
        return TravelEntryPurposeMapData();
    }

    bool TravelPurposeRegisteredCompat(PlayerbotAI* ai, TravelDestinationPurpose purpose)
    {
        TravelRegisteredKeys const& keys = TravelRegistryFacts(ai);
        travel_purpose::Registered const registered{ keys.vendor, keys.quest };

        if (IsQuestPurpose(purpose))
            return travel_purpose::Allows(travel_purpose::Group::Quest, registered);

        return travel_purpose::Allows(GroupOf(purpose), registered);
    }

    // The named purposes (city, pvp, guild meeting, guild order, petition, mount, trainer ...) are the services of other
    // waves: W8 (auction/mail), W9 (guild), W2 (trainer). None is a travel target on this core yet.
    bool TravelNamedPurposeCompat(PlayerbotAI* /*ai*/, Player* /*bot*/, ChatHelper* /*chat*/, std::string const& /*qualifier*/,
        TravelProbeCache& /*activeTrainerProbe*/)
    {
        return false;
    }

    // The numeric purposes travel actually asks on this core. Quest purposes are not asked by name (the legacy switch never
    // had them either): their destinations are gated where they are built and picked.
    bool TravelPurposeNeedCompat(PlayerbotAI* ai, std::string const& qualifier, TravelProbeCache& activeNodeProbe)
    {
        (void)activeNodeProbe;
        Player* bot = ai ? ai->GetBot() : nullptr;
        AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
        if (!bot || !context)
            return false;

        TravelDestinationPurpose purpose;
        try
        {
            purpose = TravelDestinationPurpose(std::stoi(qualifier));
        }
        catch (...)
        {
            return false;
        }

        if (!TravelPurposeRegisteredCompat(ai, purpose))
            return false;

        switch (purpose)
        {
        case TravelDestinationPurpose::Grind:
        {
            if (AI_VALUE2(bool, "manual bool", "is travel refresh"))
                return false;

            uint32 const rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);
            return rpgPhase <= 45; // only the first 45 minutes of the hour allow a grind errand
        }
        case TravelDestinationPurpose::GenericRpg:
        {
            uint32 const rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);
            if (rpgPhase < 15) // only the last 45 minutes of the hour allow generic rpg
                return false;

            return !AI_VALUE2(bool, "manual bool", "is travel refresh");
        }
        case TravelDestinationPurpose::Vendor:
            // The need itself is W2's: until its values are registered they answer false, and no trip starts (LESS).
            return AI_VALUE2(bool, "has strategy", "maintenance") && AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell");
        case TravelDestinationPurpose::Repair:
            return AI_VALUE2(bool, "has strategy", "maintenance") && AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair");
        default:
            return false;
        }
    }

    // Outgrown-zone trigger on this core: the bot's zone level against its own level, pool bots only. The legacy
    // quest/hand-in yields, the targetless-leave latch and the grind dry-streak are W3's and W0's concerns; without them
    // the rule fires on the zone alone (LESS: a bot with a pending hand-in leaves with it).
    bool TravelLeaveOutgrownCompat(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || !sPlayerbotAIConfig.leaveOutgrownZones)
            return false;

        if (!sRandomBotFacade.IsFreeBot(bot) || ai->HasRealPlayerMaster())
            return false;

        if (!bot->IsAlive() || bot->GetLevel() < 10)
            return false;

        AreaTableEntry const* botArea = WorldPosition(bot).GetArea();
        uint32 const zoneId = botArea ? (AreaZoneIdCompat(botArea) ? AreaZoneIdCompat(botArea) : AreaIdWL4(botArea)) : 0;

        int32 zoneLevel = 0;
        if (!zoneId || !sTravelMgr.TryGetValidatedAreaLevel(zoneId, zoneLevel) || zoneLevel <= 0)
            return false;

        return zoneLevel + 5 < int32(bot->GetLevel());
    }

    bool TravelQuestStageActiveCompat(Player* bot, std::string const& qualifier)
    {
        if (!bot)
            return false;

        uint32 const questId = getMultiQualifierInt(qualifier, 0, ",");
        TravelDestinationPurpose const purpose = TravelDestinationPurpose(getMultiQualifierInt(qualifier, 1, ","));

        uint32 objective = 0;
        switch (purpose)
        {
        case TravelDestinationPurpose::QuestGiver:
            if (PlayerHasQuestWL4(bot, questId))
                return false;
            break;
        case TravelDestinationPurpose::QuestTaker:
            if (bot->GetQuestRewardStatus(questId))
                return false;
            if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !bot->CanCompleteQuest(questId))
                return false;
            break;
        case TravelDestinationPurpose::QuestObjective1: objective = 1; break;
        case TravelDestinationPurpose::QuestObjective2: objective = 2; break;
        case TravelDestinationPurpose::QuestObjective3: objective = 3; break;
        case TravelDestinationPurpose::QuestObjective4: objective = 4; break;
        default: break;
        }

        if (objective)
        {
            AiObjectContext* context = bot->GetPlayerbotAI()->GetAiObjectContext();
            if (!context->HasValue("need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}"))
                return false;
            if (!context->GetValue<bool>("need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}")->Get())
                return false;
        }

        return true;
    }

    // The travel graph arrives with the node-store round (W4-A1): TravelNode.cpp is not in the slice yet, so the loader
    // has nothing to read and the graph stays empty. Travel then picks no target - the same answer a missing node table
    // gives - and says so once. W4-A1 wires its node store in here.
    void TravelGraphLoadCompat()
    {
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            sLog.outString("TortoiseBots: the travel node store is not ported yet; travel has no graph and picks no target.");
        }
    }
}
