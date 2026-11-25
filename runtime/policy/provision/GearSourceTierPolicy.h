#pragma once

// Pure gate for the generated-gear source index (ForeverCore host unit
// forever/src/host/gear/TbGearSourceTier.cpp).
//
// Allowed = a known ordinary source (world drop, dungeon below the configured
// tier, vendor, quest reward, trainer/recipe craft) that is not
// reputation-gated, not PvP gear, not retired/unobtainable and not
// blacklisted. An item with no known source fails (custom, GM and unreleased
// rows must never reach generated gear); only an owner whitelist entry lets
// such an id through, and the whitelist still does not lift the tier, PvP or
// reputation rules (TB applies IsForbiddenBotGear and PassesSourceTier as two
// separate checks; this one gate keeps both).
//
// Tier meaning is TB's (ai/playerbot/RandomItemMgr.h:69-76): 0 = base
// world/vendor/quest/craft, 1 = end-game dungeon map, 2 = raid map / world
// boss / raid quest. AiPlayerbot.RandomGearMaxSourceTier is the highest tier
// that may be handed out.
//
// Core-free and unit-tested standalone (tools/test_gear_source_tier_policy.cpp).

#include "GearBlacklistPolicy.h"

#include <cstdint>

namespace TortoiseBots
{

enum GearSourceTier : uint8_t
{
    GEAR_SOURCE_TIER_BASE = 0,
    GEAR_SOURCE_TIER_DUNGEON = 1,
    GEAR_SOURCE_TIER_RAID = 2
};

struct GearSourceFacts
{
    bool knownSource = false;             // at least one of loot, vendor, quest reward, trainer/recipe craft
    uint8_t tier = GEAR_SOURCE_TIER_BASE; // lowest tier over every known source
    bool reputationGated = false;         // item row, rewarding quest or producing recipe needs reputation
    bool pvp = false;                     // honour/PvP flag, or a PvP set sibling
    bool blacklisted = false;             // AiPlayerbot.RandomGearBlacklist
    bool whitelisted = false;             // AiPlayerbot.RandomGearWhitelist
    bool retired = false;                 // deprecated flag or a test/retired name
    bool cosmeticSlot = false;            // shirt or tabard
    uint32_t itemLevel = 1;
    uint32_t quality = 0;
};

inline bool GearSourceGateAllows(GearSourceFacts const& facts, int32_t maxTier, bool allowReputation, bool allowPvP)
{
    if (facts.blacklisted)
        return false;
    if (!facts.whitelisted)
    {
        if (!facts.knownSource || facts.retired)
            return false;
        if (facts.itemLevel == 0 && !facts.cosmeticSlot)
            return false;
        if (facts.quality >= kGearQualityLegendary)
            return false;
    }
    int32_t const cap = maxTier < 0 ? 0 : maxTier;
    if (int32_t(facts.tier) > cap)
        return false;
    if (facts.reputationGated && !allowReputation)
        return false;
    if (facts.pvp && !allowPvP)
        return false;
    return true;
}

} // namespace TortoiseBots
