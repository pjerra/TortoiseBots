// Fills the bot brain's ItemPrototype (TbItemPrototype.h) from ForeverCore's ItemTemplate, once per item.
//
// Every value is the core's own answer for the same fact; nothing is defaulted to make a field look present. Where an old
// column has no core counterpart the field is not in the struct at all (SHIM-R2-NOTES.md lists those and their uses).
#include "TbItemPrototype.h"
#include "DB2Stores.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "World.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace
{
// The mirrors live for the life of the process and never move: a deque keeps its nodes, so the pointers the bot code holds
// stay valid. They are all filled once from the module's WorldScript::OnWorldInitialized (FillAll), after which the table is
// only read; a lookup that arrives before that fill takes the locked lazy path below, which never runs again.
std::mutex g_mirrorLock;
std::deque<ItemPrototype> g_mirrors;
std::unordered_map<uint32, ItemPrototype const*> g_mirrorByEntry;
std::atomic<bool> g_filled{false};

// The stat value the core computes for an item at its base item level: Item::GetItemStatValue (Item.cpp:2472) is
// StatPercentEditor * GetRandomPropertyPoints(itemLevel, quality, inventoryType, subClass) * 0.0001. Per-item scaling and
// the socket-cost subtraction are left out: there is no Item instance here, and classic items have no sockets.
int32 StatValue(ItemTemplate const* tpl, uint32 index)
{
    static constexpr float Scale = 0.0001f;
    if (!tpl || index >= MAX_ITEM_PROTO_STATS)
        return 0;

    float points = GetRandomPropertyPoints(tpl->GetBaseItemLevel(), tpl->GetQuality(), uint32(tpl->GetInventoryType()), tpl->GetSubClass());
    if (points == 0.0f)
        return 0;

    float const value = float(tpl->GetStatPercentEditor(index)) * points * Scale;
    return int32(value < 0.0f ? value - 0.5f : value + 0.5f);
}

// The value of one stat mod of an item, 0 when the item has no such stat.
int32 StatTotal(ItemTemplate const* tpl, ItemModType mod)
{
    int32 total = 0;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        if (int32(tpl->GetStatModifierBonusStat(i)) == int32(mod))
            total += StatValue(tpl, i);
    return total;
}

// Only the ItemPrototypeFlags bits the bot code tests, each taken from the core flag of the same name: the bit layout of the
// two cores differs in the middle (cmangos 0x400 is bag space, ForeverCore 0x400 uses resources), so the whole word is not copied.
uint32 Flags(ItemTemplate const* tpl)
{
    uint32 flags = 0;
    if (tpl->HasFlag(ITEM_FLAG_CONJURED))            flags |= ITEM_FLAG_CONJURED;           // 0x2
    if (tpl->HasFlag(ITEM_FLAG_HAS_LOOT))            flags |= ITEM_FLAG_LOOTABLE;           // 0x4, cmangos's name for it
    if (tpl->HasFlag(ITEM_FLAG_DEPRECATED))          flags |= ITEM_FLAG_DEPRECATED;         // 0x10
    if (tpl->HasFlag(ITEM_FLAG_IS_WRAPPER))          flags |= ITEM_FLAG_WRAPPER;            // 0x200
    if (tpl->HasFlag(ITEM_FLAG_NO_DISENCHANT))       flags |= ITEM_FLAG_NO_DISENCHANT;      // 0x8000
    if (tpl->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE))   flags |= ITEM_FLAG_UNIQUE_EQUIPPED;    // 0x80000, cmangos's name for it
    return flags;
}

