// The bot brain reads item data as public fields of cmangos's ItemPrototype (`proto->Class`, `proto->ItemStat[i].ItemStatType`, ...).
// ForeverCore's ItemTemplate keeps the same facts private behind accessors, and a macro cannot turn a field into a call.
//
// So this is our ItemPrototype: the old field names, filled once per item from the core's ItemTemplate (TbItemPrototype.cpp),
// and kept for the life of the process. The 1,168 field reads in the bot code then compile unchanged on both cores.
//
// Only the fields the bot actually uses are here, each with what it means on the Turtle core and the ForeverCore accessor it is
// filled from. A used field with no true ForeverCore counterpart is left out on purpose (its uses stay census errors):
// SHIM-R2-NOTES.md lists those with their uses, so nobody reads a guess as data.
#pragma once

#include "ItemTemplate.h"   // ItemTemplate, ItemModType/ItemBondingType/ItemFlags values, MAX_ITEM_PROTO_STATS
#include <string>

// cmangos ItemPrototype.h:397-418.
struct _ItemDamage
{
    float  DamageMin;
    float  DamageMax;
    uint32 DamageType;
};

struct _ItemStat
{
    uint32 ItemStatType;
    int32  ItemStatValue;
};

struct _ItemSpell
{
    uint32 SpellId;
    uint32 SpellTrigger;
    int32  SpellCharges;
    float  SpellPPMRate;
    int32  SpellCooldown;
    uint32 SpellCategory;
    int32  SpellCategoryCooldown;
};

// cmangos ItemPrototype.h:420-422; MAX_ITEM_PROTO_STATS is the core's own (DBCEnums.h:1215, also 10).
#define MAX_ITEM_PROTO_DAMAGES 5
#define MAX_ITEM_PROTO_SPELLS  5

// The older cmangos spelling of the spell slot struct, which the bot strategies still use (ai/cmangos-compat-shim.h:265).
using _Spell = _ItemSpell;

// The ItemPrototypeFlags bits the bot code tests that ForeverCore does not name (cmangos ItemPrototype.h:60-88). The other
// bits it tests are the core's own ItemFlags with the same values (CONJURED 0x2, DEPRECATED 0x10, NO_DISENCHANT 0x8000) and
// are not repeated here.
enum ItemPrototypeFlags : uint32
{
    ITEM_FLAG_LOOTABLE        = 0x00000004,   // right click to open; the core calls it ITEM_FLAG_HAS_LOOT
    ITEM_FLAG_WRAPPER         = 0x00000200,   // the core calls it ITEM_FLAG_IS_WRAPPER
    ITEM_FLAG_UNIQUE_EQUIPPED = 0x00080000,   // the core calls it ITEM_FLAG_UNIQUE_EQUIPPABLE
};

// The old header's row index into DurabilityCosts for an item class/subclass pair: weapons are rows 0..20, armor 21..30.
// The table and the class/subclass numbering are the client's, so the answer is the same on both cores (cmangos
// ItemPrototype.h:355-365).
inline uint8 ItemSubClassToDurabilityMultiplierId(uint32 itemClass, uint32 itemSubClass)
{
    switch (itemClass)
    {
        case ITEM_CLASS_WEAPON: return uint8(itemSubClass);
        case ITEM_CLASS_ARMOR:  return uint8(itemSubClass + 21);
        default: break;
    }
    return 0;
}

struct ItemPrototype
{
    ItemTemplate const* Template;                           // the core record this mirror was filled from

