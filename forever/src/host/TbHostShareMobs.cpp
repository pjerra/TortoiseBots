// #24 host service: see TbHostShareMobs.h. World thread only, memory only, no database read: the rows are built from the online
// players the core already holds, and the loot question is the core's own LootStore answer. Slice OFF: not compiled.
#include "host/TbHostShareMobs.h"

#include "compat/botpch_forever.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Group.h"
#include "Loot/LootMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldSession.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_Unit.h"
#include "host/TbHostGuildOrders.h"
#include "PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"

namespace tbhost
{
namespace sharemobs
{
namespace
{
using TortoiseBots::world::MobPoint;
using TortoiseBots::world::MobVerdict;
using TortoiseBots::world::PlayerRow;
using TortoiseBots::world::ShareMobsConfig;

constexpr std::uint32_t RebuildIntervalMs = 1000;

// One map's real players. `guids` runs parallel to `rows`: the Player to ask about a quest drop when a loot question comes up
// (resolved then, never held: a player who logs out mid-second must not leave a stale pointer in the table).
struct MapTable
{
    std::vector<PlayerRow> rows;
    std::vector<ObjectGuid> guids;
};

std::unordered_map<std::uint32_t, MapTable>& Tables()
{
    static std::unordered_map<std::uint32_t, MapTable> tables;
    return tables;
}

// (player raw guid -> loot id -> does this mob's loot hold a quest drop for that player), cleared with the table. The answer is
// a fact of the loot template and the player's open quests, so one answer lasts for a rebuild.
std::unordered_map<std::uint64_t, std::unordered_map<std::uint32_t, bool>>& LootMemo()
{
    static std::unordered_map<std::uint64_t, std::unordered_map<std::uint32_t, bool>> memo;
    return memo;
}

std::uint32_t g_elapsedMs = 0;

ShareMobsConfig CurrentConfig()
{
    return TortoiseBots::BotMgr::Instance().GetShareMobsConfig();
}

// A party with a real player in it is the bot's own play: the same rule the open-world PvP role uses
// (host/TbHostWorldPvp.cpp, TB AiFactory.cpp:1195-1254).
bool GroupedWithRealPlayer(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (member && member != bot && member->IsInWorld() && HasNetworkTransportCompat(member->GetSession()))
            return true;
    }
    return false;
}

// The creature entries of the player's incomplete MONSTER objectives and the item ids of his incomplete ITEM objectives. Only
// what a kill or a drop can serve is kept: no gameobject, talk-to or currency objectives, and nothing already complete. The
// policy's AddQuestObjective holds that rule; this loop only maps the core's objective type onto it and hands it the core's
// own completion answer.
void CollectQuestNeeds(Player* player, PlayerRow& row)
{
    for (std::uint16_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        std::uint32_t const questId = player->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            TortoiseBots::world::QuestNeedKind kind = TortoiseBots::world::QuestNeedKind::None;
            if (objective.Type == QUEST_OBJECTIVE_MONSTER)
                kind = TortoiseBots::world::QuestNeedKind::Monster;
            else if (objective.Type == QUEST_OBJECTIVE_ITEM)
                kind = TortoiseBots::world::QuestNeedKind::Item;
            TortoiseBots::world::AddQuestObjective(row, kind, std::uint32_t(objective.ObjectID), objective.Amount,
                player->IsQuestObjectiveComplete(questId, objective.ID));
        }
    }
    auto tidy = [](std::vector<std::uint32_t>& ids)
    {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    };
    tidy(row.monsterEntries);
    tidy(row.itemIds);
}

void Rebuild()
{
    Tables().clear();
    LootMemo().clear();

    TortoiseBots::BotMgr& manager = TortoiseBots::BotMgr::Instance();
    for (auto const& entry : ObjectAccessor::GetPlayers())
    {
        Player* player = entry.second;
        if (!player || !player->IsInWorld() || !player->GetSession())
            continue;
        if (!HasNetworkTransportCompat(player->GetSession()))
            continue;
        if (manager.IsKnownBot(player->GetGUID()))
            continue;
        Map* map = player->GetMap();
        if (!map || map->Instanceable())
            continue;

        PlayerRow row;
        row.guid = player->GetGUID().GetCounter();
        row.mapId = player->GetMapId();
        row.instanceId = player->GetInstanceId();
        row.x = player->GetPositionX();
        row.y = player->GetPositionY();
        row.z = player->GetPositionZ();
        row.groupGuid = player->GetGroup() ? player->GetGroup()->GetGUID().GetCounter() : 0;
        CollectQuestNeeds(player, row);

        MapTable& table = Tables()[row.mapId];
        table.guids.push_back(player->GetGUID());
        table.rows.push_back(std::move(row));
    }
}

// Does this mob's loot hold a quest drop for the row's player? Asked only inside the far ring (and only when the player's
// MONSTER list does not already name the entry), answered once per row per loot id per rebuild.
bool LootNeeded(PlayerRow const& row, ObjectGuid const& playerGuid, std::uint32_t lootId)
{
    if (!lootId)
        return false;
    auto& memo = LootMemo()[row.guid];
    auto const found = memo.find(lootId);
    if (found != memo.end())
        return found->second;
    bool needed = false;
    if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
        needed = LootTemplates_Creature.HaveQuestLootForPlayer(lootId, player);
    memo.emplace(lootId, needed);
    return needed;
}

TortoiseBots::world::BotFacts Facts(Player* bot, ShareMobsConfig const& config)
{
    TortoiseBots::world::BotFacts facts;
    facts.switchOn = config.enabled;
    TortoiseBots::BotMgr& manager = TortoiseBots::BotMgr::Instance();
    ObjectGuid const guid = bot->GetGUID();
    facts.poolBot = manager.IsPoolBot(guid);
    facts.freeBot = manager.IsFreeBot(guid);
    facts.owned = manager.IsOwnedBot(guid);
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    facts.hasRealPlayerMaster = ai && ai->HasRealPlayerMaster();
    facts.openGuildOrder = tbhost::guild_orders::HasOpenOrder(guid.GetCounter());
    facts.groupHasRealPlayer = GroupedWithRealPlayer(bot);
    Map* map = bot->GetMap();
    facts.mapInstanceable = map && map->Instanceable();
    return facts;
}
} // namespace