// ForeverCore's bag family is a mask (ItemTemplate.h:356-375); the old core has one number per family (cmangos
// ItemPrototype.h:97-108: NONE 0, ARROWS 1, BULLETS 2, SOUL_SHARDS 3, HERBS 6, ENCHANTING_SUPP 7, ENGINEERING_SUPP 8,
// KEYS 9). A mask that is exactly one of those bits becomes the old number. The AhBot pricing groups trade goods by bag
// family (runtime/AhBotPricing.cpp:23 -> runtime/policy/ah/AhBotPricingPolicy.h:97-101, switch at :415-421), and its
// herb/enchanting/leather/meat/mining groups are the numbers those categories carry on the old core's data. A trade good
// whose mask names exactly one of those categories gets its number (0x8->12, 0x10000->10, 0x400->13); a mask with several
// categories, or a later-expansion family, becomes NONE - no single old family is true for it, and the switch reads NONE
// as "no group". SHIM-R2-NOTES.md lists the masks affected and their items.
uint32 BagFamilyNumber(uint32 mask, uint32 itemClass)
{
    switch (mask)
    {
        case BAG_FAMILY_MASK_ARROWS:            return 1;
        case BAG_FAMILY_MASK_BULLETS:           return 2;
        case BAG_FAMILY_MASK_SOUL_SHARDS:       return 3;
        case BAG_FAMILY_MASK_HERBS:             return 6;
        case BAG_FAMILY_MASK_ENCHANTING_SUPP:   return 7;
        case BAG_FAMILY_MASK_ENGINEERING_SUPP:  return 8;
        case BAG_FAMILY_MASK_KEYS:              return 9;
        default:                                break;
    }

    if (itemClass == ITEM_CLASS_TRADE_GOODS)
    {
        struct Category { uint32 Mask; uint32 Number; };
        static constexpr Category TradeGoodCategories[] =
        {
            { BAG_FAMILY_MASK_HERBS,             6 },   // the AhBot herb group
            { BAG_FAMILY_MASK_ENCHANTING_SUPP,   7 },   // enchanting
            { BAG_FAMILY_MASK_LEATHERWORKING_SUPP, 12 },// leather
            { BAG_FAMILY_MASK_COOKING_SUPP,      10 },  // meat
            { BAG_FAMILY_MASK_MINING_SUPP,       13 },  // metal/stone
        };

        uint32 number = 0;
        uint32 categories = 0;
        for (Category const& category : TradeGoodCategories)
        {
            if (mask & category.Mask)
            {
                number = category.Number;
                ++categories;
            }
        }
        if (categories == 1)
            return number;
    }
    return 0;
}

// ItemTemplate::GetTableDPS (ItemTemplate.cpp:210-260, which GetDPS calls when the item has no classic damage row) indexes one of five client damage tables with AssertEntry(itemLevel): a
// row the client data does not have aborts the server, and the eager fill would meet it for every weapon in the store.
// This is GetDPS's own early return (not a weapon, quality past artifact with heirloom read as rare, an inventory type it
// does not handle), so the fill only checks the tables where the core would actually read one.
bool DamageUsesStores(ItemTemplate const* tpl)
{
    uint32 quality = tpl->GetQuality();
    if (quality == ITEM_QUALITY_HEIRLOOM)
        quality = ITEM_QUALITY_RARE;
    if (tpl->GetClass() != ITEM_CLASS_WEAPON || quality > ITEM_QUALITY_ARTIFACT)
        return false;

    switch (tpl->GetInventoryType())
    {
        case INVTYPE_AMMO:
        case INVTYPE_2HWEAPON:
        case INVTYPE_RANGED:
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_WEAPONOFFHAND:
            return true;
        default:
            return false;
    }
}

// GetDPS picks one of the five stores by inventory type and caster flag. Checking all five keeps that branch out of the
// mirror; a missing row anywhere means this client cannot answer for the item level, and it is stricter, never looser.
std::string MissingDamageReason(uint32 itemLevel)
{
    struct Store { char const* Name; bool Present; };
    Store const stores[] =
    {
        { "ItemDamageAmmo",            sItemDamageAmmoStore.LookupEntry(itemLevel) != nullptr },
        { "ItemDamageOneHand",         sItemDamageOneHandStore.LookupEntry(itemLevel) != nullptr },
        { "ItemDamageOneHandCaster",   sItemDamageOneHandCasterStore.LookupEntry(itemLevel) != nullptr },
        { "ItemDamageTwoHand",         sItemDamageTwoHandStore.LookupEntry(itemLevel) != nullptr },
        { "ItemDamageTwoHandCaster",   sItemDamageTwoHandCasterStore.LookupEntry(itemLevel) != nullptr },
    };

    std::string missing;
    for (Store const& store : stores)
    {
        if (store.Present)
            continue;
        if (!missing.empty())
            missing += ", ";
        missing += store.Name;
    }
    return missing.empty() ? std::string() : "no row for item level " + std::to_string(itemLevel) + " in " + missing;
}

