#include "host/TbHostFishing.h"

#include "host/TbHostCapitalErrands.h"
#include "host/TbHostFishingPure.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostGuildOrderFish.h"
#include "compat/TbCompat_Fishing.h"
#include "runtime/GuildOrderFishingPolicy.h"

#include "Bag.h"
#include "CellImpl.h"
#include "Collision/Maps/MapDefines.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectPackets.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
#include "Maps/TerrainMgr.h"
#include "ModelIgnoreFlags.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace fishing
{

namespace
{

constexpr std::uint32_t kBaseFishingPoleItemId = 6256;   // Fishing Pole
constexpr std::uint32_t kFishingBobberEntry = 35591;     // Fishing Bobber
constexpr float kWalkSpotRangeYd = 800.0f;
constexpr std::uint32_t kMaxSkillValue = 300;

struct Errand
{
    bool active = false;
    bool idle = false;          // free-bot side activity, not a guild order
    bool hasSpot = false;
    std::uint32_t itemId = 0;
    float spotX = 0.0f;         // where the bot stands
    float spotY = 0.0f;
    float spotZ = 0.0f;
    float castX = 0.0f;         // where the bobber should land
    float castY = 0.0f;
    float castZ = 0.0f;
    std::int32_t zoneSkill = 0;
    Phase phase = Phase::Idle;
    std::uint32_t phaseMs = 0;
    std::uint32_t castMs = 0;
    std::uint8_t casts = 0;
    bool castSent = false;
    bool lureTried = false;
    bool bobberUsed = false;
    std::uint8_t lootTries = 0;
    std::uint32_t itemsGained = 0;
};

struct IdleSession
{
    std::uint32_t startMs = 0;      // 0 = no open session
    std::uint32_t endMs = 0;        // when the last one ended; starts the hourly cooldown
    int casts = 0;
    std::uint32_t lastSearchMs = 0; // 15 s search throttle
    std::uint32_t noWaterAtMs = 0;  // 15 min park after a failed search
};

std::unordered_map<ObjectGuid, Errand>& Errands()
{
    static std::unordered_map<ObjectGuid, Errand> map;
    return map;
}

std::unordered_map<ObjectGuid, bool>& CaughtFlags()
{
    static std::unordered_map<ObjectGuid, bool> map;
    return map;
}

std::unordered_map<ObjectGuid, IdleSession>& IdleSessions()
{
    static std::unordered_map<ObjectGuid, IdleSession> map;
    return map;
}

void EndIdleSession(Player* bot, char const* reason)
{
    auto it = IdleSessions().find(bot->GetGUID());
    if (it == IdleSessions().end())
        return;
    IdleSession& session = it->second;
    if (session.startMs == 0 && session.casts == 0)
        return;
    int const casts = session.casts;
    session.startMs = 0;
    session.endMs = GameTime::GetGameTimeMS();
    session.casts = 0;
    TC_LOG_INFO("tortoisebots", "[W9] fish idle end bot={} result={} casts={}",
        bot->GetName(), reason ? reason : "done", casts);
}

bool IsFishingPole(Item const* item)
{
    if (!item)
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    return proto && proto->GetClass() == ITEM_CLASS_WEAPON &&
        proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
}

Item* MainHand(Player* bot)
{
    return bot ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) : nullptr;
}

template <class Visitor>
void ForEachBagItem(Player* bot, Visitor&& visit)
{
    if (!bot)
        return;
    for (std::uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            visit(item);
    for (std::uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (std::uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(std::uint8_t(slot)))
                    visit(item);
}

Item* FindFishingPole(Player* bot)
{
    if (IsFishingPole(MainHand(bot)))
        return MainHand(bot);
    Item* found = nullptr;
    ForEachBagItem(bot, [&found](Item* item)
    {
        if (!found && IsFishingPole(item))
            found = item;
    });
    return found;
}

bool GrantItem(Player* bot, std::uint32_t itemId, std::uint32_t count)
{
    if (!bot || !itemId || !count || !sObjectMgr->GetItemTemplate(itemId))
        return false;
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) != EQUIP_ERR_OK)
        return false;
    return bot->StoreNewItem(dest, itemId, true, 0, GuidSet(), ItemContext::NONE) != nullptr;
}

std::uint32_t PoleBonusFor(std::uint32_t itemId)
{
    std::size_t count = 0;
    TortoiseBots::GuildOrders::FishingPoleBonus const* poles =
        TortoiseBots::GuildOrders::FishingPoles(count);
    for (std::size_t i = 0; i < count; ++i)
        if (poles[i].itemId == itemId)
            return poles[i].bonus;
    return 0;
}

Item* PreferredPole(Player* bot, std::int32_t zoneSkill)
{
    std::uint32_t const planned = TortoiseBots::GuildOrders::FishingPoleFor(
        zoneSkill, bot->GetSkillValue(SKILL_FISHING));
    Item* anyPole = nullptr;
    Item* plannedPole = nullptr;
    ForEachBagItem(bot, [&](Item* item)
    {
        if (!IsFishingPole(item))
            return;
        if (!anyPole)
            anyPole = item;
        if (planned && !plannedPole && item->GetEntry() == planned)
            plannedPole = item;
    });
    return plannedPole ? plannedPole : anyPole;
}

bool EnsurePoleEquipped(Player* bot, std::int32_t zoneSkill)
{
    Item* main = MainHand(bot);
    std::uint32_t const planned = TortoiseBots::GuildOrders::FishingPoleFor(
        zoneSkill, bot->GetSkillValue(SKILL_FISHING));
    if (IsFishingPole(main) && (!planned || main->GetEntry() == planned))
        return true;
    Item* pole = PreferredPole(bot, zoneSkill);
    if (!pole)
        return IsFishingPole(main);
    if (pole == main)
        return true;
    std::uint16_t dest = 0;
    if (bot->CanEquipItem(EQUIPMENT_SLOT_MAINHAND, dest, pole, true) != EQUIP_ERR_OK)
        return IsFishingPole(main);
    bot->SwapItem(pole->GetPos(), dest);
    return IsFishingPole(MainHand(bot));
}

void EnsurePoleFor(Player* bot, std::int32_t requiredSkill)
{
    std::uint32_t const planned = TortoiseBots::GuildOrders::FishingPoleFor(
        requiredSkill, bot->GetSkillValue(SKILL_FISHING));
    if (planned && !bot->HasItemCount(planned, 1))
        GrantItem(bot, planned, 1);
    if (!FindFishingPole(bot))
        GrantItem(bot, kBaseFishingPoleItemId, 1);
}

void EnsureLure(Player* bot, std::int32_t requiredSkill)
{
    if (requiredSkill <= 0 || requiredSkill <= std::int32_t(bot->GetSkillValue(SKILL_FISHING)))
        return;
    std::uint32_t const have = bot->GetItemCount(TortoiseBots::GuildOrders::kBestFishingLureItemId, false);
    if (have < TortoiseBots::GuildOrders::kFishingLureGrantCount)
        GrantItem(bot, TortoiseBots::GuildOrders::kBestFishingLureItemId,
            TortoiseBots::GuildOrders::kFishingLureGrantCount - have);
}

// A bot is granted the Fishing skill by SetSkill, and the skill alone teaches
// nothing: the rank spells (Apprentice 7620 .. Artisan 18248) are
// SkillLineAbilityAcquireMethod::Learned rows, so the core's own skill ->
// spell pass skips them and the bot never owns a fishing cast. Each rank
// spell carries the bobber summon and SPELL_EFFECT_SKILL for its rank
// (75/150/225/300), so a fishing bot learns the rank its skill fits and casts
// that, the same grant a trainer visit gives a player.
struct FishingRankSpell
{
    std::uint32_t spellId;
    std::uint16_t rank;      // SPELL_EFFECT_SKILL value for SKILL_FISHING
};

std::vector<FishingRankSpell> const& FishingRankSpells()
{
    static std::vector<FishingRankSpell> const table = []()
    {
        std::vector<FishingRankSpell> out;
        // Forever's profession rows sit on child skill lines, so the fishing line is found through its classic parent.
        for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
        {
            if (Player::GetClassicProfessionSkill(ability->SkillLine) != SKILL_FISHING)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell, DIFFICULTY_NONE);
            if (!spell)
                continue;
            bool bobber = false;
            std::uint16_t rank = 0;
            for (SpellEffectInfo const& effect : spell->GetEffects())
            {
                if (effect.Effect == SPELL_EFFECT_TRANS_DOOR && effect.MiscValue == int32(kFishingBobberEntry))
                    bobber = true;
                else if (effect.Effect == SPELL_EFFECT_SKILL && effect.MiscValue == int32(SKILL_FISHING))
                    rank = std::uint16_t(effect.CalcValue());
            }
            if (bobber && rank)
                out.push_back({ spell->Id, rank });
        }
        std::sort(out.begin(), out.end(), [](FishingRankSpell const& left, FishingRankSpell const& right)
        {
            return left.rank < right.rank;
        });
        return out;
    }();
    return table;
}

