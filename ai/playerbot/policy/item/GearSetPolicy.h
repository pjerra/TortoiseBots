#pragma once

#include "GearReplacementsData.h"
#include "runtime/GearEffectiveLevelPolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ai
{

inline constexpr uint32_t kClassicItemMaxId = 25000;
// Forever items live in their own id range (the live table holds weapon and armour ids from 200000 to 291874).
inline constexpr uint32_t kForeverItemBandMinId = 200000;
inline constexpr uint32_t kForeverItemBandMaxId = 292000;

struct GearLevelBracket
{
    uint32_t maxRequiredLevel = 1;
    uint32_t minItemLevel = 1;
    uint32_t maxItemLevel = 100;
};

struct GearItemCandidate
{
    uint32_t itemId = 0;
    uint32_t requiredExpansion = 0;
    uint32_t randomBonusListTemplateId = 0;
    uint32_t allowableClass = 0xFFFFFFFFu;
    uint32_t allowableRace = 0xFFFFFFFFu;
    uint32_t score = 0;
    uint16_t itemLevel = 0;
    uint8_t requiredLevel = 0;
    uint8_t quality = 0;
    uint8_t inventoryType = 0;
    uint8_t itemClass = 0;
    uint8_t subClass = 0;
    bool unique = false;
};

// Id range generated gear may draw from: on Forever the classic table plus Forever's own item range (the live table
// holds weapon and armour ids from 200000 to 291874). The Turtle build keeps its classic-only rule.
inline bool IsItemInGearBand(uint32_t itemId)
{
    if (itemId == 0)
        return false;
    if (itemId < kClassicItemMaxId)
        return true;
#if defined(TORTOISEBOTS_CORE_FOREVER) && TORTOISEBOTS_CORE_FOREVER
    return itemId >= kForeverItemBandMinId && itemId < kForeverItemBandMaxId;
#else
    return false;
#endif
}

// Trap / missing / spell-not-item IDs < 25000 from forever/data/waterys/forever-item-traps.csv
// and items-missing-classified.csv.
inline bool IsTrapItem(uint32_t itemId)
{
    switch (itemId)
    {
    case 12536: // SPELL_NOT_ITEM
    case 16922: // MISSING on Forever
    case 17802: // Thunderfury DEPRECATED duplicate (use 19019)
    case 18803: // MISSING on Forever
    case 21927: // SPELL_NOT_ITEM
    case 22053: // SPELL_NOT_ITEM
    case 22054: // SPELL_NOT_ITEM
        return true;
    default:
        return false;
    }
}

inline bool IsClassicItemCandidate(uint32_t itemId,
                                   uint32_t requiredExpansion,
                                   uint8_t quality,
                                   uint32_t randomBonusListTemplateId)
{
    if (itemId == 0)
        return false;
    if (requiredExpansion != 0)
        return false;
    if (!IsItemInGearBand(itemId))
        return false;
    if (quality > 4) // 4 = Epic
        return false;
    if (IsTrapItem(itemId))
        return false;
    if (randomBonusListTemplateId != 0)
        return false;
    return true;
}

inline uint8_t TargetMaxQualityForLevel(uint32_t level, int32_t requestedQuality = -1)
{
    if (requestedQuality >= 0)
        return static_cast<uint8_t>(std::min<int32_t>(requestedQuality, 4));
    return (level < 30) ? 2 : 3; // Uncommon below 30, Rare from 30, Epic only with quality=
}

inline GearLevelBracket LevelBracketForLevel(uint32_t level)
{
    uint32_t const clamped = std::clamp<uint32_t>(level, 1, 60);
    if (clamped >= 60)
        return { 60, 50, 100 };
    uint32_t const minIlvl = (clamped > 12) ? (clamped - 12) : 1;
    uint32_t const maxIlvl = clamped + 6;
    return { clamped, minIlvl, maxIlvl };
}

inline std::array<uint8_t, 17> const& GearSetSlotOrder()
{
    static constexpr std::array<uint8_t, 17> kSlots = {
        0,  // HEAD
        1,  // NECK
        2,  // SHOULDERS
        4,  // CHEST
        5,  // WAIST
        6,  // LEGS
        7,  // FEET
        8,  // WRISTS
        9,  // HANDS
        10, // FINGER1
        11, // FINGER2
        12, // TRINKET1
        13, // TRINKET2
        14, // BACK
        15, // MAINHAND
        16, // OFFHAND
        17  // RANGED
    };
    return kSlots;
}

inline bool InventoryTypeFitsSlot(uint8_t invType, uint8_t slot)
{
    switch (slot)
    {
    case 0:  return invType == 1;                                              // HEAD
    case 1:  return invType == 2;                                              // NECK
    case 2:  return invType == 3;                                              // SHOULDERS
    case 4:  return invType == 5 || invType == 20;                             // CHEST / ROBE
    case 5:  return invType == 6;                                              // WAIST
    case 6:  return invType == 7;                                              // LEGS
    case 7:  return invType == 8;                                              // FEET
    case 8:  return invType == 9;                                              // WRISTS
    case 9:  return invType == 10;                                             // HANDS
    case 10:
    case 11: return invType == 11;                                             // FINGER
    case 12:
    case 13: return invType == 12;                                             // TRINKET
    case 14: return invType == 16;                                             // CLOAK
    case 15: return invType == 13 || invType == 17 || invType == 21;           // 1H / 2H / MAINHAND
    case 16: return invType == 13 || invType == 14 || invType == 22 || invType == 23; // 1H / SHIELD / OH / HOLDABLE
    case 17: return invType == 15 || invType == 25 || invType == 26 || invType == 28; // BOW / THROWN / RANGEDRIGHT / RELIC
    default: return false;
    }
}

inline std::string CanonicalSpecName(uint8_t playerClass, std::string_view rawSpec)
{
    std::string s;
    s.reserve(rawSpec.size());
    for (char c : rawSpec)
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    switch (playerClass)
    {
    case 1: // Warrior
        if (s == "protection" || s == "prot") return "prot";
        if (s == "arms" || s == "fury") return s;
        break;
    case 2: // Paladin
        if (s == "protection" || s == "prot") return "prot";
        if (s == "retribution" || s == "retrib" || s == "ret") return "retrib";
        if (s == "holy") return "holy";
        break;
    case 3: // Hunter
        if (s == "beastmastery" || s == "beast" || s == "bm") return "beast";
        if (s == "marksmanship" || s == "marks" || s == "mm") return "marks";
        if (s == "survival" || s == "surv") return "surv";
        break;
    case 4: // Rogue
        if (s == "assassination" || s == "assas") return "assas";
        if (s == "subtlety" || s == "subtle" || s == "sub") return "subtle";
        if (s == "combat") return "combat";
        break;
    case 5: // Priest
        if (s == "discipline" || s == "disc") return "disc";
        if (s == "holy" || s == "shadow") return s;
        break;
    case 7: // Shaman
        if (s == "elemental" || s == "elem") return "elem";
        if (s == "enhancement" || s == "enhance" || s == "enh") return "enhance";
        if (s == "restoration" || s == "resto") return "resto";
        if (s == "tank" || s == "shamantank") return "shamantank";
        break;
    case 8: // Mage
        if (s == "arcane" || s == "fire" || s == "frost") return s;
        break;
    case 9: // Warlock
        if (s == "affliction" || s == "afflic" || s == "aff") return "afflic";
        if (s == "demonology" || s == "demo") return "demo";
        if (s == "destruction" || s == "destro") return "destro";
        break;
    case 11: // Druid
        if (s == "balance" || s == "boomkin") return "balance";
        if (s == "bear" || s == "feraltank" || s == "tank") return "feraltank";
        if (s == "cat" || s == "feral" || s == "feraldps") return "feraldps";
        if (s == "restoration" || s == "resto") return "resto";
        break;
    default:
        break;
    }
    return s;
}

inline bool ClassCanWearArmorSubClass(uint8_t playerClass, std::string_view specName, uint32_t level, uint8_t slot, uint8_t subClass)
{
    if (slot == 14) // CLOAK
        return subClass == 1; // CLOTH
    if (slot == 1 || slot == 10 || slot == 11 || slot == 12 || slot == 13) // NECK / FINGER / TRINKET
        return subClass == 0; // MISC
    if (slot == 16 && subClass == 6) // SHIELD
        return playerClass == 1 || playerClass == 2 || playerClass == 7;
    if (slot == 16) // HOLDABLE (frill offhand)
        return subClass == 0;
    if (slot == 17) // RELIC
    {
        if (playerClass == 2) return subClass == 7;  // LIBRAM
        if (playerClass == 11) return subClass == 8; // IDOL
        if (playerClass == 7) return subClass == 9;  // TOTEM
        return false;
    }

    bool const isCasterSpec =
        (playerClass == 2 && specName == "holy") ||
        (playerClass == 7 && (specName == "elem" || specName == "resto")) ||
        (playerClass == 11 && (specName == "balance" || specName == "resto"));

    switch (playerClass)
    {
    case 5: // Priest
    case 8: // Mage
    case 9: // Warlock
        return subClass == 1; // CLOTH
    case 4: // Rogue
        return subClass == 2; // LEATHER
    case 11: // Druid
        return subClass == 2 || (isCasterSpec && subClass == 1);
    case 3: // Hunter
        return (level >= 40) ? (subClass == 3 || subClass == 2) : (subClass == 2);
    case 7: // Shaman
        if (level >= 40)
            return subClass == 3 || (isCasterSpec && (subClass == 2 || subClass == 1));
        return subClass == 2 || (isCasterSpec && subClass == 1);
    case 1: // Warrior
        return (level >= 40) ? (subClass == 4) : (subClass == 3);
    case 2: // Paladin
        if (level >= 40)
            return subClass == 4 || (isCasterSpec && (subClass == 3 || subClass == 2 || subClass == 1));
        return subClass == 3 || (isCasterSpec && (subClass == 2 || subClass == 1));
    default:
        return false;
    }
}

inline bool ClassCanWearWeaponForSlot(uint8_t playerClass, std::string_view specName, uint8_t slot, uint8_t invType, uint8_t subClass, bool canDualWield)
{
    if (slot == 16 && invType != 14 && invType != 23) // Off-hand weapon
    {
        if (!canDualWield)
            return false;
        if (playerClass != 1 && playerClass != 3 && playerClass != 4)
            return false;
    }

    if (slot == 15 && invType == 17) // 2H weapon
    {
        if (playerClass == 4)
            return false;
        if (playerClass == 1 && specName == "prot")
            return false;
        if (playerClass == 2 && specName == "prot")
            return false;
        if (playerClass == 7 && specName == "shamantank")
            return false;
    }

    if (slot == 17) // Ranged weapon
    {
        if (playerClass == 5 || playerClass == 8 || playerClass == 9)
            return subClass == 19; // WAND
        if (playerClass == 3)
            return subClass == 2 || subClass == 3 || subClass == 18; // BOW / GUN / CROSSBOW
        if (playerClass == 1 || playerClass == 4)
            return subClass == 2 || subClass == 3 || subClass == 18 || subClass == 16; // + THROWN
        return false;
    }

    switch (playerClass)
    {
    case 1: // Warrior
        return subClass == 0 || subClass == 1 || subClass == 4 || subClass == 5 ||
               subClass == 6 || subClass == 7 || subClass == 8 || subClass == 13 || subClass == 15;
    case 2: // Paladin
        return subClass == 0 || subClass == 1 || subClass == 4 || subClass == 5 ||
               subClass == 6 || subClass == 7 || subClass == 8;
    case 3: // Hunter
        return subClass == 0 || subClass == 1 || subClass == 6 || subClass == 7 ||
               subClass == 8 || subClass == 10 || subClass == 13 || subClass == 15;
    case 4: // Rogue
        return subClass == 4 || subClass == 7 || subClass == 13 || subClass == 15;
    case 5: // Priest
        return subClass == 4 || subClass == 10 || subClass == 15;
    case 7: // Shaman
        return subClass == 0 || subClass == 1 || subClass == 4 || subClass == 5 ||
               subClass == 10 || subClass == 13 || subClass == 15;
    case 8: // Mage
    case 9: // Warlock
        return subClass == 7 || subClass == 10 || subClass == 15;
    case 11: // Druid
        return subClass == 4 || subClass == 5 || subClass == 10 || subClass == 13 || subClass == 15;
    default:
        return false;
    }
}

inline bool CandidateFitsSlotPolicy(GearItemCandidate const& c,
                                    uint8_t playerClass,
                                    std::string_view rawSpec,
                                    uint32_t level,
                                    uint8_t slot,
                                    uint8_t maxQuality,
                                    bool canDualWield,
                                    bool mainHandIsTwoHanded)
{
    if (slot == 16 && mainHandIsTwoHanded)
        return false;

    if (!IsClassicItemCandidate(c.itemId, c.requiredExpansion, c.quality, c.randomBonusListTemplateId))
        return false;

    if (c.quality > maxQuality)
        return false;

    GearLevelBracket const bracket = LevelBracketForLevel(level);
#if defined(TORTOISEBOTS_CORE_FOREVER) && TORTOISEBOTS_CORE_FOREVER
    // Many Forever rows store RequiredLevel 0; the effective level is derived from the item level instead.
    if (TortoiseBots::GearEffectiveRequiredLevel(c.requiredLevel, c.itemLevel) > bracket.maxRequiredLevel)
        return false;
#else
    if (c.requiredLevel > bracket.maxRequiredLevel)
        return false;
#endif
    if (c.itemLevel < bracket.minItemLevel || c.itemLevel > bracket.maxItemLevel)
        return false;

    if (c.allowableClass != 0 && (c.allowableClass & (1u << (playerClass - 1))) == 0)
        return false;

    if (!InventoryTypeFitsSlot(c.inventoryType, slot))
        return false;

    std::string const spec = CanonicalSpecName(playerClass, rawSpec);
    if (c.itemClass == 4) // ARMOR
        return ClassCanWearArmorSubClass(playerClass, spec, level, slot, c.subClass);
    if (c.itemClass == 2) // WEAPON
        return ClassCanWearWeaponForSlot(playerClass, spec, slot, c.inventoryType, c.subClass, canDualWield);
    return false;
}

inline std::vector<uint32_t> PreferredReplacementsFor(uint8_t playerClass, std::string_view rawSpec, uint8_t slot)
{
    std::string const spec = CanonicalSpecName(playerClass, rawSpec);
    std::vector<uint32_t> out;
    for (GearReplacementEntry const& entry : kGearReplacements)
    {
        if (entry.playerClass == playerClass && entry.slot == slot && spec == entry.spec)
        {
            for (uint32_t id : entry.itemIds)
                if (id != 0)
                    out.push_back(id);
            break;
        }
    }
    return out;
}

inline std::vector<GearItemCandidate> RankCandidatesForSlot(std::vector<GearItemCandidate> const& pool,
                                                            uint8_t playerClass,
                                                            std::string_view rawSpec,
                                                            uint32_t level,
                                                            uint8_t slot,
                                                            uint8_t maxQuality,
                                                            bool canDualWield,
                                                            bool mainHandIsTwoHanded,
                                                            std::vector<uint32_t> const& alreadyPickedIds = {})
{
    std::vector<GearItemCandidate> out;
    for (GearItemCandidate const& c : pool)
    {
        if (!CandidateFitsSlotPolicy(c, playerClass, rawSpec, level, slot, maxQuality, canDualWield, mainHandIsTwoHanded))
            continue;
        if (std::find(alreadyPickedIds.begin(), alreadyPickedIds.end(), c.itemId) != alreadyPickedIds.end())
            continue;
        out.push_back(c);
    }

    std::sort(out.begin(), out.end(), [](GearItemCandidate const& a, GearItemCandidate const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.quality != b.quality)
            return a.quality > b.quality;
        if (a.itemLevel != b.itemLevel)
            return a.itemLevel > b.itemLevel;
        return a.itemId < b.itemId;
    });
    return out;
}