// ItemTemplate::GetArmor (ItemTemplate.cpp:150-207) reads ItemArmorQuality + ItemArmorTotal + ArmorLocation for armor with
// a cloth..plate subclass, and ItemArmorShield for shields. Those are LookupEntry calls, so a missing row is a silent 0,
// not an abort; the mirror marks the item so 0 is not read as the item's armor. GetArmor returns before any table for a
// quality past artifact, and for a non-armor item or an armor subclass outside cloth..plate (not a shield) its answer is 0
// with no table that could have answered otherwise.
bool ArmorUsesStores(ItemTemplate const* tpl, bool& shield)
{
    shield = false;
    // GetArmor maps heirloom to rare before its quality ceiling (ItemTemplate.cpp:151-153), so an heirloom armor item
    // still reads the item-level tables and must be marked when one is missing.
    uint32 quality = tpl->GetQuality();
    if (quality == ITEM_QUALITY_HEIRLOOM)
        quality = ITEM_QUALITY_RARE;
    if (quality > ITEM_QUALITY_ARTIFACT || tpl->GetClass() != ITEM_CLASS_ARMOR)
        return false;

    uint32 const subClass = tpl->GetSubClass();
    if (subClass == ITEM_SUBCLASS_ARMOR_SHIELD)
    {
        shield = true;
        return true;
    }
    return subClass >= ITEM_SUBCLASS_ARMOR_CLOTH && subClass <= ITEM_SUBCLASS_ARMOR_PLATE;
}

std::string MissingArmorReason(ItemTemplate const* tpl, uint32 itemLevel, bool shield)
{
    if (shield)
        return sItemArmorShieldStore.LookupEntry(itemLevel) ? std::string()
            : "no ItemArmorShield row for item level " + std::to_string(itemLevel);

    std::string reason;
    if (!sItemArmorQualityStore.LookupEntry(itemLevel))
        reason = "no ItemArmorQuality row for item level " + std::to_string(itemLevel);
    if (!sItemArmorTotalStore.LookupEntry(itemLevel))
        reason += (reason.empty() ? "" : "; ") + std::string("no ItemArmorTotal row for item level ") + std::to_string(itemLevel);

    // The third lookup is keyed by inventory type, not item level (GetArmor maps INVTYPE_ROBE to INVTYPE_CHEST).
    uint32 const inventoryType = tpl->GetInventoryType();
    if (!sArmorLocationStore.LookupEntry(inventoryType == INVTYPE_ROBE ? INVTYPE_CHEST : inventoryType))
        reason += (reason.empty() ? "" : "; ") + std::string("no ArmorLocation row for inventory type ") + std::to_string(inventoryType);
    return reason;
}

void AddIncomplete(ItemPrototype& mirror, std::string const& reason)
{
    if (!mirror.IncompleteReason.empty())
        mirror.IncompleteReason += "; ";
    mirror.IncompleteReason += reason;
}

