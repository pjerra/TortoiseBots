// The level-60 gear enchant table (`forever/sql/world/2026_11_25_00_tortoisebots_spec_enchant.sql`): per class,
// spec and equipment slot, the enchant Forever players of that spec commonly wear at 60, or that they leave the
// slot empty. The rows are read once at world start; the lookup is a map read on the world thread.
//
// The rule is in host/TbSpecEnchantPure.h. The table is loaded with the other gear tables (TbScripts.cpp, after the
// gear index); a server without the file leaves every slot at "no rule".
#pragma once

#include "host/TbSpecEnchantPure.h"

#include <cstdint>
#include <string_view>

namespace TortoiseBots
{
namespace enchant
{

// One read of `ai_playerbot_spec_enchant`. Startup only; a missing table or column logs once and leaves no rule.
void LoadSpecEnchants();

// The table's answer for one worn item. `specName` is the canonical weight-scale
// spec name (the string GearSetOutcome::specName holds: "arms", "prot",
// "feraltank", ...), `equipSlot` the core EquipmentSlots value and `itemQuality`
// the core ItemQuality value of the item.
SpecEnchantDecision SpecEnchantFor(std::uint8_t classId, std::string_view specName,
    std::uint8_t equipSlot, std::uint32_t level, std::uint8_t itemQuality);

} // namespace enchant
} // namespace TortoiseBots
