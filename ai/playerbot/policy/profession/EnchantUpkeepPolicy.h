#pragma once

// Free-bot enchanting upkeep: break down disenchantable junk a bot carries
// and apply a better permanent enchant to the gear it wears. The host tick in
// forever/src/host/TbHostProfessionUpkeep.cpp resolves every fact; the rules
// stay here, core-free, and are tested standalone
// (forever/tools/test_enchant_upkeep_policy.cpp).
//
// Core enum values are mirrored as documented literals so this header never
// includes a core header: item class 2 = weapon, 4 = armor; bonding 1 =
// bound on acquire; quality 2 = uncommon.

#include <cstdint>

namespace TortoiseBots
{
namespace prof
{
namespace upkeep
{

// A bound-on-acquire item can be neither sold nor mailed, so it is kept; the
// profession breaks down unbound armor and weapons at uncommon and above.
inline bool DisenchantCandidateClass(std::uint32_t itemClass, std::uint32_t bonding, std::uint32_t quality)
{
    return (itemClass == 2 || itemClass == 4) && bonding != 1 && quality >= 2;
}

// Next to a real player (a master or a guild with real members) rare and
// better items are kept; an all-bot world breaks them down too.
inline bool DisenchantQualityAllowed(std::uint32_t quality, bool sharesWithRealPlayer)
{
    return !sharesWithRealPlayer || quality <= 2;
}

struct DisenchantUpkeepFacts
{
    std::uint32_t itemClass = 0;
    std::uint32_t bonding = 0;
    std::uint32_t quality = 0;
    bool hasLoot = false;          // Item::GetDisenchantLootId answered
    std::uint16_t requiredSkill = 0; // Item::GetDisenchantSkillRequired, 0 when none
    std::uint16_t botSkill = 0;
    bool protectedItem = false;    // reserved by a guild order or the mail service
    bool sharesWithRealPlayer = false;
};

inline bool DisenchantUpkeepWants(DisenchantUpkeepFacts const& f)
{
    if (f.protectedItem || !f.hasLoot)
        return false;
    if (!DisenchantCandidateClass(f.itemClass, f.bonding, f.quality))
        return false;
    if (!DisenchantQualityAllowed(f.quality, f.sharesWithRealPlayer))
        return false;
    return f.botSkill >= f.requiredSkill;
}

struct EnchantUpkeepFacts
{
    bool spellTakesReagents = false; // the catalogue row carries at least one reagent
    bool hasReagents = false;        // every reagent and totem item is in the bags
    bool protectedReagents = false;  // one of them is reserved by a guild order or the mail service
    std::uint32_t currentWeight = 0;
    std::uint32_t newWeight = 0;
};

// Only a strictly better enchant replaces the one already on the item; an
// unenchanted item weighs 0, so the same rule covers it.
inline bool EnchantUpkeepWants(EnchantUpkeepFacts const& f)
{
    if (!f.spellTakesReagents || !f.hasReagents || f.protectedReagents)
        return false;
    return f.newWeight > f.currentWeight;
}

// Recipes the profession grants as the skill grows: a catalogue row with a
// known skill requirement the bot's skill covers. A zero requirement names no
// trainer or recipe, so such a row is never granted.
inline bool EnchantRecipeGrantable(std::uint32_t reqSkill, std::uint32_t skill)
{
    return reqSkill > 0 && reqSkill <= skill;
}

} // namespace upkeep
} // namespace prof
} // namespace TortoiseBots