std::uint16_t FishingRankCap(Player* bot, std::uint16_t rank)
{
    SkillRaceClassInfoEntry const* raceClass =
        sDB2Manager.GetSkillRaceClassInfo(SKILL_FISHING, bot->GetRace(), bot->GetClass());
    SkillTiersEntry const* tiers = raceClass ? sObjectMgr->GetSkillTier(raceClass->SkillTierID) : nullptr;
    return tiers && rank ? std::uint16_t(tiers->GetValueForTierIndex(rank - 1)) : 0;
}

// The rank spell the skill value fits: the lowest rank whose cap still covers
// the value. A successful cast only raises the skill step (EffectSkill), so
// the bot never drops below the value the host wrote. Learned like a trainer
// visit; 0 when the data has no fishing rank spell.
std::uint32_t EnsureFishingSpell(Player* bot)
{
    if (!bot || !bot->HasSkill(SKILL_FISHING))
        return 0;
    std::vector<FishingRankSpell> const& ranks = FishingRankSpells();
    if (ranks.empty())
    {
        TC_LOG_INFO("tortoisebots", "[W9] fish rank spell bot={} result=no-rank-spells", bot->GetName());
        return 0;
    }
    std::uint32_t const skill = bot->GetSkillValue(SKILL_FISHING);
    std::uint32_t pick = ranks.back().spellId;
    for (FishingRankSpell const& rank : ranks)
        if (FishingRankCap(bot, rank.rank) >= skill)
        {
            pick = rank.spellId;
            break;
        }
    if (!bot->HasSpell(pick))
    {
        bot->LearnSpell(pick, false);
        if (bot->HasSpell(pick))
            TC_LOG_INFO("tortoisebots", "[W9] fish rank spell bot={} spell={} skill={}",
                bot->GetName(), pick, skill);
    }
    if (!bot->HasSpell(pick))
        TC_LOG_INFO("tortoisebots", "[W9] fish rank spell bot={} spell={} skill={} result=not-learned", bot->GetName(), pick, skill);
    return bot->HasSpell(pick) ? pick : 0;
}

