#include "host/gear/TbGearSourceTier.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "ItemDefines.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

// The core keeps a LootTemplate's rows private (the core's LootMgr.h), so the module reads the drop maps through its
// guarded copy (compat/TbCompat_V2.h: tbs9::LootTableOf, one whole-table read per table through WorldQueryCompat).
#include "compat/botpch_forever.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/policy/raid/RaidQuestChainPolicy.h"
#include "runtime/policy/provision/GearBlacklistPolicy.h"
#include "runtime/policy/provision/GearSourceTierPolicy.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace gear
{
namespace
{

struct GearSourceRecord
{
    uint8 tier = GEAR_SOURCE_TIER_RAID;
    bool reputationGated = false;
    bool pvp = false;
    bool retired = false;
    bool cosmeticSlot = false;
    uint32 itemLevel = 1;
    uint32 quality = 0;
};

struct GearSourceIndex
{
    bool initialized = false;
    std::unordered_map<uint32, GearSourceRecord> items;
};

GearSourceIndex& Index()
{
    static GearSourceIndex s;
    return s;
}

// item id -> lowest source tier over every known source.
using SourceTierMap = std::unordered_map<uint32, uint8>;

void NoteSource(SourceTierMap& tiers, uint32 itemId, uint8 tier)
{
    if (!itemId)
        return;
    auto found = tiers.find(itemId);
    if (found == tiers.end())
        tiers.emplace(itemId, tier);
    else if (tier < found->second)
        found->second = tier;
}

// TB's tier meaning: raid maps from the DBC, the end-game five-mans of this data set as dungeon tier, everything else
// base. 800 in TB's list is a Turtle map; the Classic end-game dungeons are 229 (Blackrock Spire, LBRS and UBRS), 289
// (Scholomance) and 329 (Stratholme). Dire Maul and Blackrock Depths stay base, as they do in TB.
uint8 TierForMap(uint32 mapId)
{
    if (MapEntry const* map = sMapStore.LookupEntry(mapId))
        if (map->IsRaid())
            return GEAR_SOURCE_TIER_RAID;
    switch (mapId)
    {
        case 229:
        case 289:
        case 329:
            return GEAR_SOURCE_TIER_DUNGEON;
        default:
            return GEAR_SOURCE_TIER_BASE;
    }
}

using SpawnTierMap = std::unordered_map<uint32, uint8>;

template <typename SpawnContainer>
void CollectSpawnTiers(SpawnContainer const& spawns, SpawnTierMap& out)
{
    for (auto const& entry : spawns)
    {
        SpawnData const& spawn = entry.second;
        uint8 const tier = TierForMap(spawn.mapId);
        auto found = out.find(spawn.id);
        if (found == out.end())
            out.emplace(spawn.id, tier);
        else if (tier < found->second)
            found->second = tier;
    }
}

uint32 ReferenceLootId(tbs9::LootTableRow const& row)
{
    return row.item.mincountOrRef < 0 ? uint32(-int64(row.item.mincountOrRef)) : 0;
}

// reference_loot_template nests several tables deep (a world drop points at a tier table that points at an armour
// table), so one level of expansion loses every item below the second. `visited` holds the reference tables of this
// traversal: a table's item set does not depend on the path that reached it, and the depth cap ends a malformed cycle.
inline constexpr uint32 kLootReferenceMaxDepth = 8;

void CollectLootTemplateItems(tbs9::LootTableCopy const& copy, tbs9::LootTableCopy const& references, uint32 lootId,
    uint32 depth, std::unordered_set<uint32>& visited, std::vector<uint32>& out)
{
    if (!lootId || depth > kLootReferenceMaxDepth)
        return;
    auto rows = copy.byEntry.find(lootId);
    if (rows == copy.byEntry.end())
        return;

    for (tbs9::LootTableRow const& row : rows->second)
    {
        if (uint32 const referenceId = ReferenceLootId(row))
        {
            if (visited.insert(referenceId).second)
                CollectLootTemplateItems(references, references, referenceId, depth + 1, visited, out);
            continue;
        }
        out.push_back(row.item.itemid);
    }
}

// Every real item one loot template can yield: ungrouped and grouped rows, and nested referenced templates. A
// reference row's `itemid` is the child table id, never an item. `tiered` is false for templates with no spawn on the
// map (a summoned or scripted entry cannot attest a map tier; its items stay a known source at base).
void MarkLootTemplate(tbs9::LootTableCopy const& copy, tbs9::LootTableCopy const& references, uint32 lootId,
    uint8 tier, bool tiered, SourceTierMap& sourceTier, SourceTierMap& lootTier)
{
    if (!lootId)
        return;

    std::vector<uint32> items;
    std::unordered_set<uint32> visited;
    CollectLootTemplateItems(copy, references, lootId, 0, visited, items);
    for (uint32 itemId : items)
    {
        NoteSource(sourceTier, itemId, tier);
        if (tiered)
            NoteSource(lootTier, itemId, tier);
    }
}

// Containers (lockboxes, gift boxes) and open-water fishing: ordinary base sources, never a map tier.
void MarkKnownLootItems(tbs9::LootTableCopy const& copy, tbs9::LootTableCopy const& references, SourceTierMap& sourceTier)
{
    std::vector<uint32> items;
    for (auto const& entry : copy.byEntry)
    {
        std::unordered_set<uint32> visited;
        items.clear();
        CollectLootTemplateItems(copy, references, entry.first, 0, visited, items);
        for (uint32 itemId : items)
            NoteSource(sourceTier, itemId, GEAR_SOURCE_TIER_BASE);
    }
}

// Every item a spell creates, directly or through the spell it teaches (a trainer spell or a recipe's learn spell).
void CollectCreatedItems(uint32 spellId, std::vector<uint32>& out)
{
    if (!spellId)
        return;
    SpellInfo const* spell = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!spell)
        return;

    auto add = [&out](uint32 itemId) { if (itemId) out.push_back(itemId); };
    for (SpellEffectInfo const& effect : spell->GetEffects())
    {
        if (effect.Effect == SPELL_EFFECT_CREATE_ITEM)
            add(effect.ItemType);
        else if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && effect.TriggerSpell)
        {
            SpellInfo const* taught = sSpellMgr.GetSpellInfo(effect.TriggerSpell, DIFFICULTY_NONE);
            if (!taught)
                continue;
            for (SpellEffectInfo const& taughtEffect : taught->GetEffects())
                if (taughtEffect.Effect == SPELL_EFFECT_CREATE_ITEM)
                    add(taughtEffect.ItemType);
        }
    }
}