    uint32 ItemId;                                          // T ItemPrototype.h:426                 F GetId()                 ItemTemplate.h:870
    uint32 Class;                                           // T :427 item class (ItemClass)         F GetClass()              :871
    uint32 SubClass;                                        // T :428 item subclass                  F GetSubClass()           :872
    std::string Name1;                                      // T :429 the item name                  F GetName(locale)         :905 (default DBC locale)
    uint32 Quality;                                         // T :432 0..6 (ITEM_QUALITY_*)         F GetQuality()            :874
    uint32 Flags;                                           // T :433 item_template.flags            F the ItemFlags bits the bot tests, by name (:23-72)
    uint32 BuyCount;                                        // T :434 items per vendor purchase      F GetBuyCount()           :878
    uint32 BuyPrice;                                        // T :435 copper                         F GetBuyPrice()           :879
    uint32 SellPrice;                                       // T :436 copper                         F GetSellPrice()          :880
    uint32 InventoryType;                                   // T :437 INVTYPE_*                      F GetInventoryType()      :881
    uint32 AllowableClass;                                  // T :438 class mask, 0xFFFFFFFF = all   F GetAllowableClass()     :882
    uint32 AllowableRace;                                   // T :439 race mask (first 32 bits)      F GetAllowableRace()      :883
    uint32 ItemLevel;                                       // T :440                               F GetBaseItemLevel()      :884
    uint32 RequiredLevel;                                   // T :441                               F GetBaseRequiredLevel()  :885
    uint32 RequiredSkill;                                   // T :442 skill line id                  F GetRequiredSkill()      :886
    uint32 RequiredSkillRank;                               // T :443                               F GetRequiredSkillRank()  :887
    uint32 RequiredSpell;                                   // T :444 spell id                       F GetRequiredSpell()      :888
    uint32 RequiredReputationFaction;                        // T :447                               F GetRequiredReputationFaction() :889
    uint32 RequiredReputationRank;                          // T :448                               F GetRequiredReputationRank()    :890
    uint32 MaxCount;                                        // T :449 unique limit, 0 = none         F GetMaxCount()           :891
    uint32 Stackable;                                       // T :450 stack size                     F GetMaxStackSize()       :945
    uint32 ContainerSlots;                                  // T :451                               F GetContainerSlots()     :892
    _ItemStat ItemStat[MAX_ITEM_PROTO_STATS];               // T :452 stat type + value              F GetStatModifierBonusStat()/:893, GetItemStatValue (Item.cpp:2432)
    uint32 Delay;                                           // T :453 milliseconds                   F GetDelay()              :902
    float  RangedModRange;                                  // T :454 percent, ranged attack range   F GetRangedModRange()     :903
    _ItemDamage Damage[MAX_ITEM_PROTO_DAMAGES];             // T :456 per-school damage lines        F GetDamage()/:994, GetDamageType()/:901
    uint32 Block;                                           // T :457 shield block value             F ITEM_MOD_BLOCK_VALUE stat value
    int32 Armor;                                            // T :458 armor from item_template       F GetArmor(itemLevel)     :992
    int32 HolyRes;                                          // T :459 holy resistance granted        F ITEM_MOD_HOLY_RESISTANCE stat value
    int32 FireRes;                                          // T :460                               F ITEM_MOD_FIRE_RESISTANCE
    int32 NatureRes;                                        // T :461                               F ITEM_MOD_NATURE_RESISTANCE
    int32 FrostRes;                                         // T :462                               F ITEM_MOD_FROST_RESISTANCE
    int32 ShadowRes;                                        // T :463                               F ITEM_MOD_SHADOW_RESISTANCE
    int32 ArcaneRes;                                        // T :464                               F ITEM_MOD_ARCANE_RESISTANCE
    _ItemSpell Spells[MAX_ITEM_PROTO_SPELLS];               // T :465 on-use/on-equip spells         F Effects (ItemEffectEntry, DB2Structure.h:2435), ItemTemplate.h:926
    uint32 Bonding;                                         // T :466 0..5 (ItemBondingType)         F GetBonding()            :904
    uint32 StartQuest;                                      // T :470                               F GetStartQuest()         :907
    uint32 LockID;                                          // T :471                               F GetLockID()             :908
    uint32 ItemSet;                                         // T :475                               F GetItemSet()            :909
    uint32 MaxDurability;                                   // T :476                               F MaxDurability           :925
    uint32 Area;                                            // T :477 zone bound (first zone)        F GetArea(0)              :910
    uint32 Map;                                             // T :478 instance bound                 F GetMap()                :911
    uint32 Duration;                                        // T :479 seconds                        F GetDuration()           :918
    uint32 BagFamily;                                       // T :480 BAG_FAMILY_*                   F GetBagFamily()          :912

    // The old struct's helpers, same answers from the fields above (cmangos ItemPrototype.h:497-545).
    bool CanChangeEquipStateInCombat() const;
    uint32 GetMaxStackSize() const { return Stackable; }
    bool IsWeapon() const { return Class == ITEM_CLASS_WEAPON; }
    bool IsRangedWeapon() const
    {
        return IsWeapon() && (InventoryType == INVTYPE_RANGED || InventoryType == INVTYPE_THROWN || InventoryType == INVTYPE_RANGEDRIGHT);
    }
    bool IsConjuredConsumable() const { return Class == ITEM_CLASS_CONSUMABLE && (Flags & ITEM_FLAG_CONJURED); }
    uint32 GetProficiencySkill() const;
};

namespace tbcompat
{
    // The mirror of an item, built on first use and kept for the life of the process; nullptr when the core has no such item.
    // Safe from any thread (the bot brain runs on map threads).
    ItemPrototype const* ItemProto(uint32 entry);
    ItemPrototype const* ItemProto(ItemTemplate const* tpl);    // nullptr for nullptr
    ItemTemplate const* ItemTpl(ItemPrototype const* proto);   // back to the core's record
}
