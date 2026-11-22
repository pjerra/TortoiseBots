#include "host/gear/TbGearGen.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"

namespace
{
inline void LogGearIndexBuilt(uint32 count, long long elapsedMs)
{
    TC_LOG_INFO("server.loading", "TortoiseBots: gear index built ({} classic items in {} ms)", count, elapsedMs);
}
} // namespace

#include "compat/botpch_forever.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/policy/item/GearSetPolicy.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace TortoiseBots
{
namespace gear
{
namespace
{

struct GearIndexState
{
    bool initialized = false;
    uint32 totalCount = 0;
    std::array<std::vector<ai::GearItemCandidate>, 18> bySlot;
    std::unordered_map<uint32, ai::GearItemCandidate> byId;
};

GearIndexState& Index()
{
    static GearIndexState s;
    return s;
}

bool EquipCandidateInSlot(Player* target, uint8 slot, uint32 itemId, std::string& outStatus, std::string& outCode)
{
    Item* existing = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (existing && existing->GetEntry() == itemId)
    {
        outStatus = "kept";
        outCode.clear();
        return true;
    }

    uint16 dest = 0;
    if (!existing)
    {
        InventoryResult const res = target->CanEquipNewItem(slot, dest, itemId, false);
        if (res != EQUIP_ERR_OK)
        {
            outStatus = "refused";
            outCode = "core-refused";
            return false;
        }
        Item* worn = target->EquipNewItem(dest, itemId, ItemContext::NONE, true);
        Item* check = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (worn && check && check->GetEntry() == itemId)
        {
            outStatus = "equipped";
            outCode.clear();
            return true;
        }
        outStatus = "refused";
        outCode = "core-refused";
        return false;
    }

    // Slot is occupied: move old item to bags via CanStoreNewItem + StoreNewItem + SwapItem; never destroy worn gear or pre-existing bag copies.
    InventoryResult const canSwap = target->CanEquipNewItem(slot, dest, itemId, true);
    if (canSwap != EQUIP_ERR_OK)
    {
        outStatus = "refused";
        outCode = "core-refused";
        return false;
    }

    ItemPosCountVec storeDest;
    if (target->CanStoreNewItem(NULL_BAG, NULL_SLOT, storeDest, itemId, 1) != EQUIP_ERR_OK)
    {
        outStatus = "refused";
        outCode = "bags-full";
        return false;
    }

    Item* fresh = target->StoreNewItem(storeDest, itemId, true, 0, GuidSet(), ItemContext::NONE);
    if (!fresh)
    {
        outStatus = "refused";
        outCode = "bags-full";
        return false;
    }

    ObjectGuid const freshGuid = fresh->GetGUID();
    uint16 swapDest = 0;
    if (target->CanEquipItem(slot, swapDest, fresh, true) != EQUIP_ERR_OK)
    {
        if (Item* created = target->GetItemByGuid(freshGuid))
            target->DestroyItem(created->GetBagSlot(), created->GetSlot(), true);
        outStatus = "refused";
        outCode = "core-refused";
        return false;
    }

    target->SwapItem(fresh->GetPos(), swapDest);
    Item* check = target->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (check && check->GetEntry() == itemId)
    {
        outStatus = "equipped";
        outCode.clear();
        return true;
    }

    if (Item* created = target->GetItemByGuid(freshGuid))
        target->DestroyItem(created->GetBagSlot(), created->GetSlot(), true);
    outStatus = "refused";
    outCode = "core-refused";
    return false;
}

} // namespace

void InitGearIndex()
{
    auto const start = std::chrono::steady_clock::now();
    GearIndexState& idx = Index();
    for (auto& v : idx.bySlot)
        v.clear();
    idx.byId.clear();
    idx.totalCount = 0;

    ItemTemplateContainer const& store = ObjectMgr::instance()->GetItemTemplateStore();
    for (auto const& [id, tpl] : store)
    {
        if (!ai::IsClassicItemCandidate(id, tpl.GetRequiredExpansion(), tpl.GetQuality(), tpl.RandomBonusListTemplateId))
            continue;
        if (tpl.GetClass() != ITEM_CLASS_WEAPON && tpl.GetClass() != ITEM_CLASS_ARMOR)
            continue;

        ai::GearItemCandidate cand;
        cand.itemId = id;
        cand.requiredExpansion = tpl.GetRequiredExpansion();
        cand.randomBonusListTemplateId = tpl.RandomBonusListTemplateId;
        cand.allowableClass = tpl.GetAllowableClass();
        cand.itemLevel = static_cast<uint16>(tpl.GetBaseItemLevel());
        cand.requiredLevel = static_cast<uint8>(std::min<uint32>(tpl.GetBaseRequiredLevel(), 255u));
        cand.quality = static_cast<uint8>(tpl.GetQuality());
        cand.inventoryType = static_cast<uint8>(tpl.GetInventoryType());
        cand.itemClass = static_cast<uint8>(tpl.GetClass());
        cand.subClass = static_cast<uint8>(tpl.GetSubClass());
        cand.unique = (tpl.GetMaxCount() == 1);

        bool mapped = false;
        for (uint8 slot : ai::GearSetSlotOrder())
        {
            if (ai::InventoryTypeFitsSlot(cand.inventoryType, slot))
            {
                idx.bySlot[slot].push_back(cand);
                mapped = true;
            }
        }
        if (mapped)
        {
            idx.byId.emplace(id, cand);
            ++idx.totalCount;
        }
    }

    idx.initialized = true;
    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    LogGearIndexBuilt(idx.totalCount, elapsedMs);
}

uint32 GearIndexItemCount()
{
    if (!Index().initialized)
        InitGearIndex();
    return Index().totalCount;
}

std::vector<ai::GearItemCandidate> const& SlotCandidates(uint8 slot)
{
    if (!Index().initialized)
        InitGearIndex();
    static std::vector<ai::GearItemCandidate> const kEmpty;
    return slot < Index().bySlot.size() ? Index().bySlot[slot] : kEmpty;
}

ai::GearItemCandidate const* FindCandidate(uint32 itemId)
{
    if (!Index().initialized)
        InitGearIndex();
    auto const it = Index().byId.find(itemId);
    return it != Index().byId.end() ? &it->second : nullptr;
}

std::vector<GearAuditEntry> AuditGearIndex(uint32 level)
{
    if (!Index().initialized)
        InitGearIndex();

    uint32 const clampedLevel = std::clamp<uint32>(level, 1, 60);
    uint8 const maxQ = ai::TargetMaxQualityForLevel(clampedLevel);
    ai::GearLevelBracket const bracket = ai::LevelBracketForLevel(clampedLevel);

    std::vector<GearAuditEntry> out;
    for (uint8 slot : ai::GearSetSlotOrder())
    {
        uint32 count = 0;
        for (ai::GearItemCandidate const& c : Index().bySlot[slot])
        {
            if (c.quality <= maxQ &&
                c.requiredLevel <= bracket.maxRequiredLevel &&
                c.itemLevel >= bracket.minItemLevel &&
                c.itemLevel <= bracket.maxItemLevel)
            {
                ++count;
            }
        }
        out.push_back({ slot, count });
    }
    return out;
}

GearSetOutcome SetGearForTarget(Player* target, std::string const& specArg, uint32 levelArg, int32 qualityArg)
{
    GearSetOutcome outcome;
    if (!target)
    {
        outcome.errorCode = "no-such-player";
        outcome.errorDetail = "target is null";
        return outcome;
    }

    if (!Index().initialized)
        InitGearIndex();

    uint8 const playerClass = target->GetClass();
    uint32 specId = 0;
    std::string canonicalSpec;
    if (!specArg.empty())
    {
        canonicalSpec = ai::CanonicalSpecName(playerClass, specArg);
        specId = sRandomItemMgr.GetSpecIdFor(playerClass, canonicalSpec);
        if (!specId)
        {
            outcome.errorCode = "unknown-spec";
            outcome.errorDetail = specArg + " is not a spec for this class";
            return outcome;
        }
    }
    else
    {
        specId = sRandomItemMgr.GetPlayerSpecId(target);
        if (!specId)
            specId = sRandomItemMgr.GetFallbackSpecId(playerClass);
        canonicalSpec = ai::CanonicalSpecName(playerClass, sRandomItemMgr.GetSpecName(specId));
    }

    uint32 const level = levelArg > 0 ? std::clamp<uint32>(levelArg, 1, 60) : std::clamp<uint32>(target->GetLevel(), 1, 60);
    uint8 const maxQuality = ai::TargetMaxQualityForLevel(level, qualityArg);
    bool const canDualWield = target->CanDualWield();

    outcome.ok = true;
    outcome.specName = canonicalSpec;
    outcome.level = level;
    outcome.maxQuality = maxQuality;

    bool mainHandIsTwoHanded = false;
    std::vector<uint32> alreadyPicked;

    for (uint8 slot : ai::GearSetSlotOrder())
    {
        GearSlotResult res;
        res.slot = slot;

        if (slot == 16 && mainHandIsTwoHanded)
        {
            // Unequip any existing off-hand to bags if possible when wearing a 2H weapon.
            if (Item* oldOh = target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                target->AutoUnequipOffhandIfNeed(true);
            res.status = "refused";
            res.code = "two-hand";
            outcome.slots.push_back(res);
            continue;
        }

        // Filter slot pool first, then score only fitting candidates.
        std::vector<ai::GearItemCandidate> ranked = ai::FilterCandidatesForSlot(
            Index().bySlot[slot], playerClass, canonicalSpec, level, slot, maxQuality, canDualWield, mainHandIsTwoHanded, alreadyPicked);
        for (ai::GearItemCandidate& c : ranked)
        {
            if (ItemPrototype const* proto = tbcompat::ItemProto(c.itemId))
            {
                ItemSpecType itSpec;
                c.score = sRandomItemMgr.CalculateStatWeight(playerClass, static_cast<uint8>(specId), proto, itSpec);
            }
        }
        ai::SortCandidatesForSlot(ranked);

        // At level 60, prepend preferred replacements from gear-replacements.csv when they fit.
        if (level == 60)
        {
            std::vector<uint32> const prefs = ai::PreferredReplacementsFor(playerClass, canonicalSpec, slot);
            std::vector<ai::GearItemCandidate> prefCandidates;
            for (uint32 prefId : prefs)
            {
                auto it = Index().byId.find(prefId);
                if (it == Index().byId.end())
                    continue;
                ai::GearItemCandidate cand = it->second;
                if (!ai::CandidateFitsSlotPolicy(cand, playerClass, canonicalSpec, level, slot, maxQuality, canDualWield, mainHandIsTwoHanded))
                    continue;
                if (std::find(alreadyPicked.begin(), alreadyPicked.end(), cand.itemId) != alreadyPicked.end())
                    continue;
                if (ItemPrototype const* proto = tbcompat::ItemProto(cand.itemId))
                {
                    ItemSpecType itSpec;
                    cand.score = sRandomItemMgr.CalculateStatWeight(playerClass, static_cast<uint8>(specId), proto, itSpec);
                }
                prefCandidates.push_back(cand);
            }
            if (!prefCandidates.empty())
            {
                // Keep highest-tier fitting replacement first, then generated candidates.
                ranked.insert(ranked.begin(), prefCandidates.begin(), prefCandidates.end());
            }
        }

        bool equippedSlot = false;
        std::string lastFailCode = "no-item";
        for (ai::GearItemCandidate const& cand : ranked)
        {
            uint16 dest = 0;
            if (target->CanEquipNewItem(slot, dest, cand.itemId, true) != EQUIP_ERR_OK)
                continue;

            std::string status, code;
            if (EquipCandidateInSlot(target, slot, cand.itemId, status, code))
            {
                res.itemId = cand.itemId;
                res.itemLevel = cand.itemLevel;
                res.score = cand.score;
                res.status = status;
                equippedSlot = true;
                alreadyPicked.push_back(cand.itemId);
                if (slot == 15 && cand.inventoryType == INVTYPE_2HWEAPON)
                    mainHandIsTwoHanded = true;
                ++outcome.equipped;
                break;
            }
            if (code == "bags-full")
            {
                lastFailCode = "bags-full";
                break;
            }
            if (lastFailCode == "no-item" && !code.empty())
                lastFailCode = code;
        }

        if (!equippedSlot)
        {
            res.status = "refused";
            res.code = lastFailCode;
            ++outcome.failed;
        }
        outcome.slots.push_back(res);
    }

    return outcome;
}

} // namespace gear
} // namespace TortoiseBots

#endif
