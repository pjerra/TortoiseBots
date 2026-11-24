// ForeverCore side of the both-core helpers of the world-life slice W4, round A2 (travel destinations: TravelMgr.cpp,
// TravelValues.cpp). Turtle side: ai/cmangos-compat-wl4.h (same names and signatures; each helper there is the expression
// the bot always made). Both are found through ai/playerbot/TbCompatWL4.h (`__has_include`), so the shared shim headers stay
// untouched; the wrapper is included AFTER TravelMgr.h/TravelValues.h/QuestValues.h, so the bot's own types are known here.
//
// What this pair carries (FOREVER-W0-W4-SPEC section 2.3, decisions D4/D5):
//  * field names the two cores spell differently (area id/level/team, creature level/rank/npc flags/gold/xp, quest level and
//    type, skill maxima, map entry id) - the golden rule: an unknown answer makes the bot do LESS, never more;
//  * the three travel data sets TravelMgr reads through the shared value context on Turtle (`entry guidps`, `entry travel
//    purpose`, `entry quest relation`): on ForeverCore they are built here from the core's own stores and the module's own
//    SQL, through WorldQueryCompat (TbCompat_S5.h), so a missing table answers "no rows" instead of aborting the server;
//  * the travel graph load, the run-log opening, the taxi zone cache and the fish locations, which are separate rounds
//    (W4-A1, W4-B, W5, W7) and answer "not there" here so travel runs without them (it then picks no target - LESS);
//  * the out-of-slice services the legacy destination rules read (guild orders, alt/hire lifecycle, trainer and vendor
//    needs): their symbols are answered with the "no such service" value by forever/src/slice/SliceLinkStubs.cpp, exactly
//    like the other slices' absent services.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "DataStores/DB2Stores.h"
#include "DataStores/DB2Structure.h"
#include "Entities/Creature/CreatureData.h"
#include "Entities/Creature/GossipDef.h"
#include "Entities/Player/Player.h"
#include "Globals/ObjectMgr.h"
#include "Maps/Map.h"
#include "Quests/QuestDef.h"
#include "TbCompat_S5.h"   // WorldQueryCompat: a missing module table answers no rows, never an abort
#include "World/World.h"

// The bot's own types (idempotent: TravelMgr.h/TravelValues.h/QuestValues.h are included by the callers first).
#include "playerbot/TravelMgr.h"      // WorldPosition, before ChatHelper.h names it by value
#include "playerbot/ChatHelper.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/policy/travel/CreatureLevelPolicy.h"

// The key the loader stores a spawn point under. ForeverCore's guid is two raw halves: the low one is only the spawn
// counter, which creatures and game objects count separately, so the high half (type, entry, map) is part of the key.
inline ai::TravelPointKey GuidKeyWL4(ObjectGuid const& guid)
{
    return ai::MakeTravelPointKey(guid.GetRawValue(1), guid.GetRawValue(0));
}

// ===============================================================================================================
// AREA TABLE (TravelMgr destination rules, WorldPosition helpers)
// ===============================================================================================================

// The area's id (Turtle `Id`, ForeverCore `ID`).
inline uint32 AreaIdWL4(AreaTableEntry const* area)
{
    return area ? area->ID : 0;
}

// The zone a sub-area belongs to, or the area's own id when it is already a zone (the legacy
// `AreaZoneIdCompat(area) ? AreaZoneIdCompat(area) : area->Id` expression).
inline uint32 AreaZoneIdOrSelfWL4(AreaTableEntry const* area)
{
    uint32 const zoneId = AreaZoneIdCompat(area);
    return zoneId ? zoneId : AreaIdWL4(area);
}

// The area's team (Turtle `Team`; ForeverCore carries the same 2 Alliance / 4 Horde / 6 both bits as the area's faction-group
// mask, AreaTeamCompat in TbCompat_C.h, the one WorldPosition::isEnemyHomeZoneFor reads). A sub-area usually carries none and
// the caller falls back to its zone, as on Turtle.
inline uint32 AreaTeamWL4(AreaTableEntry const* area)
{
    return area ? uint32(AreaTeamCompat(area)) : uint32(AREATEAM_NONE);
}

