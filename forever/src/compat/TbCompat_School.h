// ForeverCore side of the both-core helpers of the spell-school switch (issue #48). Turtle side: ai/cmangos-compat-school.h,
// where the switch is off and every read is inert.
//
// The host (forever/src/host/TbSchool.cpp) needs three answers from the core, one per immunity source, and one name lookup:
//
//   * the template mask: the creature's CreatureImmunities row, rebuilt into `1 << school` bits (the same read as
//     CreatureSchoolImmuneMaskCompat, TbCompat_Unported.h:322, which the damage rotation's own immunity gate uses; written out
//     here because that header needs the slice's pre-included core set, and this host file compiles in both builds);
//   * the aura read: Unit::IsImmunedToDamage(SpellSchoolMask) (Unit.h:1745), the core's own "this school does nothing to it",
//     which also covers an aura applied after the template (Ice Block, a scripted immunity) and the template row again;
//   * the damage-taken multiplier: Unit::GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, 1 << school)
//     (Unit.h:1436), the exact factor Unit::CalculateDamageTaken multiplies by (Unit.cpp:7517), so the Death Talon frost
//     vulnerability (spells 22277-22281, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN for frost) is the number the bot compares.
//
// The name lookup is the only place a CSV row is turned into an id: the spell store is scanned once per wanted name (the
// table is 22 names, at OnWorldInitialized only) and the id handed to the mirror, whose `School` field is what the row is
// compared against (never a guess from the name).
#pragma once

#include "Creature.h"
#include "CreatureData.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include "TbSpellEntry.h"        // SpellProto: the mirror the school compares against

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tbcompat
{
// False on a core whose reads cannot answer the switch; the host then builds no index and `.tb school` reports off.
inline bool SchoolSwitchComputableCompat() { return true; }

// The creature template's school immunity: 0 for a unit that is not a creature, has no template or has no row.
inline std::uint32_t TemplateSchoolImmuneMaskCompat(Unit const* target)
{
    if (!target || !target->IsCreature())
        return 0;
    // The core's own names: this header is read by files that never see the compat shim's `CreatureInfo`/`GetCreatureInfo`.
    CreatureTemplate const* const info = static_cast<Creature const*>(target)->GetCreatureTemplate();
    CreatureImmunities const* const row = info ? SpellMgr::GetCreatureImmunities(info->CreatureImmunitiesId) : nullptr;
    if (!row)
        return 0;
    std::uint32_t mask = 0;
    for (std::size_t school = 0; school < row->School.size() && school < 7; ++school)
        if (row->School[school])
            mask |= 1u << school;
    return mask;
}

// The core's own immunity answer for one school (its auras and its template row).
inline bool AuraSchoolImmuneCompat(Unit const* target, int school)
{
    if (!target || school < 0 || school >= 7)
        return false;
    return target->IsImmunedToDamage(SpellSchoolMask(1u << school));
}

// 100 * the core's damage-taken multiplier for one school, rounded and kept inside an int16 (100 = plain damage).
inline int SchoolTakenPctCompat(Unit const* target, int school)
{
    if (!target || school < 0 || school >= 7)
        return 100;
    float const multiplier = target->GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, 1u << school);
    long const pct = std::lround(100.0f * multiplier);
    if (pct < 0)
        return 0;
    if (pct > 32767)
        return 32767;
    return int(pct);
}

// Resolve a spell name to the id of the first (lowest) spell of that name and the school the MIRROR has for it. False when
// no spell carries the name.
inline bool ResolveSpellByNameCompat(std::string const& name, std::uint32_t& spellId, std::uint32_t& school)
{
    spellId = 0;
    school = 0;
    if (name.empty() || !sSpellMgr)
        return false;
    std::uint32_t best = 0;
    sSpellMgr->ForEachSpellInfo([&name, &best](SpellInfo const* info)
    {
        if (best || !info || !info->SpellName)
            return;
        char const* const text = (*info->SpellName)[LOCALE_enUS];
        if (text && name == text)
            best = info->Id;
    });
    if (!best)
        return false;
    SpellEntry const* const entry = SpellProto(best);
    if (!entry)
        return false;
    spellId = entry->Id;
    school = entry->School;
    return true;
}
} // namespace tbcompat