// Idle fishing needs only what the legacy factory hands every bot: the
// secondary skill and a pole. No tiered pole or lure (orders plan those).
bool EnsureIdleFishingGear(Player* bot)
{
    if (!bot)
        return false;
    if (!bot->HasSkill(SKILL_FISHING))
    {
        std::uint16_t const maxValue = std::uint16_t(std::min<std::uint32_t>(kMaxSkillValue, bot->GetMaxSkillValueForLevel()));
        bot->SetSkill(SKILL_FISHING, 1, std::max<std::uint16_t>(1, maxValue), maxValue);
    }
    if (!bot->HasSkill(SKILL_FISHING))
        return false;
    if (!EnsureFishingSpell(bot))
        return false;
    if (!FindFishingPole(bot))
        GrantItem(bot, kBaseFishingPoleItemId, 1);
    return FindFishingPole(bot) != nullptr;
}

bool ApplyLure(Player* bot, Item* pole)
{
    if (!bot || !pole || pole->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0)
        return false;
    Item* lure = bot->GetItemByEntry(TortoiseBots::GuildOrders::kBestFishingLureItemId, ItemSearchLocation::Inventory);
    if (!lure)
        return false;

    std::uint32_t spellId = 0;
    for (ItemEffectEntry const* effect : lure->GetEffects())
        if (effect->SpellID > 0 && effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
        {
            spellId = std::uint32_t(effect->SpellID);
            break;
        }
    if (!spellId)
        return false;

    SpellCastTargets targets;
    targets.SetItemTarget(pole);
    bot->CastItemUseSpell(lure, spellId, targets, ObjectGuid::Empty, { 0, 0, 0 });
    return true;
}

bool IsChannelingFishing(Player* bot)
{
    Spell* spell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    SpellInfo const* info = spell ? spell->GetSpellInfo() : nullptr;
    char const* name = info && info->SpellName ? info->SpellName->Str[LOCALE_enUS] : nullptr;
    return name && std::string(name).compare(0, 7, "Fishing") == 0;
}

GameObject* FindOwnedBobber(Player* bot)
{
    if (!bot || !bot->FindMap())
        return nullptr;
    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 100.0f, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, 100.0f);
    for (GameObject* go : objects)
    {
        if (!go || !go->isSpawned())
            continue;
        if (go->GetGoType() != GAMEOBJECT_TYPE_FISHINGNODE)
            continue;
        if (go->GetOwnerGUID() != bot->GetGUID())
            continue;
        return go;
    }
    return nullptr;
}

struct WaterSpot
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct WaterCacheEntry
{
    bool hasWater = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t atMs = 0;
};

// One water verdict per map cell, shared by all bots for 30 minutes: the
// radial search is ~88 terrain/LOS probes, so a cell is never scanned twice
// while bots stay near it. World thread only (the bot manager's tick), so no
// lock is needed.
std::unordered_map<std::uint64_t, WaterCacheEntry>& WaterCache()
{
    static std::unordered_map<std::uint64_t, WaterCacheEntry> cache;
    return cache;
}

float FishableWaterLevelAt(Player* bot, float x, float y, float z)
{
    Map* map = bot ? bot->GetMap() : nullptr;
    TerrainInfo* terrain = map ? map->GetTerrain() : nullptr;
    if (!terrain)
        return INVALID_HEIGHT;
    float ground = 0.0f;
    float const waterLevel = terrain->GetWaterOrGroundLevel(bot->GetPhaseShift(), map->GetId(), x, y, z, &ground);
    if (waterLevel <= INVALID_HEIGHT || ground <= INVALID_HEIGHT)
        return INVALID_HEIGHT;
    if (!ai::FishingWaterDeepEnough(waterLevel, ground))
        return INVALID_HEIGHT;
    LiquidData liquid{};
    if (terrain->GetLiquidStatus(bot->GetPhaseShift(), map->GetId(), x, y, ground, {}, &liquid) == LIQUID_MAP_NO_WATER)
        return INVALID_HEIGHT;
    // Magma and slime are not fishing water.
    if (liquid.type_flags.HasFlag(map_liquidHeaderTypeFlags::Magma) ||
        liquid.type_flags.HasFlag(map_liquidHeaderTypeFlags::Slime))
        return INVALID_HEIGHT;
    // A surface query can echo the bot's own water tile from above it.
    if (waterLevel > z + 0.5f)
        return INVALID_HEIGHT;
    return waterLevel;
}