void Fill(ItemPrototype& mirror, ItemTemplate const* tpl)
{
    mirror.Template = tpl;
    mirror.ItemId = tpl->GetId();
    mirror.Class = tpl->GetClass();
    mirror.SubClass = tpl->GetSubClass();
    mirror.Name1 = tpl->GetName(sWorld->GetDefaultDbcLocale());
    mirror.Quality = tpl->GetQuality();
    mirror.Flags = Flags(tpl);
    mirror.BuyCount = tpl->GetBuyCount();
    mirror.BuyPrice = tpl->GetBuyPrice();
    mirror.SellPrice = tpl->GetSellPrice();
    mirror.InventoryType = uint32(tpl->GetInventoryType());
    mirror.AllowableClass = uint32(tpl->GetAllowableClass());
    mirror.AllowableRace = uint32(tpl->GetAllowableRace().RawValue[0]);
    mirror.ItemLevel = tpl->GetBaseItemLevel();
    mirror.RequiredLevel = uint32(std::max<int32>(0, tpl->GetBaseRequiredLevel()));
    mirror.RequiredSkill = tpl->GetRequiredSkill();
    mirror.RequiredSkillRank = tpl->GetRequiredSkillRank();
    mirror.RequiredSpell = tpl->GetRequiredSpell();
    mirror.RequiredReputationFaction = tpl->GetRequiredReputationFaction();
    mirror.RequiredReputationRank = tpl->GetRequiredReputationRank();
    mirror.MaxCount = tpl->GetMaxCount();
    // The raw column, not GetMaxStackSize(): the bot multiplies prices by it, and that accessor turns 0/negative into
    // 0x7FFFFFFE.
    mirror.Stackable = uint32(std::max<int32>(0, tpl->ExtendedData->Stackable));
    mirror.ContainerSlots = tpl->GetContainerSlots();

    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        mirror.ItemStat[i].ItemStatType = uint32(tpl->GetStatModifierBonusStat(i));
        mirror.ItemStat[i].ItemStatValue = StatValue(tpl, i);
    }

    mirror.Delay = tpl->GetDelay();
    mirror.RangedModRange = tpl->GetRangedModRange();

    uint32 const itemLevel = tpl->GetBaseItemLevel();
    mirror.Incomplete = false;
    mirror.IncompleteReason.clear();

    if (DamageUsesStores(tpl))
    {
        std::string const missing = MissingDamageReason(itemLevel);
        if (missing.empty())
        {
            float minDamage = 0.0f;
            float maxDamage = 0.0f;
            tpl->GetDamage(itemLevel, minDamage, maxDamage);
            mirror.Damage[0].DamageMin = minDamage;
            mirror.Damage[0].DamageMax = maxDamage;
        }
        else
        {
            mirror.Incomplete = true;
            AddIncomplete(mirror, missing);
        }
    }
    mirror.Damage[0].DamageType = tpl->GetDamageType();

    // The shield's own block value is the classic column: ForeverCore keeps it in item_classic_block and adds it to the
    // shield's block (ObjectMgr.cpp:808-833, Player.cpp:9054-9066), the same place the old core's item_template.block went
    // (ObjectMgr.cpp:2301, Player.cpp:8507-8508). The ITEM_MOD_BLOCK_VALUE stat is a separate bonus on this core
    // (Player.cpp:8903-8905) and was an equip spell on the old core - never part of its Block column - so it is not added;
    // BlockStat keeps it for the live check. The resistances are item stats on ForeverCore where cmangos had a column each.
    mirror.BlockBase = sObjectMgr->GetItemClassicBlock(tpl->GetId());
    mirror.BlockStat = uint32(std::max<int32>(0, StatTotal(tpl, ITEM_MOD_BLOCK_VALUE)));
    mirror.Block = mirror.BlockBase;

    bool shieldSubclass = false;
    std::string armorMissing;
    if (ArmorUsesStores(tpl, shieldSubclass))
        armorMissing = MissingArmorReason(tpl, itemLevel, shieldSubclass);
    if (armorMissing.empty())
        mirror.Armor = int32(tpl->GetArmor(itemLevel));
    else
    {
        mirror.Incomplete = true;
        AddIncomplete(mirror, armorMissing);
    }
    int32 const allSchools = StatTotal(tpl, ITEM_MOD_CLASSIC_RESISTANCE_ALL_SCHOOLS);
    // "+X to all resistances" reaches fire, nature, frost, shadow and arcane, but not holy (Player.cpp:9012-9014).
    mirror.HolyRes = StatTotal(tpl, ITEM_MOD_HOLY_RESISTANCE);
    mirror.FireRes = StatTotal(tpl, ITEM_MOD_FIRE_RESISTANCE) + allSchools;
    mirror.NatureRes = StatTotal(tpl, ITEM_MOD_NATURE_RESISTANCE) + allSchools;
    mirror.FrostRes = StatTotal(tpl, ITEM_MOD_FROST_RESISTANCE) + allSchools;
    mirror.ShadowRes = StatTotal(tpl, ITEM_MOD_SHADOW_RESISTANCE) + allSchools;
    mirror.ArcaneRes = StatTotal(tpl, ITEM_MOD_ARCANE_RESISTANCE) + allSchools;

    // The five spell slots cmangos had as item_template.spells[0..4] are the item's effects, one per LegacySlotIndex, in
    // the core's own order (ObjectMgr.cpp:3743 sorts them by that index).
    for (ItemEffectEntry const* effect : tpl->Effects)
    {
        if (!effect || effect->LegacySlotIndex >= MAX_ITEM_PROTO_SPELLS)
            continue;

        _ItemSpell& slot = mirror.Spells[effect->LegacySlotIndex];
        slot.SpellId = uint32(std::max<int32>(0, effect->SpellID));
        slot.SpellTrigger = effect->TriggerType;
        slot.SpellCharges = effect->Charges;
        slot.SpellPPMRate = tpl->SpellPPMRate;
        slot.SpellCooldown = effect->CoolDownMSec;
        slot.SpellCategory = effect->SpellCategoryID;
        // L5 (package L): a food item whose ItemEffect row carries no category (0) read as "not food" to FindFoodVisitor (category 11) while the
        // same bread worked through `.tb use`. cmangos' item_template.spellcategory_1 was the spell's own category for food and drink, so a missing
        // value takes the spell's (SpellInfo::GetCategory); a stated category is kept as it is.
        if (!slot.SpellCategory && slot.SpellId)
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(slot.SpellId, DIFFICULTY_NONE))
                slot.SpellCategory = spellInfo->GetCategory();
        slot.SpellCategoryCooldown = effect->CategoryCoolDownMSec;
    }

    mirror.Bonding = uint32(tpl->GetBonding());
    mirror.StartQuest = tpl->GetStartQuest();
    mirror.LockID = tpl->GetLockID();
    mirror.ItemSet = tpl->GetItemSet();
    mirror.MaxDurability = tpl->MaxDurability;
    mirror.Area = tpl->GetArea(0);
    mirror.Map = tpl->GetMap();
    mirror.Duration = tpl->GetDuration();
    mirror.BagFamilyMask = tpl->GetBagFamily();
    mirror.BagFamily = BagFamilyNumber(mirror.BagFamilyMask, tpl->GetClass());
}
}  // namespace