struct CreatedItemCleanup
{
    bool destroyByGuid = false;
    uint64_t destroyGuid = 0;
    uint32_t destroyEntryCount = 0;
};

inline CreatedItemCleanup DecideFailedSwapCleanup(uint64_t createdInstanceGuid, uint32_t /*itemId*/)
{
    if (createdInstanceGuid == 0)
        return { false, 0, 0 };
    return { true, createdInstanceGuid, 0 };
}

inline std::vector<GearItemCandidate> FilterCandidatesForSlot(std::vector<GearItemCandidate> const& pool,
                                                              uint8_t playerClass,
                                                              std::string_view rawSpec,
                                                              uint32_t level,
                                                              uint8_t slot,
                                                              uint8_t maxQuality,
                                                              bool canDualWield,
                                                              bool mainHandIsTwoHanded,
                                                              std::vector<uint32_t> const& alreadyPickedIds = {})
{
    std::vector<GearItemCandidate> out;
    for (GearItemCandidate const& c : pool)
    {
        if (!CandidateFitsSlotPolicy(c, playerClass, rawSpec, level, slot, maxQuality, canDualWield, mainHandIsTwoHanded))
            continue;
        if (std::find(alreadyPickedIds.begin(), alreadyPickedIds.end(), c.itemId) != alreadyPickedIds.end())
            continue;
        out.push_back(c);
    }
    return out;
}

inline void SortCandidatesForSlot(std::vector<GearItemCandidate>& out)
{
    std::sort(out.begin(), out.end(), [](GearItemCandidate const& a, GearItemCandidate const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.quality != b.quality)
            return a.quality > b.quality;
        if (a.itemLevel != b.itemLevel)
            return a.itemLevel > b.itemLevel;
        return a.itemId < b.itemId;
    });
}

} // namespace ai