// Open-water search: coarse rings outward from the bot, 8 directions, and the
// first ring with fishable water wins; the middle candidate keeps the cast
// square to the bank. Bounded and cached per cell, never a world scan.
bool FindNearbyWater(Player* bot, float fromX, float fromY, float fromZ, float radius, WaterSpot& out)
{
    if (!bot || !bot->GetMap())
        return false;
    CellCoord const cell = Trinity::ComputeCellCoord(fromX, fromY);
    std::uint64_t const key = ai::FishingCellKey(bot->GetMapId(), std::int32_t(cell.x_coord), std::int32_t(cell.y_coord));
    std::uint32_t const now = GameTime::GetGameTimeMS();
    if (auto it = WaterCache().find(key); it != WaterCache().end() && ai::FishingWaterCacheFresh(it->second.atMs, now))
    {
        if (!it->second.hasWater)
            return false;
        out = { it->second.x, it->second.y, it->second.z };
        return true;
    }

    float const pi = 3.14159265358979323846f;
    std::vector<WaterSpot> found;
    for (float dist = ai::FISH_MIN_CAST_DISTANCE; dist <= radius + ai::FISH_MAX_CAST_DISTANCE; dist += ai::FISH_SEARCH_STEP)
    {
        found.clear();
        for (int i = 0; i < ai::FISH_SEARCH_DIRECTIONS; ++i)
        {
            float const angle = 2.0f * pi * float(i) / float(ai::FISH_SEARCH_DIRECTIONS);
            float const x = fromX + std::cos(angle) * dist;
            float const y = fromY + std::sin(angle) * dist;
            float const waterZ = FishableWaterLevelAt(bot, x, y, fromZ);
            if (waterZ <= INVALID_HEIGHT)
                continue;
            float const eyeZ = bot->GetPositionZ() + bot->GetCollisionHeight();
            if (!bot->GetMap()->isInLineOfSight(bot->GetPhaseShift(), bot->GetPositionX(), bot->GetPositionY(), eyeZ,
                    x, y, waterZ, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                continue;
            found.push_back({ x, y, waterZ });
        }
        if (!found.empty())
            break;
    }

    WaterCacheEntry entry;
    entry.atMs = now;
    if (found.empty())
    {
        WaterCache()[key] = entry;
        return false;
    }
    WaterSpot const pick = found[found.size() / 2];
    entry.hasWater = true;
    entry.x = pick.x;
    entry.y = pick.y;
    entry.z = pick.z;
    WaterCache()[key] = entry;
    out = pick;
    return true;
}

// A nearest visible fishing hole within `radius`, else false. Pools win over
// open water at pick time.
bool FindFishingHole(Player* bot, float radius, WaterSpot& out)
{
    if (!bot || !bot->GetMap())
        return false;
    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), radius, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, radius);

    GameObject* nearest = nullptr;
    float best = radius;
    for (GameObject* go : objects)
    {
        if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE)
            continue;
        float const dist = bot->GetDistance2d(go->GetPositionX(), go->GetPositionY());
        if (dist <= best)
        {
            best = dist;
            nearest = go;
        }
    }
    if (!nearest)
        return false;
    out = { nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ() };
    return true;
}