void CollectRecipeProducts(ItemTemplate const& recipe, std::vector<uint32>& out)
{
    for (ItemEffectEntry const* effect : recipe.Effects)
    {
        if (!effect || effect->SpellID <= 0)
            continue;
        // A recipe is used (0) or learned (6); the item's spell is the learn spell that triggers the craft spell.
        if (effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE && effect->TriggerType != ITEM_SPELLTRIGGER_ON_LEARN)
            continue;
        CollectCreatedItems(uint32(effect->SpellID), out);
    }
}

uint8 LootTierOf(SourceTierMap const& lootTier, uint32 itemId)
{
    auto found = lootTier.find(itemId);
    return found == lootTier.end() ? GEAR_SOURCE_TIER_BASE : found->second;
}

struct BuildState
{
    SourceTierMap sourceTier;    // every known source, lowest tier wins
    SourceTierMap lootTier;      // corpse and chest loot only (TB's tier index, also the raid-exclusivity test)
    std::unordered_set<uint32> vendored;
    SourceTierMap recipeTier;    // products of obtainable recipes, lowest recipe tier wins
    std::unordered_set<uint32> repGatedProducts;
    std::unordered_set<uint32> repGatedQuestItems;
    std::unordered_map<uint32, std::vector<uint32>> questRewards;
    QuestPredecessors predecessors;
    std::unordered_map<uint32, bool> raidQuestMemo;
    std::unordered_map<uint32, bool> raidExclusiveMemo;
    std::unordered_set<uint32> pvpSets;
};

// A quest completes on a raid map: ZoneOrSort > 0 is an area whose map is the quest's map (a negative value is a
// QuestSort id, not a map).
bool CompletesOnRaidMap(Quest const* quest)
{
    int32 const zoneOrSort = quest->GetZoneOrSort();
    if (zoneOrSort <= 0)
        return false;
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(uint32(zoneOrSort));
    if (!area)
        return false;
    MapEntry const* map = sMapStore.LookupEntry(area->ContinentID);
    return map && map->IsRaid();
}

bool RaidExclusiveItem(BuildState& state, uint32 itemId);

