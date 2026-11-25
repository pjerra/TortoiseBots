// The level-60 gear enchant rule, core-free so a unit test proves every band
// (forever/tools/test_spec_enchant_rules.cpp). The table reader is
// host/TbSpecEnchant.cpp; here live what a row means and when it applies.
//
// The rows come from the armory sample of level-60 Forever characters: per
// class, spec and equipment slot, the enchant the best-geared players of that
// spec wear, or that they leave the slot empty. The sample covers level 60
// only, so below 60 the table says nothing and the ordinary picker keeps
// deciding.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace enchant
{

// Core ItemQuality values mirrored (0 poor, 1 normal, 2 uncommon, 3 rare,
// 4 epic); the header never includes a core header.
inline constexpr std::uint8_t kQualityPoor = 0;
inline constexpr std::uint8_t kQualityNormal = 1;
inline constexpr std::uint8_t kQualityUncommon = 2;

// The only level the sample covers.
inline constexpr std::uint32_t kSpecEnchantLevel = 60;

enum class SpecEnchantVerdict : std::uint8_t
{
    NoRule,     // the table does not name this spec and slot
    LeaveEmpty, // the table names the slot, and it stays without an enchant
    Apply,      // the table's choice applies to this item
};

// One table row as the reader hands it over.
struct SpecEnchantRule
{
    bool present = false;      // the table names this spec and slot
    std::uint32_t spellId = 0; // the choice; 0 = players leave the slot empty
    std::uint8_t minLevel = 0; // the choice's own level requirement
};

struct SpecEnchantDecision
{
    SpecEnchantVerdict verdict = SpecEnchantVerdict::NoRule;
    std::uint32_t spellId = 0;
};

// The level margin the ordinary picker applies by item quality: a white item
// takes only a choice whose requirement sits well below the bot, a green one
// a little below, blue and better the choice itself.
inline std::uint32_t SpecEnchantLevelMargin(std::uint8_t quality)
{
    if (quality == kQualityNormal)
        return 10;
    if (quality == kQualityUncommon)
        return 5;
    return 0;
}

// A named slot stays the table's, including when the item cannot carry the
// choice: no fallback enchant replaces it. That keeps one rule for the slot
// (the player choice, or none) instead of a per-item mix, and it never puts a
// level-60-only enchant on gear the ordinary quality ceiling would refuse.
inline SpecEnchantDecision DecideSpecEnchant(SpecEnchantRule const& rule, std::uint32_t level, std::uint8_t quality)
{
    if (!rule.present || level < kSpecEnchantLevel)
        return SpecEnchantDecision{};
    if (quality == kQualityPoor || rule.spellId == 0)
        return SpecEnchantDecision{ SpecEnchantVerdict::LeaveEmpty, 0 };
    if (std::uint32_t(rule.minLevel) + SpecEnchantLevelMargin(quality) > level)
        return SpecEnchantDecision{ SpecEnchantVerdict::LeaveEmpty, 0 };
    return SpecEnchantDecision{ SpecEnchantVerdict::Apply, rule.spellId };
}

} // namespace enchant
} // namespace TortoiseBots