bool ItemPrototype::CanChangeEquipStateInCombat() const
{
    switch (InventoryType)
    {
        case INVTYPE_RELIC:
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE:
            return true;
        default:
            break;
    }

    switch (Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_PROJECTILE:
            return true;
        default:
            return false;
    }
}

uint32 ItemPrototype::GetProficiencySkill() const
{
    // Skilling table of the old struct, answered by the core for the same item: ItemTemplate::GetSkill (ItemTemplate.cpp:97).
    return Template ? Template->GetSkill() : 0;
}

namespace tbcompat
{
namespace
{
ItemPrototype const* Find(uint32 entry)
{
    auto known = g_mirrorByEntry.find(entry);
    return known != g_mirrorByEntry.end() ? known->second : nullptr;
}
}  // namespace

// Filled once, from the item store that World::SetInitialWorldSettings has loaded (World.cpp:1625; item_classic_block at
// World.cpp:1654) just before the world goes online (Main.cpp:372). The container is not modified again, so the lookups
// read it without the lock; the release store publishes the fill to the map threads.
void FillAll()
{
    uint32 const start = getMSTime();
    std::lock_guard<std::mutex> lock(g_mirrorLock);
    if (g_filled.load(std::memory_order_relaxed))
        return;

    ItemTemplateContainer const& store = sObjectMgr->GetItemTemplateStore();
    if (store.empty())
    {
        TC_LOG_WARN("server.loading", "TortoiseBots: item mirror fill found an empty item store; keeping the lazy path.");
        return;
    }

    uint32 stackableAnomalies = 0;
    g_mirrorByEntry.reserve(store.size());

    for (auto const& entry : store)
    {
        ItemTemplate const& tpl = entry.second;
        int32 const stackable = tpl.ExtendedData->Stackable;
        if (stackable <= 0 || stackable == std::numeric_limits<int32>::max())
            ++stackableAnomalies;

        // Keep a mirror a pre-fill lazy lookup already handed out: its address must not move or die.
        if (g_mirrorByEntry.find(entry.first) != g_mirrorByEntry.end())
            continue;

        ItemPrototype& mirror = g_mirrors.emplace_back();
        Fill(mirror, &tpl);
        g_mirrorByEntry[entry.first] = &mirror;
    }

    g_filled.store(true, std::memory_order_release);

    // One line, whether or not anything is missing, so the log always says how the fill went. The first ten mirrors with a
    // field no client table could answer are named by entry, in the store's own order.
    uint32 incomplete = 0, incompleteLowLevel = 0, ammoStrictRejects = 0;
    std::string firstIncomplete;
    for (auto const& entry : store)
    {
        ItemPrototype const* mirror = Find(entry.first);
        if (!mirror || !mirror->Incomplete)
            continue;
        ++incomplete;
        if (mirror->RequiredLevel <= 60)
            ++incompleteLowLevel;

        // The damage pre-check is stricter than the core: it asks all five ItemDamage* stores, while GetDPS picks one by
        // inventory type. An ammo item whose own store has its row but another does not is rejected by the pre-check
        // alone; count those so the difference is visible.
        ItemTemplate const& tpl = entry.second;
        uint32 const itemLevel = tpl.GetBaseItemLevel();
        if (tpl.GetInventoryType() == INVTYPE_AMMO &&
            sItemDamageAmmoStore.LookupEntry(itemLevel) && !MissingDamageReason(itemLevel).empty())
            ++ammoStrictRejects;

        if (incomplete <= 10)
        {
            if (!firstIncomplete.empty())
                firstIncomplete += "; ";
            firstIncomplete += std::to_string(mirror->ItemId) + " (" + mirror->IncompleteReason + ")";
        }
    }

    TC_LOG_INFO("server.loading", "TortoiseBots: item mirrors filled: {} items in {} ms, {} with a Stackable column <= 0 or INT_MAX, {} incomplete ({} at required level 60 or less), {} incomplete ammo items whose ItemDamageAmmo row exists but the five-store damage pre-check rejects: {}.",
        uint32(store.size()), GetMSTimeDiffToNow(start), stackableAnomalies, incomplete, incompleteLowLevel, ammoStrictRejects,
        firstIncomplete.empty() ? std::string("none") : firstIncomplete);
}

ItemPrototype const* ItemProto(uint32 entry)
{
    if (!entry)
        return nullptr;

    // Steady state: the table was filled at startup and is never written again.
    if (g_filled.load(std::memory_order_acquire))
        return Find(entry);

    // A lookup before the fill (not expected: the fill runs before the world is online) builds one mirror under the lock.
    std::lock_guard<std::mutex> lock(g_mirrorLock);
    if (g_filled.load(std::memory_order_relaxed))
        return Find(entry);

    if (ItemPrototype const* known = Find(entry))
        return known;

    ItemTemplate const* tpl = sObjectMgr->GetItemTemplate(entry);
    if (!tpl)
        return nullptr;

    ItemPrototype& mirror = g_mirrors.emplace_back();
    Fill(mirror, tpl);
    g_mirrorByEntry[entry] = &mirror;
    return &mirror;
}

ItemPrototype const* ItemProto(ItemTemplate const* tpl)
{
    return tpl ? ItemProto(tpl->GetId()) : nullptr;
}

ItemTemplate const* ItemTpl(ItemPrototype const* proto)
{
    return proto ? proto->Template : nullptr;
}
}  // namespace tbcompat