// TB's direct raid quest: raid quest type, raid-scale group, raid map completion, or started by raid-exclusive loot.
bool DirectRaidQuest(BuildState& state, Quest const* quest)
{
    if (!quest)
        return false;
    if (quest->GetQuestInfoID() == QUEST_INFO_RAID)
        return true;
    if (quest->GetSuggestedPlayers() > 5)
        return true;
    if (CompletesOnRaidMap(quest))
        return true;
    return RaidExclusiveItem(state, quest->GetSrcItemId());
}

// A raid status that spreads down a chain (policy: RaidQuestChainPolicy.h).
bool RaidChainSource(BuildState& state, Quest const* quest)
{
    if (!quest)
        return false;
    return TaintsChain(CompletesOnRaidMap(quest), RaidExclusiveItem(state, quest->GetSrcItemId()),
        quest->GetQuestInfoID() == QUEST_INFO_RAID, quest->GetZoneOrSort());
}

// An item whose only source is raid loot: not vendored, not craftable, and every quest rewarding it is a direct raid
// quest. Memoized with a false cycle guard, as TB does, so a quest started by an item it rewards terminates.
bool RaidExclusiveItem(BuildState& state, uint32 itemId)
{
    if (!itemId)
        return false;
    auto memo = state.raidExclusiveMemo.find(itemId);
    if (memo != state.raidExclusiveMemo.end())
        return memo->second;
    state.raidExclusiveMemo.emplace(itemId, false);

    bool exclusive = false;
    auto loot = state.lootTier.find(itemId);
    if (loot != state.lootTier.end() && loot->second == GEAR_SOURCE_TIER_RAID &&
        !state.recipeTier.contains(itemId) && !state.vendored.contains(itemId))
    {
        exclusive = true;
        auto rewards = state.questRewards.find(itemId);
        if (rewards != state.questRewards.end())
            for (uint32 questId : rewards->second)
                if (!DirectRaidQuest(state, sObjectMgr.GetQuestTemplate(questId)))
                {
                    exclusive = false;
                    break;
                }
    }

    state.raidExclusiveMemo[itemId] = exclusive;
    return exclusive;
}

bool IsRaidQuest(BuildState& state, uint32 questId)
{
    auto memo = state.raidQuestMemo.find(questId);
    if (memo != state.raidQuestMemo.end())
        return memo->second;
    if (!sObjectMgr.GetQuestTemplate(questId))
    {
        state.raidQuestMemo.emplace(questId, false);
        return false;
    }
    bool const raid = ReachesRaidQuest(questId,
        [&state](uint32 id) { return DirectRaidQuest(state, sObjectMgr.GetQuestTemplate(id)); },
        [&state](uint32 id) { return RaidChainSource(state, sObjectMgr.GetQuestTemplate(id)); },
        state.predecessors);
    state.raidQuestMemo.emplace(questId, raid);
    return raid;
}