void Tick(std::uint32_t diff)
{
    if (!CurrentConfig().enabled)
    {
        if (!Tables().empty() || !LootMemo().empty())
        {
            Tables().clear();
            LootMemo().clear();
        }
        g_elapsedMs = 0;
        return;
    }

    g_elapsedMs += diff;
    if (g_elapsedMs < RebuildIntervalMs)
        return;
    g_elapsedMs = 0;
    Rebuild();
}

MobVerdict Judge(Player* bot, Unit* unit)
{
    if (!bot || !unit)
        return MobVerdict::None;

    ShareMobsConfig const config = CurrentConfig();
    if (!config.enabled)
        return MobVerdict::None;

    // The two flags are the cheapest way to leave: only a pool bot still playing free is asked at all, and every other fact
    // (owner, master, guild order, party, map) is read only for those.
    TortoiseBots::BotMgr& manager = TortoiseBots::BotMgr::Instance();
    ObjectGuid const guid = bot->GetGUID();
    if (!manager.IsPoolBot(guid) || !manager.IsFreeBot(guid))
        return MobVerdict::None;

    auto const found = Tables().find(bot->GetMapId());
    if (found == Tables().end())
        return MobVerdict::None;

    Creature* creature = unit->ToCreature();
    if (!creature)
        return MobVerdict::None;

    MobPoint mob;
    mob.mapId = unit->GetMapId();
    mob.instanceId = unit->GetInstanceId();
    mob.x = unit->GetPositionX();
    mob.y = unit->GetPositionY();
    mob.z = unit->GetPositionZ();
    mob.entry = creature->GetEntry();

    std::uint64_t const botGroupGuid = bot->GetGroup() ? bot->GetGroup()->GetGUID().GetCounter() : 0;
    std::uint32_t const lootId = creature->GetLootId();

    MapTable const& table = found->second;
    // Every other fact (owner, master, guild order, party, map) is a group walk and a guild-order lookup: it is read once per
    // bot per pass, and only when a real player is actually in range of this mob.
    bool haveFacts = false;
    TortoiseBots::world::BotFacts facts;
    for (std::size_t index = 0; index < table.rows.size(); ++index)
    {
        PlayerRow const& row = table.rows[index];
        float const distance = TortoiseBots::world::ShareMobsDistanceYd(row, mob);
        if (distance > config.farYd)
            continue;
        if (!haveFacts)
        {
            facts = Facts(bot, config);
            haveFacts = true;
            if (!TortoiseBots::world::ShareMobsApplies(facts))
                return MobVerdict::None;
        }
        bool const lootNeeded = distance > config.nearYd && !row.needsMonster(mob.entry) &&
            LootNeeded(row, table.guids[index], lootId);
        MobVerdict const verdict = TortoiseBots::world::JudgeMob(config, row, botGroupGuid, mob, lootNeeded);
        if (verdict != MobVerdict::None)
            return verdict;
    }
    return MobVerdict::None;
}

} // namespace sharemobs
} // namespace tbhost
