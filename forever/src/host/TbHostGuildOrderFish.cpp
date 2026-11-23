#include "host/TbHostGuildOrderFish.h"

#include "host/TbHostGuildOrderFishPure.h"

#include "DatabaseEnv.h"
#include "compat/TbCompat_S5.h"

#include "DB2Stores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"

#include "runtime/GuildOrderFishingPolicy.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guild_order_fish
{

namespace
{

struct FishingSourceState
{
    std::mutex mutex;
    bool loaded = false;
    // Item the loot can yield -> lowest positive skill_fishing_base_level among
    // its source zones (0: no positive requirement).
    std::unordered_map<std::uint32_t, std::uint32_t> minSkillByItem;
};

FishingSourceState& SourceState()
{
    static FishingSourceState state;
    return state;
}

std::uint32_t AreaFishingSkill(std::uint32_t areaId)
{
    if (!areaId)
        return 0;
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    int32_t const required = area ? sObjectMgr->GetFishingBaseSkillLevel(area) : 0;
    return required > 0 ? std::uint32_t(required) : 0u;
}

// Expands a reference_loot_template id into the item ids it can yield,
// following nested references once and caching the answer.
void ExpandReference(std::uint32_t refId, std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& cache,
    std::unordered_map<std::uint32_t, bool>& expanding, std::vector<std::uint32_t>& out)
{
    if (!refId)
        return;

    auto cached = cache.find(refId);
    if (cached != cache.end())
    {
        out.insert(out.end(), cached->second.begin(), cached->second.end());
        return;
    }
    if (expanding[refId])
        return;
    expanding[refId] = true;

    std::vector<std::uint32_t> items;
    std::string const sql = "SELECT `ItemType`, `Item` FROM `reference_loot_template` WHERE `Entry` = " +
        std::to_string(refId);
    if (QueryResult result = WorldQueryCompat(sql.c_str()))
    {
        do
        {
            Field* fields = result->Fetch();
            std::uint32_t const type = fields[0].GetUInt32();
            std::uint32_t const item = fields[1].GetUInt32();
            if (!item)
                continue;
            if (type == 0)
                items.push_back(item);
            else if (type == 1)
            {
                std::vector<std::uint32_t> nested;
                ExpandReference(item, cache, expanding, nested);
                items.insert(items.end(), nested.begin(), nested.end());
            }
        } while (result->NextRow());
    }

    expanding[refId] = false;
    cache.emplace(refId, items);
    out.insert(out.end(), items.begin(), items.end());
}

} // namespace

void LoadFishingSources()
{
    std::unordered_map<std::uint32_t, std::uint32_t> minSkillByItem;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> refCache;
    std::unordered_map<std::uint32_t, bool> refExpanding;

    auto addItem = [&minSkillByItem](std::uint32_t itemId, std::uint32_t skill)
    {
        if (!itemId)
            return;
        auto [it, inserted] = minSkillByItem.emplace(itemId, skill);
        if (!inserted && skill < it->second)
            it->second = skill;
    };

    auto addReference = [&](std::uint32_t refId, std::uint32_t skill)
    {
        std::vector<std::uint32_t> items;
        ExpandReference(refId, refCache, refExpanding, items);
        for (std::uint32_t itemId : items)
            addItem(itemId, skill);
    };

    // Open water: the core rolls fishing_loot_template by area id, and the
    // area's skill_fishing_base_level is the requirement.
    if (QueryResult result = WorldQueryCompat("SELECT `Entry`, `ItemType`, `Item` FROM `fishing_loot_template`"))
    {
        do
        {
            Field* fields = result->Fetch();
            std::uint32_t const areaId = fields[0].GetUInt32();
            std::uint32_t const type = fields[1].GetUInt32();
            std::uint32_t const itemId = fields[2].GetUInt32();
            std::uint32_t const skill = AreaFishingSkill(areaId);
            if (type == 0)
                addItem(itemId, skill);
            else if (type == 1)
                addReference(itemId, skill);
        } while (result->NextRow());
    }

    // Fishing holes: gameobject_template type 25 names the loot id in Data1;
    // an unlocked pool has no positive requirement of its own.
    std::vector<std::uint32_t> holeLootIds;
    if (QueryResult result = WorldQueryCompat("SELECT `entry`, `Data1` FROM `gameobject_template` WHERE `type` = 25"))
    {
        do
        {
            std::uint32_t const lootId = result->Fetch()[1].GetUInt32();
            if (lootId)
                holeLootIds.push_back(lootId);
        } while (result->NextRow());
    }

    for (std::uint32_t lootId : holeLootIds)
    {
        std::string const sql = "SELECT `ItemType`, `Item` FROM `gameobject_loot_template` WHERE `Entry` = " +
            std::to_string(lootId);
        if (QueryResult result = WorldQueryCompat(sql.c_str()))
        {
            do
            {
                Field* fields = result->Fetch();
                std::uint32_t const type = fields[0].GetUInt32();
                std::uint32_t const itemId = fields[1].GetUInt32();
                if (type == 0)
                    addItem(itemId, 0);
                else if (type == 1)
                    addReference(itemId, 0);
            } while (result->NextRow());
        }
    }

    FishingSourceState& state = SourceState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.minSkillByItem = std::move(minSkillByItem);
    state.loaded = true;
    TC_LOG_INFO("tortoisebots", "[W9] fishing sources: {} items", uint32_t(state.minSkillByItem.size()));
}

bool IsFishingSource(std::uint32_t itemId)
{
    if (!itemId)
        return false;
    FishingSourceState& state = SourceState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.minSkillByItem.find(itemId) != state.minSkillByItem.end();
}

std::uint32_t MinFishingZoneSkill(std::uint32_t itemId)
{
    if (!itemId)
        return 0;
    FishingSourceState& state = SourceState();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto const it = state.minSkillByItem.find(itemId);
    return it == state.minSkillByItem.end() ? 0u : it->second;
}

bool HostFishingAvailable()
{
    // No fishing on this build: no fishing action is registered, and no host
    // path casts the fishing spell, uses the bobber or takes the catch.
    return false;
}

std::optional<TortoiseBots::GuildOrders::OrderReason> ResolveFarmRefusal(Player* bot, std::uint32_t itemId)
{
    if (!bot || !itemId)
        return std::nullopt;

    std::uint32_t minSkill = 0;
    {
        FishingSourceState& state = SourceState();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto const it = state.minSkillByItem.find(itemId);
        if (it == state.minSkillByItem.end())
            return std::nullopt;
        minSkill = it->second;
    }

    bool const hasSkill = bot->HasSkill(SKILL_FISHING);
    std::uint32_t const botSkill = hasSkill ? bot->GetSkillValue(SKILL_FISHING) : 0;
    bool const reachable = TortoiseBots::GuildOrders::FishingZoneReachable(int32_t(minSkill), botSkill);

    FishFarmVerdict const verdict = DecideFishFarmVerdict(true, hasSkill, reachable, HostFishingAvailable());
    TortoiseBots::GuildOrders::OrderReason const reason = FishFarmReason(verdict);
    if (reason == TortoiseBots::GuildOrders::OrderReason::None)
        return std::nullopt;
    return reason;
}

} // namespace guild_order_fish
} // namespace tbhost
