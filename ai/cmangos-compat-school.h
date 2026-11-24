// Turtle side of the both-core helpers of the spell-school switch (issue #48). ForeverCore side: forever/src/compat/
// TbCompat_School.h, the reads that answer the switch.
//
// The switch ships OFF and is not built for this core: its kit gate (TortoiseBots.SpellSchool.Enable, default 0) reads false
// and the runtime read has no state to give, so every helper answers the "no reading" value the host already handles: no
// template immunity, no aura immunity, plain damage everywhere, no resolved spell. The host builds no index and `.tb school`
// reports `main=- pick=- src=none`; no behaviour changes here.
#pragma once

#include <cstdint>
#include <string>

namespace tbcompat
{
constexpr bool SchoolSwitchComputableCompat() { return false; }

inline std::uint32_t TemplateSchoolImmuneMaskCompat(void const*) { return 0; }
inline bool AuraSchoolImmuneCompat(void const*, int) { return false; }
inline int SchoolTakenPctCompat(void const*, int) { return 100; }
inline bool ResolveSpellByNameCompat(std::string const&, std::uint32_t& spellId, std::uint32_t& school)
{
    spellId = 0;
    school = 0;
    return false;
}
} // namespace tbcompat