// Dry stand at `from` that can cast at the water point, else false. The cast
// lands out from the bank, so the water must sit inside the 10-20 yd window.
bool FindShoreCastSpot(Player* bot, float fromX, float fromY, float fromZ, WaterSpot const& water, WaterSpot& out)
{
    Map* map = bot ? bot->GetMap() : nullptr;
    TerrainInfo* terrain = map ? map->GetTerrain() : nullptr;
    if (!map || !terrain)
        return false;
    float const dx = water.x - fromX;
    float const dy = water.y - fromY;
    float const dist = std::sqrt(dx * dx + dy * dy);
    if (!ai::FishingCastInRange(dist))
        return false;
    if (terrain->IsInWater(bot->GetPhaseShift(), map->GetId(), fromX, fromY, fromZ))
        return false;
    float const ground = map->GetHeight(bot->GetPhaseShift(), fromX, fromY, fromZ + 2.0f, true);
    if (ground <= INVALID_HEIGHT)
        return false;
    if ((water.z - 0.5f) > fromZ && (water.z - 0.5f) > ground)
        return false;
    float const eyeZ = bot->GetPositionZ() + bot->GetCollisionHeight();
    if (!map->isInLineOfSight(bot->GetPhaseShift(), bot->GetPositionX(), bot->GetPositionY(), eyeZ,
            water.x, water.y, water.z, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        return false;
    out = { fromX, fromY, ground };
    return true;
}

// Water is near but not castable from here: sample the bot-to-water ray from
// the closest point that still reaches (20 yd off the water) back toward the
// bot and take the nearest dry stand that casts. Fixed samples, no scan.
bool StepTowardFishableWater(Player* bot, float fromX, float fromY, float fromZ,
    WaterSpot const& water, float radius, WaterSpot& out)
{
    if (!bot || !bot->GetMap())
        return false;
    float const dx = water.x - fromX;
    float const dy = water.y - fromY;
    float const dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f || dist > radius + ai::FISH_MAX_CAST_DISTANCE)
        return false;
    float const stepFrom = std::max(0.0f, dist - ai::FISH_MAX_CAST_DISTANCE);
    for (float back = stepFrom; back <= dist - 1.0f; back += ai::FISH_SEARCH_STEP)
    {
        float const t = back / dist;
        float const sx = fromX + dx * t;
        float const sy = fromY + dy * t;
        float const sz = bot->GetMap()->GetHeight(bot->GetPhaseShift(), sx, sy, fromZ + 2.0f, true);
        if (sz <= INVALID_HEIGHT)
            continue;
        WaterSpot stand;
        if (FindShoreCastSpot(bot, sx, sy, sz, water, stand))
        {
            out = stand;
            return true;
        }
    }
    return false;
}

constexpr float kGuardRadiusYd = 35.0f;
constexpr std::uint32_t kOpenWorldMapMaxId = 1;

// The catch may come from the open water (the loot's owner is the bobber) or
// from a fishing-hole pool the core picked next to the bobber (the loot's
// owner is the pool), so the whole loot view is searched, by loot type.
Loot* FindFishingLoot(Player* bot)
{
    if (!bot)
        return nullptr;
    for (auto const& entry : bot->GetAELootView())
    {
        Loot* loot = entry.second;
        if (!loot || loot->isLooted() || loot->items.empty())
            continue;
        if (loot->loot_type != LOOT_FISHING && loot->loot_type != LOOT_FISHINGHOLE &&
            loot->loot_type != LOOT_FISHING_JUNK)
            continue;
        return loot;
    }
    return nullptr;
}

void TakeLoot(Player* bot, Errand& errand, Loot* loot)
{
    std::unordered_map<std::uint32_t, std::uint32_t> beforeCounts;
    for (LootItem const& li : loot->items)
        if (li.itemid && beforeCounts.find(li.itemid) == beforeCounts.end())
            beforeCounts.emplace(li.itemid, bot->GetItemCount(li.itemid, false));

    std::vector<std::uint8_t> listIds;
    for (LootItem const& li : loot->items)
        if (!li.is_looted)
            listIds.push_back(std::uint8_t(li.LootListId));

    ObjectGuid const owner = loot->GetOwnerGUID();
    for (std::uint8_t listId : listIds)
    {
        Loot* current = bot->GetLootByWorldObjectGUID(owner);
        if (!current)
            break;
        WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
        WorldPackets::Loot::LootRequest request;
        request.Object = current->GetGUID();
        request.LootListID = listId;
        packet.Loot.push_back(request);
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
    }

    for (auto const& [itemId, before] : beforeCounts)
    {
        std::uint32_t const after = bot->GetItemCount(itemId, false);
        if (after > before)
        {
            errand.itemsGained += after - before;
            break;
        }
    }

    errand.lootTries += 1;
    if (Loot* remaining = bot->GetLootByWorldObjectGUID(owner))
        if (remaining->isLooted() || errand.itemsGained > 0 || errand.lootTries >= 2)
            bot->GetSession()->DoLootRelease(remaining);
}

float DistanceToSpot(Player* bot, Errand const& errand)
{
    float const dx = errand.spotX - bot->GetPositionX();
    float const dy = errand.spotY - bot->GetPositionY();
    float const dz = errand.spotZ - bot->GetPositionZ();
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void Finish(Player* bot, Errand& errand, char const* reason, Facts const* facts = nullptr)
{
    bool const caught = reason && std::string(reason) == "done";
    if (errand.idle)
    {
        TC_LOG_INFO("tortoisebots", "[W9] fish idle bot={} result={} casts={} bobber={} ready={} used={} loot={} channel={}",
            bot->GetName(), reason ? reason : "done", std::uint32_t(errand.casts),
            facts && facts->hasBobber, facts && facts->bobberReady, facts && facts->bobberUsed,
            facts && facts->lootAvailable, facts && facts->channelingFishing);
        // A terminal state closes the side activity now; other misses keep the
        // session (the next search may find water within the budget).
        std::string const result = reason ? reason : "done";
        if (result == "combat" || result == "dead" || result == "not-in-world")
            EndIdleSession(bot, result.c_str());
    }
    else
    {
        TC_LOG_INFO("tortoisebots", "[W9] fish bot={} item={} result={} casts={} bobber={} ready={} used={} loot={} channel={}",
            bot->GetName(), errand.itemId, reason ? reason : "done", std::uint32_t(errand.casts),
            facts && facts->hasBobber, facts && facts->bobberReady, facts && facts->bobberUsed,
            facts && facts->lootAvailable, facts && facts->channelingFishing);
        if (caught)
            CaughtFlags()[bot->GetGUID()] = true;
    }
    errand = Errand{};
}

} // namespace

bool HostAvailable()
{
    // No loaded fish locations means no spot to walk to: the order refuses
    // like a build without an executor instead of fishing in place forever.
    return tbcompat::FishingStoreLoaded();
}

bool CanFishOrder(Player* bot, std::uint32_t itemId)
{
    if (!bot || !itemId)
        return false;
    if (!HostAvailable())
        return false;
    // The errand casts in open water; an item only a fishing-hole pool can
    // yield is not reachable this way.
    if (!guild_order_fish::IsOpenWaterFishingSource(itemId))
        return false;
    if (!bot->HasSkill(SKILL_FISHING))
        return false;
    return TortoiseBots::GuildOrders::FishingZoneReachable(
        guild_order_fish::MinFishingZoneSkill(itemId), bot->GetSkillValue(SKILL_FISHING));
}

bool EnsureOrderFishing(Player* bot, std::uint32_t itemId)
{
    if (!bot || !itemId || !guild_order_fish::IsFishingSource(itemId))
        return false;

    if (!bot->HasSkill(SKILL_FISHING))
    {
        std::uint16_t const maxValue = std::uint16_t(std::min<std::uint32_t>(kMaxSkillValue, bot->GetMaxSkillValueForLevel()));
        bot->SetSkill(SKILL_FISHING, 1, std::max<std::uint16_t>(1, maxValue), maxValue);
        if (bot->HasSkill(SKILL_FISHING))
            TC_LOG_INFO("tortoisebots", "[W9] fish learn bot={} skill={}", bot->GetName(), bot->GetSkillValue(SKILL_FISHING));
    }
    if (!bot->HasSkill(SKILL_FISHING))
        return false;
    if (!EnsureFishingSpell(bot))
        return false;

    std::int32_t const minSkill = std::int32_t(guild_order_fish::MinFishingZoneSkill(itemId));
    EnsurePoleFor(bot, minSkill);
    EnsureLure(bot, minSkill);
    return true;
}

bool StartOrderErrand(Player* bot, std::uint32_t itemId, std::string& outReason)
{
    if (!bot || !itemId || !guild_order_fish::IsOpenWaterFishingSource(itemId) ||
        !EnsureOrderFishing(bot, itemId))
    {
        outReason = "noskill";
        return false;
    }

    std::uint32_t const botSkill = bot->GetSkillValue(SKILL_FISHING);
    std::uint32_t const maxZoneSkill = botSkill +
        TortoiseBots::GuildOrders::BestFishingPoleBonus() + TortoiseBots::GuildOrders::kBestFishingLureBonus;

    tbcompat::FishingSpot spot;
    if (!tbcompat::NearestFishingSpot(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            kWalkSpotRangeYd, maxZoneSkill, spot))
    {
        outReason = "no-water";
        return false;
    }

    Errand& errand = Errands()[bot->GetGUID()];
    errand = Errand{};
    CaughtFlags().erase(bot->GetGUID());
    errand.active = true;
    errand.hasSpot = true;
    errand.itemId = itemId;
    errand.spotX = spot.x;
    errand.spotY = spot.y;
    errand.spotZ = spot.z;
    errand.castX = spot.x;
    errand.castY = spot.y;
    errand.castZ = spot.z;
    errand.zoneSkill = spot.zoneSkill;
    errand.phase = Phase::Walking;
    EnsurePoleFor(bot, spot.zoneSkill);
    EnsureLure(bot, spot.zoneSkill);
    if (!FindFishingPole(bot))
    {
        errand = Errand{};
        outReason = "no-pole";
        return false;
    }
    outReason = "fishing";
    TC_LOG_INFO("tortoisebots", "[W9] fish start bot={} item={} zone={} skill={} spot={:.1f},{:.1f}",
        bot->GetName(), itemId, spot.zone, spot.zoneSkill, spot.x, spot.y);
    return true;
}

static void TickActive(Player* bot, std::uint32_t diffMs)
{
    if (!bot)
        return;
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end() || !it->second.active)
        return;
    Errand& errand = it->second;

    if (!bot->IsInWorld())
    {
        Finish(bot, errand, "not-in-world");
        return;
    }
    if (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        return;

    errand.phaseMs += diffMs;
    if (errand.castSent)
        errand.castMs += diffMs;

    GameObject* bobber = FindOwnedBobber(bot);
    Loot* loot = FindFishingLoot(bot);

    Facts facts;
    facts.phase = errand.phase;
    facts.inWorld = bot->IsInWorld();
    facts.alive = bot->IsAlive();
    facts.inCombat = bot->IsInCombat();
    facts.hasSpot = errand.hasSpot;
    facts.dryStand = errand.idle;
    facts.channelingFishing = IsChannelingFishing(bot);
    facts.hasBobber = bobber != nullptr;
    facts.bobberReady = bobber && bobber->getLootState() == GO_READY;
    facts.bobberUsed = errand.bobberUsed;
    facts.lootAvailable = loot != nullptr;
    facts.castSent = errand.castSent;
    facts.casts = errand.casts;
    facts.distanceToSpot = DistanceToSpot(bot, errand);
    facts.phaseMs = errand.phaseMs;
    facts.castMs = errand.castMs;
    facts.lootTries = errand.lootTries;
    facts.itemsGained = errand.itemsGained;

    Decision const decision = Step(facts);
    if (decision.action == Action::Done)
    {
        Finish(bot, errand, "done");
        return;
    }
    if (decision.action == Action::Fail)
    {
        Finish(bot, errand, decision.fail, &facts);
        return;
    }

    auto moveToSpot = [&]()
    {
        if (!bot->isMoving())
            bot->GetMotionMaster()->MovePoint(0, errand.spotX, errand.spotY, errand.spotZ);
    };

    auto cast = [&]()
    {
        if (!EnsurePoleEquipped(bot, errand.zoneSkill))
        {
            Finish(bot, errand, "no-pole");
            return;
        }
        Item* pole = MainHand(bot);
        if (errand.zoneSkill > 0 && !errand.lureTried &&
            errand.zoneSkill > std::int32_t(bot->GetSkillValue(SKILL_FISHING) + PoleBonusFor(pole->GetEntry())))
        {
            errand.lureTried = true;
            if (ApplyLure(bot, pole))
                return;
        }
        if (bot->IsMounted())
            bot->Dismount();
        bot->StopMoving();
        float const dx = errand.castX - bot->GetPositionX();
        float const dy = errand.castY - bot->GetPositionY();
        if (dx != 0.0f || dy != 0.0f)
            bot->SetFacingTo(std::atan2(dy, dx));
        // The core resolves the fishing target from the caster, not from a
        // supplied destination (TARGET_DEST_CASTER_FISHING rolls its own point
        // 10-20 yd in front), so the cast is the legacy fish action's
        // "<spell> <bot>": the known rank spell, aimed by the bot's facing.
        std::uint32_t const spellId = EnsureFishingSpell(bot);
        if (!spellId)
        {
            Finish(bot, errand, "no-spell");
            return;
        }
        SpellCastResult const result = bot->CastSpell(bot, spellId);
        errand.casts += 1;
        if (errand.idle)
        {
            auto session = IdleSessions().find(bot->GetGUID());
            if (session != IdleSessions().end() && session->second.startMs != 0)
                session->second.casts += 1;
        }
        errand.castSent = true;
        errand.bobberUsed = false;
        errand.lootTries = 0;
        errand.castMs = 0;
        errand.phase = Phase::CastingWait;
        errand.phaseMs = 0;
        if (result != SPELL_CAST_OK)
            TC_LOG_INFO("tortoisebots", "[W9] fish cast-refused bot={} spell={} result={}",
                bot->GetName(), spellId, std::uint32_t(result));
    };

    auto useBobber = [&]()
    {
        if (!bobber)
            return;
        WorldPackets::GameObject::GameObjUse packet{ WorldPacket(CMSG_GAME_OBJ_USE) };
        packet.Guid = bobber->GetGUID();
        bot->GetSession()->HandleGameObjectUseOpcode(packet);
        errand.bobberUsed = true;
    };

    switch (decision.action)
    {
        case Action::MoveToSpot:
            moveToSpot();
            break;
        case Action::Cast:
            cast();
            break;
        case Action::UseBobber:
            useBobber();
            break;
        case Action::TakeLoot:
            if (loot)
                TakeLoot(bot, errand, loot);
            if (errand.itemsGained == 0 && errand.lootTries >= 2)
            {
                Finish(bot, errand, "bags-full");
                return;
            }
            errand.phase = Phase::Looting;
            errand.phaseMs = 0;
            break;
        case Action::None:
        case Action::Done:
        case Action::Fail:
        default:
            if (decision.next != errand.phase)
            {
                errand.phase = decision.next;
                errand.phaseMs = 0;
            }
            break;
    }
}

bool HasActiveErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto it = Errands().find(bot->GetGUID());
    return it != Errands().end() && it->second.active;
}

