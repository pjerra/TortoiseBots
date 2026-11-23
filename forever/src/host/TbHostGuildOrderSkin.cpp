#include "host/TbHostGuildOrderSkin.h"
#include "host/TbHostGuildOrders.h"

#include "playerbot/GuildOrderSources.h"

#include "DatabaseEnv.h"
#include "compat/TbCompat_S5.h"

#include "CreatureData.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"

#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderSourcesPolicy.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tbhost
{
namespace guild_order_skin
{

namespace
{

struct SkinSource
{
    std::uint32_t entry = 0;
    std::uint32_t reqSkill = 0;
};

struct SkinSourceState
{
    std::mutex mutex;
    bool loaded = false;
    // Item the skin loot can yield -> the creature entries whose corpse may yield it, with the
    // Skinning skill their level asks for.
    std::unordered_map<std::uint32_t, std::vector<SkinSource>> byItem;
};

SkinSourceState& SourceState()
{
    static SkinSourceState state;
    return state;
}

// The items one reference template can yield, one level deep: the world resolves a single
// indirection, and a nested reference is not promised by a farm plan either.
void CollectReferenceItems(std::uint32_t lootId, std::uint32_t referenceId,
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>& itemsByLootId)
{
    if (!referenceId)
        return;
    std::string const sql = "SELECT `ItemType`, `Item`, `QuestRequired` FROM `reference_loot_template` WHERE `Entry` = " +
        std::to_string(referenceId);
    if (QueryResult result = WorldQueryCompat(sql.c_str()))
    {
        do
        {
            Field* fields = result->Fetch();
            if (fields[0].GetUInt32() != 0)
                continue;
            std::uint32_t const itemId = fields[1].GetUInt32();
            if (itemId && !fields[2].GetBool())
                itemsByLootId[lootId].insert(itemId);
        } while (result->NextRow());
    }
}

} // namespace

void LoadSkinSources()
{
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> itemsByLootId;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> references; // (skin loot id, reference id)
    if (QueryResult result = WorldQueryCompat("SELECT `Entry`, `ItemType`, `Item`, `QuestRequired` FROM `skinning_loot_template`"))
    {
        do
        {
            Field* fields = result->Fetch();
            std::uint32_t const lootId = fields[0].GetUInt32();
            std::uint32_t const type = fields[1].GetUInt32();
            std::uint32_t const itemId = fields[2].GetUInt32();
            if (!lootId || !itemId)
                continue;
            if (type == 0)
            {
                if (!fields[3].GetBool())
                    itemsByLootId[lootId].insert(itemId);
            }
            else if (type == 1)
                references.emplace_back(lootId, itemId);
        } while (result->NextRow());
    }
    for (std::pair<std::uint32_t, std::uint32_t> const& reference : references)
        CollectReferenceItems(reference.first, reference.second, itemsByLootId);

    std::unordered_map<std::uint32_t, std::vector<SkinSource>> byItem;
    std::unordered_set<std::uint32_t> entries;
    if (QueryResult result = WorldQueryCompat(
            "SELECT `Entry`, `SkinLootID` FROM `creature_template_difficulty` WHERE `DifficultyID` = 0 AND `SkinLootID` <> 0"))
    {
        do
        {
            Field* fields = result->Fetch();
            std::uint32_t const entry = fields[0].GetUInt32();
            std::uint32_t const lootId = fields[1].GetUInt32();
            auto const items = itemsByLootId.find(lootId);
            if (items == itemsByLootId.end() || items->second.empty())
                continue;

            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry);
            if (!info || TortoiseBots::GuildOrders::CreatureRankExcludedFromSoloFarm(std::uint32_t(info->Classification)))
                continue;

            // ForeverCore keeps no per-template level (Turtle level_max); the vanilla levels are
            // the creature_classic_level pair the travel planner also reads. No row = no planned
            // source, so the gate does not promise a mob the plan cannot use.
            std::pair<std::uint8_t, std::uint8_t> const* level = sObjectMgr->GetCreatureClassicLevel(entry);
            if (!level || !level->second)
                continue;
            std::uint32_t const reqSkill = TortoiseBots::GuildOrders::SkinningSkillForMobLevel(level->second);
            for (std::uint32_t itemId : items->second)
                byItem[itemId].push_back(SkinSource{ entry, reqSkill });
            entries.insert(entry);
        } while (result->NextRow());
    }

    SkinSourceState& state = SourceState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.byItem = std::move(byItem);
    state.loaded = true;
    TC_LOG_INFO("tortoisebots", "[W7] skin sources: {} items, {} creature entries",
        std::uint32_t(state.byItem.size()), std::uint32_t(entries.size()));
}

bool SkinMobFor(Player* bot, std::uint32_t creatureEntry)
{
    if (!bot || !creatureEntry)
        return false;
    if (!bot->HasSkill(SKILL_SKINNING))
        return false;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> const record =
        tbhost::guild_orders::FindOrder(std::uint32_t(bot->GetGUID().GetCounter()));
    if (!record || record->type != TortoiseBots::GuildOrders::OrderType::Farm ||
        record->state != TortoiseBots::GuildOrders::OrderState::Working || !record->itemId)
        return false;

    std::uint32_t const skill = bot->GetSkillValue(SKILL_SKINNING);
    SkinSourceState& state = SourceState();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto const sources = state.byItem.find(record->itemId);
    if (sources == state.byItem.end())
        return false;
    for (SkinSource const& source : sources->second)
        if (source.entry == creatureEntry && skill >= source.reqSkill)
            return true;
    return false;
}

} // namespace guild_order_skin
} // namespace tbhost

namespace ai
{
bool GuildOrderSkinMobFor(Player* bot, uint32_t creatureEntry)
{
    return tbhost::guild_order_skin::SkinMobFor(bot, creatureEntry);
}
} // namespace ai