void BuildIndex()
{
    auto const started = std::chrono::steady_clock::now();
    GearSourceIndex& index = Index();
    index.items.clear();

    BuildState state;

    SpawnTierMap creatureSpawns;
    SpawnTierMap gameObjectSpawns;
    CollectSpawnTiers(sObjectMgr.GetAllCreatureData(), creatureSpawns);
    CollectSpawnTiers(sObjectMgr.GetAllGameObjectData(), gameObjectSpawns);

    tbs9::LootTableCopy const& references = tbs9::LootTableOf("reference_loot_template");
    tbs9::LootTableCopy const& creatureLoot = tbs9::LootTableOf("creature_loot_template");
    tbs9::LootTableCopy const& gameObjectLoot = tbs9::LootTableOf("gameobject_loot_template");
    tbs9::LootTableCopy const& itemLoot = tbs9::LootTableOf("item_loot_template");
    tbs9::LootTableCopy const& fishingLoot = tbs9::LootTableOf("fishing_loot_template");

    for (auto const& entry : sObjectMgr.GetCreatureTemplates())
    {
        uint32 const creatureEntry = entry.first;
        CreatureTemplate const& info = entry.second;

        // A vendor source is independent of the creature's loot difficulty row. Only an ordinary item sold for gold
        // counts: a currency/mark vendor row, an extended-cost (PvP/raid token) row and a condition-gated row are not
        // ordinary gear, and marking them base would undo the raid/PvP classification.
        if (VendorItemData const* vendor = sObjectMgr.GetNpcVendorItemList(creatureEntry))
            for (uint32 slot = 0; slot < vendor->GetItemCount(); ++slot)
                if (VendorItem const* vendorItem = vendor->GetItem(slot))
                    if (uint32 const itemId = vendorItem->item)
                        if (vendorItem->Type == ITEM_VENDOR_TYPE_ITEM && vendorItem->ExtendedCost == 0 &&
                            vendorItem->PlayerConditionId == 0)
                        {
                            NoteSource(state.sourceTier, itemId, GEAR_SOURCE_TIER_BASE);
                            state.vendored.insert(itemId);
                        }

        CreatureDifficulty const* difficulty = info.GetDifficulty(DIFFICULTY_NONE);
        if (!difficulty)
            continue;

        auto spawn = creatureSpawns.find(creatureEntry);
        bool const spawned = spawn != creatureSpawns.end();
        uint8 tier = spawned ? spawn->second : GEAR_SOURCE_TIER_BASE;
        if (spawned && (difficulty->TypeFlags & CREATURE_TYPE_FLAG_BOSS_MOB))
            tier = GEAR_SOURCE_TIER_RAID;
        MarkLootTemplate(creatureLoot, references, difficulty->LootID, tier, spawned, state.sourceTier, state.lootTier);
    }

    for (auto const& entry : sObjectMgr.GetGameObjectTemplates())
    {
        uint32 const gameObjectEntry = entry.first;
        GameObjectTemplate const& info = entry.second;
        uint32 const lootId = info.GetLootId();
        if (!lootId)
            continue;
        auto spawn = gameObjectSpawns.find(gameObjectEntry);
        bool const spawned = spawn != gameObjectSpawns.end();
        uint8 const tier = spawned ? spawn->second : GEAR_SOURCE_TIER_BASE;
        MarkLootTemplate(gameObjectLoot, references, lootId, tier, spawned, state.sourceTier, state.lootTier);
    }

    MarkKnownLootItems(itemLoot, references, state.sourceTier);
    MarkKnownLootItems(fishingLoot, references, state.sourceTier);

    for (auto const& entry : sObjectMgr.GetQuestTemplates())
    {
        Quest const* quest = entry.second.get();
        if (!quest)
            continue;
        RecordQuestChainLinks(state.predecessors, quest->GetQuestId(), quest->GetPrevQuestId(),
            int32(quest->GetNextQuestId()), quest->GetNextQuestInChain());
        for (uint32 i = 0; i < quest->GetRewItemsCount(); ++i)
            if (uint32 const itemId = quest->RewardItemId[i])
                state.questRewards[itemId].push_back(quest->GetQuestId());
        for (uint32 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
            if (quest->RewardChoiceItemType[i] == LootItemType::Item)
                if (uint32 const itemId = quest->RewardChoiceItemId[i])
                    state.questRewards[itemId].push_back(quest->GetQuestId());
    }

    // One pass over the item store for the two item-row indexes: recipes and PvP sets.
    for (auto const& entry : sObjectMgr.GetItemTemplateStore())
    {
        uint32 const itemId = entry.first;
        ItemTemplate const& proto = entry.second;

        // Only the entry pieces normally carry the flag, so a set with any flagged member marks all its siblings.
        if (proto.GetItemSet() && proto.HasFlag(ITEM_FLAG_NO_DISENCHANT))
            state.pvpSets.insert(proto.GetItemSet());

        // A recipe product inherits its recipe's source. A recipe nobody can obtain (custom or cut row) must not vouch
        // for its product, so it needs a known source here; trainer-taught crafts need no recipe item and are handled below.
        if (proto.GetClass() != ITEM_CLASS_RECIPE)
            continue;
        if (!state.sourceTier.contains(itemId) && !state.questRewards.contains(itemId))
            continue;

        bool const repGated = proto.GetRequiredReputationFaction() != 0 || proto.GetRequiredReputationRank() != 0;
        uint8 const recipeSourceTier = repGated ? GEAR_SOURCE_TIER_RAID : LootTierOf(state.lootTier, itemId);
        std::vector<uint32> products;
        CollectRecipeProducts(proto, products);
        for (uint32 product : products)
        {
            NoteSource(state.sourceTier, product, recipeSourceTier);
            NoteSource(state.recipeTier, product, recipeSourceTier);
            if (repGated)
                state.repGatedProducts.insert(product);
        }
    }

    for (auto const& entry : state.questRewards)
    {
        uint32 const itemId = entry.first;
        bool allRaid = !entry.second.empty();
        bool reputation = false;
        for (uint32 questId : entry.second)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || !IsRaidQuest(state, questId))
                allRaid = false;
            if (quest && quest->GetRequiredMinRepFaction())
                reputation = true;
        }
        NoteSource(state.sourceTier, itemId, allRaid ? GEAR_SOURCE_TIER_RAID : GEAR_SOURCE_TIER_BASE);
        if (reputation)
            state.repGatedQuestItems.insert(itemId);
    }

    for (auto const& entry : sObjectMgr.GetTrainers())
    {
        std::vector<uint32> products;
        for (Trainer::Spell const& spell : entry.second.GetSpells())
        {
            products.clear();
            CollectCreatedItems(spell.SpellId, products);
            for (uint32 product : products)
                NoteSource(state.sourceTier, product, GEAR_SOURCE_TIER_BASE);
        }
    }

    uint32 perTier[3] = { 0, 0, 0 };
    uint32 reputationCount = 0;
    uint32 pvpCount = 0;
    uint32 unknownCount = 0;
    for (auto const& entry : sObjectMgr.GetItemTemplateStore())
    {
        uint32 const itemId = entry.first;
        ItemTemplate const& proto = entry.second;
        if (proto.GetClass() != ITEM_CLASS_WEAPON && proto.GetClass() != ITEM_CLASS_ARMOR)
            continue;

        auto tier = state.sourceTier.find(itemId);
        if (tier == state.sourceTier.end())
        {
            ++unknownCount;
            continue;
        }

        GearSourceRecord record;
        record.tier = tier->second;
        record.reputationGated = proto.GetRequiredReputationFaction() != 0 || proto.GetRequiredReputationRank() != 0 ||
            state.repGatedQuestItems.contains(itemId) || state.repGatedProducts.contains(itemId);
        record.pvp = proto.HasFlag(ITEM_FLAG_NO_DISENCHANT) ||
            (proto.GetItemSet() != 0 && state.pvpSets.contains(proto.GetItemSet()));
        char const* const name = proto.GetName(LOCALE_enUS);
        record.retired = proto.HasFlag(ITEM_FLAG_DEPRECATED) || (name && GearNameLooksRetired(name));
        record.cosmeticSlot = proto.GetInventoryType() == INVTYPE_BODY || proto.GetInventoryType() == INVTYPE_TABARD;
        record.itemLevel = proto.GetBaseItemLevel();
        record.quality = proto.GetQuality();

        ++perTier[record.tier];
        if (record.reputationGated)
            ++reputationCount;
        if (record.pvp)
            ++pvpCount;
        index.items.emplace(itemId, record);
    }

    index.initialized = true;
    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    Log::instance()->OutMessage("server.loading", LOG_LEVEL_INFO,
        "TortoiseBots: gear source index built ({} weapon/armor items: {} base, {} dungeon, {} raid, {} reputation, {} pvp, {} no known source) in {} ms",
        index.items.size(), perTier[0], perTier[1], perTier[2], reputationCount, pvpCount, unknownCount, elapsedMs);
}

} // namespace

