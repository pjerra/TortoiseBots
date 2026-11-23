#include "host/TbHostFishing.h"

#include "host/TbHostFishingPure.h"
#include "host/TbHostGuildOrderFish.h"
#include "compat/TbCompat_Fishing.h"
#include "runtime/GuildOrderFishingPolicy.h"

#include "Bag.h"
#include "CellImpl.h"
#include "GameObject.h"
#include "GameObjectPackets.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Map.h"
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

constexpr std::uint32_t kFishingCastSpellId = 7731;
constexpr std::uint32_t kBaseFishingPoleItemId = 6256;   // Fishing Pole
constexpr float kWalkSpotRangeYd = 800.0f;
constexpr std::uint32_t kMaxSkillValue = 300;

struct Errand
{
    bool active = false;
    bool hasSpot = false;
    std::uint32_t itemId = 0;
    float spotX = 0.0f;
    float spotY = 0.0f;
    float spotZ = 0.0f;
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
    TC_LOG_INFO("tortoisebots", "[W9] fish bot={} item={} result={} casts={} bobber={} ready={} used={} loot={} channel={}",
        bot->GetName(), errand.itemId, reason ? reason : "done", std::uint32_t(errand.casts),
        facts && facts->hasBobber, facts && facts->bobberReady, facts && facts->bobberUsed,
        facts && facts->lootAvailable, facts && facts->channelingFishing);
    if (caught)
        CaughtFlags()[bot->GetGUID()] = true;
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

void TickErrand(Player* bot, std::uint32_t diffMs)
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
        float const dx = errand.spotX - bot->GetPositionX();
        float const dy = errand.spotY - bot->GetPositionY();
        if (dx != 0.0f || dy != 0.0f)
            bot->SetFacingTo(std::atan2(dy, dx));
        // The bobber lands at the destination, so it lands in the water point
        // the store chose (the core otherwise picks a close point and the
        // bobber can fall on land).
        Position const dest(errand.spotX, errand.spotY, errand.spotZ);
        SpellCastResult const result = bot->CastSpell(dest, kFishingCastSpellId);
        errand.casts += 1;
        errand.castSent = true;
        errand.bobberUsed = false;
        errand.lootTries = 0;
        errand.castMs = 0;
        errand.phase = Phase::CastingWait;
        errand.phaseMs = 0;
        if (result != SPELL_CAST_OK)
            TC_LOG_INFO("tortoisebots", "[W9] fish cast-refused bot={} spell={} result={}",
                bot->GetName(), kFishingCastSpellId, std::uint32_t(result));
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
}

void ClearErrand(ObjectGuid const& botGuid)
{
    Errands().erase(botGuid);
    CaughtFlags().erase(botGuid);
}

} // namespace fishing
} // namespace tbhost
