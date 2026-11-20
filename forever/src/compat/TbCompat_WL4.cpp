// Out-of-line parts of the W4-A2 compat pair (forever/src/compat/TbCompat_WL4.h): the three travel data sets ForeverCore
// builds for itself, the reduced value answers, and the registry check of the purpose gate. Compiled only with the slice
// (TORTOISEBOTS_SLICE1); it includes the bot brain, so the OFF build leaves it out (CMakeLists.txt filters it like
// TbCompat_C.cpp).
#include "compat/botpch_forever.h"

#include "TbCompat_WL4.h"

#include "playerbot/TravelMgr.h"
#include "playerbot/TravelNode.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/policy/travel/TravelPurposeGatePolicy.h"

#include "Maps/TerrainMgr.h"

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

            TravelAddQuestRelationRows(out, "SELECT `id`, `quest` FROM `creature_queststarter`", 1, true);
            TravelAddQuestRelationRows(out, "SELECT `id`, `quest` FROM `creature_questender`", 1, false);
            TravelAddQuestRelationRows(out, "SELECT `id`, `quest` FROM `gameobject_queststarter`", -1, true);
            TravelAddQuestRelationRows(out, "SELECT `id`, `quest` FROM `gameobject_questender`", -1, false);

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

    // One index at boot, held only while the destinations are built: the loader copies the points it keeps into
    // TravelMgr::pointsMap, so the boot copy is dropped afterwards (TravelEntryGuidpsReleaseCompat) instead of staying
    // resident beside them.
    std::shared_ptr<EntryGuidps const>& TravelEntryGuidpsCache()
    {
        static std::shared_ptr<EntryGuidps const> cache;
        return cache;
    }

    EntryGuidps TravelBuildEntryGuidps()
    {
        uint32 const started = getMSTime();
        EntryGuidps out;
        uint32 spawns = 0, skippedMaps = 0;

        // TerrainMgr keeps only a weak reference to a map's terrain: with no live map yet, every area lookup would
        // reload the continent's grid files. Holding the two continents' terrain for this pass makes each lookup a
        // grid read (the first one per grid loads it) and releases everything when the index is built.
        std::vector<std::shared_ptr<TerrainInfo>> terrain;
        for (uint32 mapId : { 0u, 1u })
            if (std::shared_ptr<TerrainInfo> continent = sTerrainMgr.LoadTerrain(mapId))
                terrain.push_back(continent);

        // Travel covers the two continents; the instance and battleground spawns are no destination and no terrain
        // file of theirs is loaded for this.
        auto indexedMap = [](uint32 mapId) { return mapId <= 1; };

        for (auto const& spawn : sObjectMgr.GetAllCreatureData())
        {
            if (!indexedMap(spawn.second.mapId)) { ++skippedMaps; continue; }

            // The spawn's guid here is the entry + the spawn id (the shim's pair constructor has no ForeverCore form).
            AsyncGuidPosition point(GuidPosition(ObjectGuid::Create<HighGuid::Creature>(spawn.second.mapId, spawn.second.id, spawn.first),
                WorldPosition(spawn.second.mapId, spawn.second.spawnPoint.GetPositionX(), spawn.second.spawnPoint.GetPositionY(),
                    spawn.second.spawnPoint.GetPositionZ(), spawn.second.spawnPoint.GetOrientation())));
            if (!point.isValid() || point.IsEventUnspawned())
                continue;

            point.FetchArea();   // boot only: the area flag is cached, no rule below loads terrain again
            out[point.GetEntry()].push_back(point);

            if ((++spawns % 20000) == 0)
                sLog.outString(">> travel destination index: %u spawns, %u ms", spawns, GetMSTimeDiffToNow(started));
        }

        for (auto const& spawn : sObjectMgr.GetAllGameObjectData())
        {
            if (!indexedMap(spawn.second.mapId)) { ++skippedMaps; continue; }

            AsyncGuidPosition point(GuidPosition(ObjectGuid::Create<HighGuid::GameObject>(spawn.second.mapId, spawn.second.id, spawn.first),
                WorldPosition(spawn.second.mapId, spawn.second.spawnPoint.GetPositionX(), spawn.second.spawnPoint.GetPositionY(),
                    spawn.second.spawnPoint.GetPositionZ(), spawn.second.spawnPoint.GetOrientation())));
            if (!point.isValid() || point.IsEventUnspawned())
                continue;

            point.FetchArea();
            out[-int32(point.GetEntry())].push_back(point);

            if ((++spawns % 20000) == 0)
                sLog.outString(">> travel destination index: %u spawns, %u ms", spawns, GetMSTimeDiffToNow(started));
        }

        sLog.outString(">> Loaded %u travel destination spawn entries (%u spawns on maps 0/1, %u skipped on other maps) in %u ms.",
            uint32(out.size()), spawns, skippedMaps, GetMSTimeDiffToNow(started));
        return out;
    }

    // Set once the loader dropped the boot index. After that the index is NOT rebuilt: a later reader (W2's MaintenanceValues
    // reads `entry guidps` once) would silently pay a 2 s world-thread rebuild with the terrain held again and keep a second copy
    // of 123k points resident. It gets an empty index and one log line naming the caller's miss instead.
    bool& TravelEntryGuidpsReleased()
    {
        static bool released = false;
        return released;
    }

    EntryGuidps const& TravelEntryGuidpsData()
    {
        if (TravelEntryGuidpsReleased())
        {
            static EntryGuidps const empty;
            static std::once_flag logged;
            std::call_once(logged, []()
            {
                sLog.outError("TortoiseBots: the travel destination spawn index was asked for after the boot build released it; "
                    "answering empty (it is not rebuilt at runtime). A reader of `entry guidps` needs the index kept: change the release.");
            });
            return empty;
        }

        std::shared_ptr<EntryGuidps const>& cache = TravelEntryGuidpsCache();
        if (!cache)
            cache = std::make_shared<EntryGuidps const>(TravelBuildEntryGuidps());

        return *cache;
    }

    // The loader's points are copies in TravelMgr::pointsMap; on this core the boot index has no other reader, so it is
    // dropped after the destination build and a later ask answers empty (TravelEntryGuidpsData above), never a rebuild.
    void TravelReleaseEntryGuidps()
    {
        std::shared_ptr<EntryGuidps const>& cache = TravelEntryGuidpsCache();
        if (!cache)
            return;

        size_t entries = 0, points = 0;
        for (auto const& entry : *cache)
        {
            ++entries;
            points += entry.second.size();
        }

        cache.reset();
        TravelEntryGuidpsReleased() = true;

        sLog.outString(">> travel destination index released: %u entries, %u spawn points", uint32(entries), uint32(points));
    }

    // ===============================================================================================================
    // The purpose map: what a template can be visited for. The reduced classic set (grind, town, vendor, repair); the
    // trainer, auction, mail, boss and gather purposes belong to slices this core does not build yet.
    // ===============================================================================================================

    EntryTravelPurposeMap const& TravelEntryPurposeMapData()
    {
        static EntryTravelPurposeMap const map = []()
        {
            uint32 const started = getMSTime();
            EntryQuestRelationMap const& relations = TravelQuestRelationMapData();
            // Only a template with a spawn point on maps 0/1 can become a destination: the loader drops the others, and
            // keeping them here (about 200k rows, most of them unused) costs memory for nothing.
            EntryGuidps const& spawns = TravelEntryGuidpsData();
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

                if (spawns.find(int32(entry)) == spawns.end())
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
                if (spawns.find(-int32(entry)) == spawns.end())
                    continue;

                DestinationPurose purpose = 0;

                auto relation = relations.find(-int32(entry));
                if (relation != relations.end())
                    for (auto const& [questId, flag] : relation->second)
                        purpose |= flag;

                if (purpose > 0)
                    out[-int32(entry)] = purpose;
            }

            sLog.outString(">> Loaded %u travel destination purpose entries in %u ms.", uint32(out.size()),
                GetMSTimeDiffToNow(started));
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
            std::set<std::string> actions, strategies, values;
            ai->GetAiObjectContext()->GetSupportedActions(actions);
            ai->GetAiObjectContext()->GetSupportedStrategies(strategies);
            ai->GetAiObjectContext()->GetSupportedValues(values);

            // W2's vendor keys: the two actions, the strategy, and the `vendor trip needed` value the need below reads.
            facts.vendor = actions.count("sell") && actions.count("repair") && strategies.count("maintenance") &&
                values.count("vendor trip needed");
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

    void TravelEntryGuidpsReleaseCompat()
    {
        TravelReleaseEntryGuidps();
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

            // The first 45 minutes of the hour allow a grind errand; a beginner (a free pool bot below level 5, no real master)
            // grinds all hour, as on Turtle (issue #393): the RPG walk of the last 15 minutes refuses it below level 5.
            return travel_purpose::GrindPhaseOpen(bot->GetLevel(), sRandomBotFacade.IsFreeBot(bot), ai->HasRealPlayerMaster(),
                ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1));
        }
        case TravelDestinationPurpose::GenericRpg:
        {
            uint32 const rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);
            if (rpgPhase < 15) // only the last 45 minutes of the hour allow generic rpg
                return false;

            return !AI_VALUE2(bool, "manual bool", "is travel refresh");
        }
        case TravelDestinationPurpose::Vendor:
            // The real need, not the retired `should sell && can sell` pair (issue #379: true for every pool bot holding food, 2,300
            // vendor errands in 65 minutes): W2's `vendor trip needed` value, the snapshot of the Turtle side's
            // `VendorTripNeeded`. The purpose is already refused above until W2's keys (that value among them) are registered, so
            // this line never asks a value the context does not have. The Turtle `BagPressureVendorTrip` valve is W2's request
            // path (ChooseTravelTargetAction), not a value, and comes with it.
            {
                TravelRegisteredKeys const& keys = TravelRegistryFacts(ai);
                travel_purpose::Registered const registered{ keys.vendor, keys.quest };
                bool const tripNeeded = registered.vendorKeys && AI_VALUE(bool, "vendor trip needed");
                return travel_purpose::VendorErrandNeeded(registered, tripNeeded);
            }
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

        uint32 const questId = Qualified::getMultiQualifierInt(qualifier, 0, ",");
        TravelDestinationPurpose const purpose = TravelDestinationPurpose(Qualified::getMultiQualifierInt(qualifier, 1, ","));

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
            AiObjectContext* context = PlayerbotAIStorage::Instance().GetAI(bot)->GetAiObjectContext();
            if (!context->HasValue("need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}"))
                return false;
            if (!context->GetValue<bool>("need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}")->Get())
                return false;
        }

        return true;
    }

    // The travel graph: the shipped node store (W4-A1), read through the schema guard. The shipped graphs themselves are the
    // generation boot's output (round B); until it runs, the three tables are missing or empty, loadNodeStore says so once and
    // leaves an empty graph, generateAll returns on it, and travel picks no target - no error, no query on a missing table.
    void TravelGraphLoadCompat()
    {
        sTravelNodeMap.loadNodeStore();
        sTravelNodeMap.generateAll();

        // A small graph (the boot check's) names what each node reaches over the links the A* may take, so the check can see that a
        // zeppelin or a skipped bad link is no route. A real graph has hundreds of nodes: no line.
        std::vector<TravelNode*> const nodes = sTravelNodeMap.GetNodes();
        if (!nodes.empty() && nodes.size() <= 16)
        {
            std::string reach;
            for (TravelNode* node : nodes)
                reach += (reach.empty() ? "" : " ") + node->GetName() + "=" + std::to_string(node->getRouteSize());
            sLog.outString(">> travel graph reach (usable links): %s", reach.c_str());
        }

        sTravelNodeMap.printMap();
        sTravelNodeMap.printNodeStore();
        if (sPlayerbotAIConfig.generateTravelNodes)
            sTravelNodeMap.saveNodeStore();
    }
}