// The DBC area row by id (Turtle `AreaEntry::GetById`, ForeverCore the area table store).
inline AreaTableEntry const* AreaDbEntryWL4(uint32 areaId)
{
    return areaId ? sAreaTableStore.LookupEntry(areaId) : nullptr;
}

// The area table's row count and row lookup (Turtle `sAreaStore.GetMaxEntry()`/`LookupEntry<AreaEntry>`, ForeverCore
// `sAreaTableStore`).
inline uint32 AreaStoreCountWL4()
{
    return sAreaTableStore.GetNumRows();
}

inline AreaTableEntry const* AreaStoreLookupWL4(uint32 areaId)
{
    return sAreaTableStore.LookupEntry(areaId);
}

// Is the area a place exploration can be credited for? (Turtle has an `ExploreFlag` bit index, ForeverCore an `AreaBit`;
// -1 means the area carries none.)
inline bool AreaDiscoverableWL4(AreaTableEntry const* area)
{
    return area && area->AreaBit >= 0;
}

// Is an explorable area already discovered by the player? Turtle keeps the legacy bit math on the explored-zones update
// field; ForeverCore answers it per area id (Player::HasExploredZone), and an area with no exploration bit (AreaBit < 0)
// is not discoverable - treated as done, so the explore destination is not offered (LESS).
inline bool AreaExploredWL4(Player* player, AreaTableEntry const* area)
{
    if (!area || !player)
        return true;
    if (area->AreaBit < 0)
        return true;
    return player->HasExploredZone(area->ID);
}

// A snapshot of the online players (Turtle reads them under HashMapHolder<Player>::ReadGuard, ForeverCore's accessor hands
// out the same map and no guard handle).
inline std::vector<Player*> TravelPlayersCompat()
{
    std::vector<Player*> players;
    for (auto const& entry : ObjectAccessor::GetPlayers())
        players.push_back(entry.second);
    return players;
}

// Does GetAreaLevel resolve the parent zone before the DBC exploration level and skip the sub-area average? ForeverCore:
// yes (the sub-area average reads unknown levels and the parent zone is the generated zone level's fallback). Turtle: no.
inline bool AreaPreferParentCompat()
{
    return true;
}

