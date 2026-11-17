// Fills the bot brain's ItemPrototype (TbItemPrototype.h) from ForeverCore's ItemTemplate, once per item.
//
// Every value is the core's own answer for the same fact; nothing is defaulted to make a field look present. Where an old
// column has no core counterpart the field is not in the struct at all (SHIM-R2-NOTES.md lists those and their uses).
#include "TbItemPrototype.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ObjectMgr.h"
#include "World.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace
{
// The mirrors live for the life of the process and never move: a deque keeps its nodes, so the pointers the bot code holds
// stay valid. Bot code runs on the map threads, so the table and the filling are behind one mutex.
std::mutex g_mirrorLock;
std::deque<ItemPrototype> g_mirrors;
std::unordered_map<uint32, ItemPrototype const*> g_mirrorByEntry;

// The stat value the core computes for an item at its base item level: Item::GetItemStatValue (Item.cpp:2432) is
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

    uint32 const itemLevel = tpl->GetBaseItemLevel();
    float minDamage = 0.0f;
    float maxDamage = 0.0f;
    tpl->GetDamage(itemLevel, minDamage, maxDamage);
    mirror.Damage[0].DamageMin = minDamage;
    mirror.Damage[0].DamageMax = maxDamage;
    mirror.Damage[0].DamageType = tpl->GetDamageType();

    // Block and the resistances are item stats on ForeverCore where cmangos had a column each: the value the item grants
    // is the sum of that stat mod's computed value.
    mirror.Block = uint32(StatTotal(tpl, ITEM_MOD_BLOCK_VALUE));
    mirror.Armor = int32(tpl->GetArmor(itemLevel));
    int32 const allSchools = StatTotal(tpl, ITEM_MOD_CLASSIC_RESISTANCE_ALL_SCHOOLS);
    mirror.HolyRes = StatTotal(tpl, ITEM_MOD_HOLY_RESISTANCE) + allSchools;
    mirror.FireRes = StatTotal(tpl, ITEM_MOD_FIRE_RESISTANCE) + allSchools;
    mirror.NatureRes = StatTotal(tpl, ITEM_MOD_NATURE_RESISTANCE) + allSchools;
    mirror.FrostRes = StatTotal(tpl, ITEM_MOD_FROST_RESISTANCE) + allSchools;
    mirror.ShadowRes = StatTotal(tpl, ITEM_MOD_SHADOW_RESISTANCE) + allSchools;
    mirror.ArcaneRes = StatTotal(tpl, ITEM_MOD_ARCANE_RESISTANCE) + allSchools;

    // The five spell slots cmangos had as item_template.spells[0..4] are the item's effects, one per LegacySlotIndex, in
    // the core's own order (ObjectMgr.cpp:3741 sorts them by that index).
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
    mirror.BagFamily = tpl->GetBagFamily();
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
ItemPrototype const* ItemProto(uint32 entry)
{
    if (!entry)
        return nullptr;

    std::lock_guard<std::mutex> lock(g_mirrorLock);
    auto known = g_mirrorByEntry.find(entry);
    if (known != g_mirrorByEntry.end())
        return known->second;

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