void TickErrand(Player* bot, std::uint32_t diffMs)
{
    if (!bot)
        return;
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end() || !it->second.active || it->second.idle)
        return;
    TickActive(bot, diffMs);
}

bool HasActiveIdleErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto it = Errands().find(bot->GetGUID());
    return it != Errands().end() && it->second.active && it->second.idle;
}

namespace
{
void CancelIdleErrand(Player* bot)
{
    auto it = Errands().find(bot->GetGUID());
    if (it != Errands().end() && it->second.active && it->second.idle)
    {
        if (bot->isMoving())
            bot->StopMoving();
        it->second = Errand{};
    }
    // Keep the ended session: its hourly cooldown survives a free toggle.
    EndIdleSession(bot, "disabled");
}
}

void TickIdle(Player* bot, std::uint32_t diffMs, bool enabled)
{
    if (!bot)
        return;
    ObjectGuid const guid = bot->GetGUID();
    if (!enabled)
    {
        // A bot that stopped being free (a master, a group, the switch off)
        // must not keep the engine paused for a fishing spot.
        CancelIdleErrand(bot);
        return;
    }

    IdleSession& session = IdleSessions()[guid];
    std::uint32_t const now = GameTime::GetGameTimeMS();
    bool const idleActive = HasActiveIdleErrand(bot);
    bool const orderActive = !idleActive && HasActiveErrand(bot);

    if (session.startMs != 0 && IdleSessionOver(session.startMs, session.casts, now))
    {
        if (idleActive)
            Finish(bot, Errands()[guid], "budget spent");
        EndIdleSession(bot, "budget spent");
        return;
    }

    if (idleActive)
    {
        TickActive(bot, diffMs);
        if (!HasActiveIdleErrand(bot))
            session.lastSearchMs = now;
        return;
    }
    if (orderActive)
        return;
    if (tbhost::errands::HasActiveErrand(bot) || tbhost::guild_mail::HasActiveErrand(bot) ||
        tbhost::guild_orders::HasActiveDisenchantErrand(bot) || tbhost::guild_orders::HasActiveEnchantErrand(bot))
        return;
    // Open world only: a dungeon or raid bot is on a run, not on a bank.
    if (bot->GetMapId() > kOpenWorldMapMaxId)
    {
        if (session.startMs != 0)
            EndIdleSession(bot, "left-open-world");
        return;
    }

    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat())
    {
        if (session.startMs != 0)
            EndIdleSession(bot, bot->IsInCombat() ? "combat" : (!bot->IsAlive() ? "dead" : "not-in-world"));
        return;
    }
    if (bot->isMoving() || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return;
    if (session.startMs == 0 && !IdleSessionStartable(0, session.endMs, 0, now))
        return;
    if (!IdleSearchAllowed(session.lastSearchMs, session.noWaterAtMs, now))
        return;
    session.lastSearchMs = now;

    if (!EnsureIdleFishingGear(bot))
        return;

    float const radius = ai::FISH_SEARCH_RADIUS_MASTERLESS;
    float const botX = bot->GetPositionX();
    float const botY = bot->GetPositionY();
    float const botZ = bot->GetPositionZ();

    WaterSpot hole;
    WaterSpot water;
    WaterSpot stand;
    WaterSpot cast;
    bool holeFound = FindFishingHole(bot, radius, hole);
    bool holeStand = false;
    if (holeFound)
    {
        holeStand = FindShoreCastSpot(bot, botX, botY, botZ, hole, stand) ||
            StepTowardFishableWater(bot, botX, botY, botZ, hole, radius, stand);
        if (holeStand)
            cast = hole;
        else
            holeFound = false;
    }

    bool waterFound = false;
    bool waterStand = false;
    if (!holeStand)
    {
        waterFound = FindNearbyWater(bot, botX, botY, botZ, radius, water);
        if (waterFound)
        {
            waterStand = FindShoreCastSpot(bot, botX, botY, botZ, water, stand) ||
                StepTowardFishableWater(bot, botX, botY, botZ, water, radius, stand);
        }
        if (!waterStand)
            waterFound = false;
        else
            cast = water;
    }

    bool guarded = false;
    if (holeStand || waterStand)
        guarded = tbcompat::SpotGuarded(bot->GetMapId(), stand.x, stand.y, stand.z, kGuardRadiusYd,
            bot->GetLevel(), bot->GetTeam() == HORDE);

    if (!IdleSpotChosen(holeFound, waterFound, guarded))
    {
        session.noWaterAtMs = now;
        TC_LOG_INFO("tortoisebots", "[W9] fish idle search bot={} hole={} water={} guarded={}",
            bot->GetName(), holeFound, waterFound, guarded);
        return;
    }

    std::uint32_t const areaId = sTerrainMgr.GetAreaId(bot->GetPhaseShift(), bot->GetMapId(), cast.x, cast.y, cast.z);
    std::int32_t const zoneSkill = tbcompat::FishingBaseSkill(areaId);
    if (!ai::FishingZoneSkillOk(zoneSkill, bot->GetSkillValue(SKILL_FISHING)))
    {
        session.noWaterAtMs = now;
        TC_LOG_INFO("tortoisebots", "[W9] fish idle lowskill bot={} zone={} need={} skill={}",
            bot->GetName(), areaId, zoneSkill, bot->GetSkillValue(SKILL_FISHING));
        return;
    }

    session.noWaterAtMs = 0;
    if (session.startMs == 0)
    {
        session.startMs = now;
        session.casts = 0;
    }

    Errand& errand = Errands()[guid];
    errand = Errand{};
    errand.active = true;
    errand.idle = true;
    errand.hasSpot = true;
    errand.spotX = stand.x;
    errand.spotY = stand.y;
    errand.spotZ = stand.z;
    errand.castX = cast.x;
    errand.castY = cast.y;
    errand.castZ = cast.z;
    errand.zoneSkill = zoneSkill;
    errand.phase = Phase::Walking;
    TC_LOG_INFO("tortoisebots", "[W9] fish idle start bot={} hole={} zone={} need={} skill={} spot={:.1f},{:.1f}",
        bot->GetName(), holeFound, areaId, zoneSkill, bot->GetSkillValue(SKILL_FISHING), stand.x, stand.y);
}

bool TakeCaught(Player* bot)
{
    if (!bot)
        return false;
    auto it = CaughtFlags().find(bot->GetGUID());
    if (it == CaughtFlags().end() || !it->second)
        return false;
    it->second = false;
    return true;
}

std::string ActiveErrandPhase(Player const* bot)
{
    if (!bot)
        return {};
    auto it = Errands().find(bot->GetGUID());
    if (it == Errands().end() || !it->second.active)
        return {};
    switch (it->second.phase)
    {
        case Phase::Walking: return "walking";
        case Phase::Casting:
        case Phase::CastingWait: return "casting";
        case Phase::BobberWait:
        case Phase::Looting: return "fishing";
        default: return "pending";
    }
}

void CancelErrand(Player const* bot)
{
    if (!bot)
        return;
    auto it = Errands().find(bot->GetGUID());
    if (it != Errands().end())
        it->second = Errand{};
    CaughtFlags().erase(bot->GetGUID());
    IdleSessions().erase(bot->GetGUID());
}

void ClearErrand(ObjectGuid const& botGuid)
{
    Errands().erase(botGuid);
    CaughtFlags().erase(botGuid);
    IdleSessions().erase(botGuid);
}

} // namespace fishing
} // namespace tbhost