void InitGearSourceIndex()
{
    BuildIndex();
}

bool SourceGateAllows(uint32 itemId)
{
    if (!itemId)
        return false;
    if (!Index().initialized)
        BuildIndex();

    PlayerbotAIConfig const& config = sPlayerbotAIConfig;
    GearSourceFacts facts;
    facts.blacklisted = GearBlacklistBlocks(config.randomGearBlacklist, itemId);
    facts.whitelisted = std::find(config.randomGearWhitelist.begin(), config.randomGearWhitelist.end(), itemId) !=
        config.randomGearWhitelist.end();

    auto found = Index().items.find(itemId);
    if (found != Index().items.end())
    {
        GearSourceRecord const& record = found->second;
        facts.knownSource = true;
        facts.tier = record.tier;
        facts.reputationGated = record.reputationGated;
        facts.pvp = record.pvp;
        facts.retired = record.retired;
        facts.cosmeticSlot = record.cosmeticSlot;
        facts.itemLevel = record.itemLevel;
        facts.quality = record.quality;
    }

    // An unindexed id has no known source: it fails unless the owner whitelisted it, and then only on the base tier with
    // no flags (there is nothing to classify).
    return GearSourceGateAllows(facts, int32(config.randomGearMaxSourceTier),
        config.randomGearAllowReputation, config.randomGearAllowPvP);
}

} // namespace gear
} // namespace TortoiseBots

#endif