// The level cap the explore rule compares against (Turtle `DEFAULT_MAX_LEVEL`, ForeverCore the world's configured cap).
inline uint32 DefaultMaxLevelWL4()
{
    return uint32(sWorld.getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
}

// ===============================================================================================================
// CREATURE TEMPLATE
// ===============================================================================================================

// The spawn's top level. Turtle `CreatureInfo::level_max`; ForeverCore has no per-template level, the vanilla levels live
// in `creature_classic_level` (ObjectMgr::GetCreatureClassicLevel, a min/max pair). An entry without a row has NO known level
// and answers ai::travel_level::kUnknown (above every level window), never 0: 0 sat inside the grind band of a level 1-2 bot
// and offered a fresh bot a spot that could be rated 60+ (review of W4-A2, decision 4; CreatureLevelPolicy.h).
inline int32 CreatureLevelMaxWL4(uint32 entry)
{
    std::pair<uint8, uint8> const* classic = ObjectMgr::instance()->GetCreatureClassicLevel(entry);
    return ai::travel_level::FromClassicRow(classic != nullptr, classic ? uint32(classic->second) : 0u);
}

// The template's rank as the legacy numbers: 0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare. ForeverCore's
// CreatureClassifications enum uses the same numbers for the first five values.
inline int32 CreatureRankWL4(CreatureInfo const* info)
{
    return info ? int32(info->Classification) : 0;
}

// The template's NPC flags (Turtle `npc_flags`, ForeverCore `npcflag`).
inline uint32 CreatureNpcFlagsWL4(CreatureInfo const* info)
{
    return info ? uint32(info->npcflag) : 0;
}

// The template's minimum coin drop (Turtle `gold_min`, ForeverCore the difficulty row's GoldMin).
inline uint32 CreatureGoldMinWL4(uint32 entry)
{
    CreatureTemplate const* info = ObjectMgr::instance()->GetCreatureTemplate(entry);
    CreatureDifficulty const* difficulty = info ? info->GetDifficulty(DIFFICULTY_NONE) : nullptr;
    return difficulty ? difficulty->GoldMin : 0;
}

// Does killing it pay experience at all (Turtle `xp_multiplier != 0`, ForeverCore the template's ModExperience).
inline bool CreaturePaysXpWL4(CreatureInfo const* info)
{
    return info && info->ModExperience != 0.0f;
}

// The creature's spawn entry in a spawn row (Turtle `CreatureData::creature_id[0]`, ForeverCore `CreatureData::id`).
inline uint32 SpawnCreatureEntryWL4(CreatureData const& data)
{
    return data.id;
}

// ===============================================================================================================
// FACTIONS (the "no reputation list, so the core never refuses an attack for it" test)
// ===============================================================================================================

inline FactionTemplateEntry const* FactionTemplateWL4(uint32 factionId)
{
    return sFactionTemplateStore.LookupEntry(factionId);
}

inline uint32 FactionOfTemplateWL4(FactionTemplateEntry const* tmpl)
{
    return tmpl ? uint32(tmpl->Faction) : 0;
}

inline FactionEntry const* FactionWL4(uint32 factionId)
{
    return sFactionStore.LookupEntry(factionId);
}

inline bool FactionCanHaveReputationWL4(FactionEntry const* faction)
{
    return faction && faction->CanHaveReputation();
}

// ===============================================================================================================
// QUEST TEMPLATE
// ===============================================================================================================

// The quest's minimum level: Turtle `GetMinLevel()`; ForeverCore keeps the vanilla range in `quest_classic_level`
// (GetClassicMinLevel, 0 = any level).
inline int32 QuestMinLevelWL4(Quest const* quest)
{
    return quest ? quest->GetClassicMinLevel() : 0;
}

// Quest kind: Turtle's QUEST_TYPE_* on GetType(); ForeverCore's quest tag (QuestTagType) on GetQuestTag().
// ForeverCore has no elite quest tag (its classic quest table carries no such kind): a quest is dungeon, raid or pvp by tag,
// never "elite", so the elite gates answer false here and the dungeon/raid gates do the work (LESS: no elite-only refusal).
inline bool QuestIsEliteWL4(Quest const* /*quest*/)
{
    return false;
}

inline bool QuestIsDungeonWL4(Quest const* quest)
{
    return quest && quest->GetQuestTag() && *quest->GetQuestTag() == QuestTagType::Dungeon;
}

inline bool QuestIsRaidWL4(Quest const* quest)
{
    return quest && quest->GetQuestTag() && *quest->GetQuestTag() == QuestTagType::Raid;
}

inline bool QuestIsPvpWL4(Quest const* quest)
{
    return quest && quest->GetQuestTag() && *quest->GetQuestTag() == QuestTagType::Pvp;
}

// The object id and count of an objective slot (Turtle's flat ReqItemId/ReqItemCount arrays; ForeverCore's objective
// rows, where the item objectives are the ones whose Type names an item). 0 when the slot holds no item objective.
inline uint32 QuestReqItemIdWL4(Quest const* quest, uint32 index)
{
    if (!quest)
        return 0;
    QuestObjectives const& objectives = quest->GetObjectives();
    if (index >= objectives.size())
        return 0;
    if (objectives[index].Type != QUEST_OBJECTIVE_ITEM)
        return 0;
    return uint32(std::max(0, objectives[index].ObjectID));
}

inline uint32 QuestReqItemCountWL4(Quest const* quest, uint32 index)
{
    if (!quest)
        return 0;
    QuestObjectives const& objectives = quest->GetObjectives();
    if (index >= objectives.size())
        return 0;
    if (objectives[index].Type != QUEST_OBJECTIVE_ITEM)
        return 0;
    return uint32(std::max(0, objectives[index].Amount));
}

// Does the quest carry a required player condition (Turtle `GetRequiredCondition`)? ForeverCore's classic quests have no
// single condition gate; answering false means CanTakeQuest is tried on the destination's map (LESS: one wasted walk).
inline bool QuestHasRequiredConditionWL4(Quest const* /*quest*/)
{
    return false;
}

// The quest's previous-chain check (Turtle SatisfyQuestPrevChain, ForeverCore SatisfyQuestDependentPreviousQuests).
inline bool PlayerSatisfyQuestPrevChainWL4(Player* player, Quest const* quest)
{
    return player && quest && player->SatisfyQuestDependentPreviousQuests(quest, false);
}

// Does the player already have the quest (Turtle Player::HasQuest, ForeverCore a non-empty quest status)?
inline bool PlayerHasQuestWL4(Player* player, uint32 questId)
{
    return player && player->GetQuestStatus(questId) != QUEST_STATUS_NONE;
}

// ===============================================================================================================
// PLAYER, MAP
// ===============================================================================================================

// The player's homebind (Turtle GetHomeBindLocation, ForeverCore the public m_homebind). decltype(auto) with the parentheses:
// under the shim `WorldLocation` is the bot's legacy struct, so a `WorldLocation const&` return would convert the core member
// to a temporary and return a dangling reference (TbCompat_V.h's HomeBindLocationCompat does the same).
inline decltype(auto) PlayerHomeBindWL4(Player* player)
{
    return (player->m_homebind);
}

// Maximum skill value (Turtle GetSkillMax, ForeverCore GetPureMaxSkillValue).
inline uint16 PlayerSkillMaxWL4(Player* player, SkillType skill)
{
    return player ? player->GetPureMaxSkillValue(skill) : 0;
}

// The skill cap at the player's level (Turtle GetSkillMaxForLevel, ForeverCore GetMaxSkillValueForLevel).
inline uint16 PlayerSkillMaxForLevelWL4(Player* player)
{
    return player ? player->GetMaxSkillValueForLevel() : 0;
}

// Is the map a continent (Turtle Map::IsContinent, ForeverCore "not instanceable").
inline bool MapIsContinentWL4(Map const* map)
{
    return map && !map->Instanceable();
}

// The map template's id (Turtle MapEntry::id, ForeverCore MapEntry::ID).
inline uint32 MapEntryIdWL4(MapEntry const* entry)
{
    return entry ? entry->ID : 0;
}

namespace ai
{
// The creature spawns the explore destinations are built from. ForeverCore: none - the explore purpose is not in the
// reduced set (its points need an explored-area bit this core tracks per area id, and the spawn pair has no guid here).
inline std::vector<CreatureDataPair const*> TravelExploreSpawnsCompat()
{
    return {};
}

// ===============================================================================================================
// TRAVEL DATA SETS (TravelMgr.cpp, TravelValues.cpp). Defined in TbCompat_WL4.cpp; built once, at boot.
// ===============================================================================================================

// entry -> questId -> purpose flag. Built from creature_queststarter/_ender and gameobject_queststarter/_ender, kept only
// for quests with `Expansion = 0` (the retail leftovers on the classic quest table are not destinations). A missing table
// answers no rows: no quest destination, one line, travel keeps its other purposes.
ai::EntryQuestRelationMap const& TravelQuestRelationMapCompat();

// entry -> every spawn position of that entry (creatures positive, game objects negative), area flag resolved once here so
// the destination rules never load terrain on the world thread (issue #684).
ai::EntryGuidps const& TravelEntryGuidpsCompat();
// Drops the boot copy of the spawn index after the destination build; the next ask rebuilds it (nothing on this core asks).
void TravelEntryGuidpsReleaseCompat();

// The builders the two values call (TravelValues.cpp). Turtle's own bodies live in its header; these are the ForeverCore
// equivalents, over the same data the readers above expose.
ai::EntryGuidps TravelEntryGuidpsLegacyCompat();
ai::EntryTravelPurposeMap TravelEntryPurposeMapLegacyCompat();

// entry -> the purposes the template can serve. ForeverCore's set is the reduced one the plan keeps on this core: grind,
// town (GenericRpg), vendor and repair, each gated at pick time by TravelPurposeGateRegisteredCompat; no trainer, AH, mail,
// boss or gather destination until their slices exist.
ai::EntryTravelPurposeMap const& TravelEntryPurposeMapCompat();

// The loader warms two shared values on Turtle (trainer map, mount list); this core's purpose map reads neither.
inline void TravelWarmSharedValuesCompat() {}

// Is a purpose's slice merged? The gate reads the bot's own registry (the generated slice contexts): W2's vendor keys are
// the `sell`/`repair` actions plus the `maintenance` strategy, W3's quest keys the `rpg quest` strategy. A purpose whose
// keys are absent is refused and the bot does LESS (keeps grinding). See policy/travel/TravelPurposeGatePolicy.h.
bool TravelPurposeRegisteredCompat(PlayerbotAI* ai, ai::TravelDestinationPurpose purpose);

// The reduced ForeverCore answers of the four travel values TravelValues.cpp owns (the full rule sets live on the Turtle
// side). Each one is the legacy question asked against the data this core has.
bool TravelPurposeNeedCompat(PlayerbotAI* ai, std::string const& qualifier, ai::TravelProbeCache& activeNodeProbe);
bool TravelNamedPurposeCompat(PlayerbotAI* ai, Player* bot, ChatHelper* chat, std::string const& qualifier,
    ai::TravelProbeCache& activeTrainerProbe);
bool TravelLeaveOutgrownCompat(PlayerbotAI* ai);
bool TravelQuestStageActiveCompat(Player* bot, std::string const& qualifier);

// ===============================================================================================================
// BOOT PIECES OWNED BY OTHER W4 ROUNDS / WAVES. Each answer is "not there", so the caller does LESS.
// ===============================================================================================================

// The travel graph (W4-A1): load the shipped node store and the generated path cache. ForeverCore has no node store yet;
// the graph stays empty and travel picks no target (the missing tables would do the same).
void TravelGraphLoadCompat();

// The run logs the legacy loader opens (writes into the bot's log folder, truncating every boot). W4 does not need them;
// the W1 event log is opened by PlayerbotAIConfig.
inline void TravelOpenRunLogsCompat() {}

// The taxi-node zone cache (W5 flight): flights are refused on this core, so no cache is built.
inline void TravelLoadTaxiZonesCompat(std::unordered_map<uint32, uint32>& zones) { zones.clear(); }

// Fishing locations (W7): the legacy store, loaded (ForeverCore has the shipped
// FISH_LOCATION rows and a travel graph). The rows carry the classic area
// FLAG (Turtle's ExploreFlag, ForeverCore's AreaBit); the store's keys on this
// core are area ids, so the flag is mapped through the area table once. The
// same flag can exist on more than one continent, so the row on the spot's map
// wins, as the legacy flag lookup did.
inline uint32 AreaIdOfStoredFishFlagWL4(int16 flag, uint32 mapId)
{
    if (flag < 0)
        return 0;
    struct FlagIndex
    {
        std::unordered_map<uint32, uint32> byMapAndFlag;
        std::unordered_map<uint16, uint32> byFlag;
    };
    static FlagIndex const index = []()
    {
        FlagIndex out;
        for (AreaTableEntry const* area : sAreaTableStore)
        {
            if (!area || area->AreaBit < 0)
                continue;
            uint16 const bit = uint16(area->AreaBit);
            out.byMapAndFlag.emplace((uint32(area->ContinentID) << 16) | bit, area->ID);
            out.byFlag.emplace(bit, area->ID);
        }
        return out;
    }();
    auto const onMap = index.byMapAndFlag.find((mapId << 16) | uint16(flag));
    if (onMap != index.byMapAndFlag.end())
        return onMap->second;
    auto const anyMap = index.byFlag.find(uint16(flag));
    return anyMap == index.byFlag.end() ? 0u : anyMap->second;
}

// A fish row's stored classic area flag, as the key this core's positions
// carry: Turtle keeps the flag, ForeverCore's WorldPosition carries an area
// id, so the flag is mapped once when the row is loaded.
inline int32 StoredFishAreaKeyWL4(int32 storedFlag, uint32 mapId)
{
    return int32(AreaIdOfStoredFishFlagWL4(int16(storedFlag), mapId));
}

inline bool TravelFishingCompat() { return true; }

// The area's fishing requirement, read like ObjectMgr::GetFishingBaseSkillLevel
// (the row, then its parent areas) but without the core's "is not properly
// defined in skill_fishing_base_level" error line: a sub-area without its own
// row is the normal case here, and the loader asks for every fish spot.
inline int32 FishingBaseSkillQuietWL4(uint32 areaId)
{
    static std::unordered_map<uint32, int32> const base = []()
    {
        std::unordered_map<uint32, int32> out;
        if (QueryResult result = WorldQueryCompat("SELECT `entry`, `skill` FROM `skill_fishing_base_level`"))
        {
            do
            {
                Field* fields = result->Fetch();
                out.emplace(fields[0].GetUInt32(), fields[1].GetInt32());
            } while (result->NextRow());
        }
        return out;
    }();

    uint32 id = areaId;
    for (uint32 depth = 0; id && depth < 8; ++depth)
    {
        auto const it = base.find(id);
        if (it != base.end())
            return it->second;
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(id);
        if (!area)
            break;
        id = area->ParentAreaID;
    }
    return 0;
}

inline int32 FishingBaseSkillWL4(uint32 areaId)
{
    return FishingBaseSkillQuietWL4(areaId);
}

inline uint32 FishingZoneOfWL4(ai::AsyncGuidPosition const& pos)
{
    uint32 const areaId = pos.GetAreaFlag();
    AreaTableEntry const* area = areaId ? sAreaTableStore.LookupEntry(areaId) : nullptr;
    if (area && FishingBaseSkillQuietWL4(area->ID))
        return area->ID;

    uint32 const zoneId = area ? (area->ParentAreaID ? area->ParentAreaID : area->ID) : 0;
    AreaTableEntry const* zone = zoneId ? sAreaTableStore.LookupEntry(zoneId) : nullptr;
    if (zone && FishingBaseSkillQuietWL4(zone->ID))
        return zone->ID;

    return 0;
}

// Terrain cache trimming at the end of the legacy destination load: the destinations round does not load terrain beyond
// the spawn area flags, so there is nothing to trim.
inline void TravelTerrainTrimCompat() {}

// A module world-database read through the S5 guard (a missing table / column answers no rows, one line).
inline QueryResult TravelWorldQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    return WorldQueryCompat(sql, schemaMissing);
}

// The legacy loader asked `SHOW TABLES LIKE` before reading the zone-level table; ForeverCore never sends a statement to a missing
// table (the guard above refuses it and says so), so there is no separate probe: false.
inline bool TravelWorldTableMissingCompat(char const* /*table*/) { return false; }

// Fill the area-level cache for every DBC area at boot, so the runtime GetAreaLevel only reads (bot ticks run on the parallel
// map threads and an unlocked insert into the shared map is a crash class). Turtle keeps its lazy cache as it always was.
inline bool TravelPrecomputeAreaLevelsCompat() { return true; }

// One transaction around the fish-location save (never reached on this core; kept compiling).
// Never reached on this core (the fish loader returns first); the batch form needs no transaction here.
inline void TravelBeginTransactionCompat() {}
inline void TravelCommitTransactionCompat() {}

// ===============================================================================================================
// ROUND A3 (the travel chooser, walker and action): the fields and small services those files read, spelled
// differently on the two cores. Each ForeverCore answer is the same question with the core's own data; where the
// data does not exist it answers "less" (no banker, no focus, no tracked progress).
// ===============================================================================================================

// A creature whose gossip offers the guild bank (Turtle gossip_menu_id plus option_id/npc_option_npcflag; ForeverCore
// a list of menu ids and an option kind).
inline bool GuildBankerGossipWL4(CreatureInfo const* info)
{
    if (!info)
        return false;
    bool sawItems = false;
    for (uint32 menuId : info->GossipMenuIds)
    {
        auto bounds = sObjectMgr.GetGossipMenuItemsMapBounds(menuId);
        for (auto itr = bounds.begin(); itr != bounds.end(); ++itr)
        {
            sawItems = true;
            if (itr->second.OptionNpc == GossipOptionNpc::GuildBanker)
                return true;
        }
    }
    if (!sawItems)
    {
        auto bounds = sObjectMgr.GetGossipMenuItemsMapBounds(0);
        for (auto itr = bounds.begin(); itr != bounds.end(); ++itr)
            if (itr->second.OptionNpc == GossipOptionNpc::GuildBanker)
                return true;
    }
    return false;
}

// The spell focus a game object serves (Turtle spellFocus.focusId, ForeverCore spellFocus.spellFocusType).
inline uint32 SpellFocusIdWL4(GameObjectInfo const* info)
{
    return info && info->type == GAMEOBJECT_TYPE_SPELL_FOCUS ? info->spellFocus.spellFocusType : 0;
}

// A quest log row's state. ForeverCore removes a rewarded quest from the log, so a row is never rewarded there.
inline bool QuestStatusRewardedWL4(QuestStatusData const& status) { return status.Status == QUEST_STATUS_REWARDED; }
inline bool QuestStatusIncompleteWL4(QuestStatusData const& status) { return status.Status == QUEST_STATUS_INCOMPLETE; }
inline bool QuestStatusCompleteWL4(QuestStatusData const& status) { return status.Status == QUEST_STATUS_COMPLETE; }

// The quest's objective slots the stall check walks (Turtle's flat four-slot arrays; ForeverCore's objective list).
inline uint32 QuestObjectiveSlotCountWL4(Quest const* quest)
{
    return quest ? std::min<uint32>(4, uint32(quest->GetObjectives().size())) : 0;
}

inline int32 QuestReqCreatureOrGOIdWL4(Quest const* quest, uint32 index)
{
    if (!quest || index >= quest->GetObjectives().size())
        return 0;
    QuestObjective const& objective = quest->GetObjectives()[index];
    return objective.Type == QUEST_OBJECTIVE_MONSTER || objective.Type == QUEST_OBJECTIVE_GAMEOBJECT ? objective.ObjectID : 0;
}

inline uint32 QuestReqCreatureOrGOCountWL4(Quest const* quest, uint32 index)
{
    if (!quest || index >= quest->GetObjectives().size())
        return 0;
    QuestObjective const& objective = quest->GetObjectives()[index];
    return objective.Type == QUEST_OBJECTIVE_MONSTER || objective.Type == QUEST_OBJECTIVE_GAMEOBJECT ? uint32(std::max(0, objective.Amount)) : 0;
}

// The live counters of one objective slot (Turtle m_creatureOrGOcount/m_itemcount; ForeverCore the core's stored value).
inline void QuestObjectiveCountersWL4(Player* bot, uint32 questId, Quest const* quest, uint32 index, uint32& kill, uint32& item)
{
    kill = 0;
    item = 0;
    if (!bot || !quest || index >= quest->GetObjectives().size())
        return;
    QuestObjective const& objective = quest->GetObjectives()[index];
    int32 const value = std::max(0, bot->GetQuestObjectiveData(questId, objective.ID));
    if (objective.Type == QUEST_OBJECTIVE_ITEM)
        item = uint32(value);
    else if (objective.Type == QUEST_OBJECTIVE_MONSTER || objective.Type == QUEST_OBJECTIVE_GAMEOBJECT)
        kill = uint32(value);
}

// Taxi tables: the node row (Turtle sObjectMgr.GetTaxiNodeEntry, ForeverCore the DB2 store).
inline TaxiNodesEntry const* TaxiNodeLookupWL4(uint32 nodeId)
{
    return sTaxiNodesStore.LookupEntry(nodeId);
}
inline char const* TaxiNodeNameWL4(TaxiNodesEntry const* node)
{
    return node ? node->Name[LOCALE_enUS] : "";
}

// The spline's movement origin (Turtle carries the caller's name; ForeverCore does not keep one, so no leg is
// recognised as the action's own).
inline char const* MoveSplineOriginWL4(Movement::MoveSpline const& /*spline*/) { return nullptr; }
inline int32 MoveSplineTimeElapsedWL4(Movement::MoveSpline const& spline) { return spline.timePassed(); }

// Riding a mesa lift: the legacy transport mark (Turtle SetTransportData + MOVEFLAG_ONTRANSPORT; ForeverCore keeps
// the transport in the movement info and has no on-transport flag).
inline void LiftSetTransportWL4(Player* bot, ObjectGuid guid, uint32 now)
{
    if (!bot)
        return;
    bot->m_movementInfo.transport.guid = guid;
    bot->m_movementInfo.transport.pos.Relocate(0.0f, 0.0f, 0.0f, bot->GetOrientation());
    bot->m_movementInfo.transport.time = now;
}
inline ObjectGuid LiftTransportGuidWL4(Player* bot)
{
    return bot ? bot->m_movementInfo.transport.guid : ObjectGuid();
}
inline void LiftClearTransportWL4(Player* bot)
{
    if (bot)
        bot->m_movementInfo.ResetTransport();
}
}
