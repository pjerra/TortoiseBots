#pragma once

// Pure choice of the spell school a caster bot should cast with (issue #48, round 1): the main school of its spec unless the
// target is immune to it or takes so much more from another school that the switch pays.
//
// No core includes: the host translates a target into these plain numbers (forever/src/host/TbSchool.cpp), so the rule stays
// testable in forever/tools/test_spell_school_choice.cpp without the server.
//
// School indexes are the core's SpellEntry::School, the same seven `school-nukes.csv` and the creature masks use (normal 0,
// holy 1, fire 2, nature 3, frost 4, shadow 5, arcane 6). `takenPct` is the damage the target takes from that school as a
// percent: 100 is plain damage, 150 is half again (the Death Talon aura). `immuneMask` has bit `1 << school` set for every
// school the target is immune to (the template's row, the core's own immunity read, and the data row).
//
// The rule, decided once:
//   * the main school immune -> the known, non-immune school that takes the most (ties: the lower school index, which is the
//     order `school-nukes.csv` lists a spec's fallbacks in; `test_gen_data_tables.py` pins that order);
//   * otherwise a known school that takes at least `minGainPct` more than the main one (>=, and the main school's own
//     damage-taken auras raise the bar with it);
//   * no such school -> stay (-1). A school outside `knownMask` is never picked, and the main school is never "picked" for
//     itself.
// Healers need no case here: their specs have no `school-nukes.csv` rows, so their main school is unknown.

#include <cstdint>

namespace ai
{
    constexpr int kSchoolCount = 7;
    constexpr int kSchoolStay = -1;

    struct SchoolState
    {
        std::uint8_t immuneMask = 0;
        std::int16_t takenPct[kSchoolCount] = {};
    };

    // The kit spec name of a class's talent tab, the same words the ForeverCore factory adds as the class strategy
    // (forever/src/slice/SliceAiFactory.cpp, AddDefaultCombatStrategies). The five caster kits have rows in
    // `school-nukes.csv`; a healer or melee spec has none, so its name never meets a row.
    constexpr int kClassPriest = 5, kClassShaman = 7, kClassMage = 8, kClassWarlock = 9, kClassDruid = 11;

    inline char const* SpecNameOf(int classId, int specTab)
    {
        switch (classId)
        {
            case kClassMage: return specTab == 0 ? "arcane" : (specTab == 2 ? "frost" : "fire");
            case kClassWarlock: return specTab == 1 ? "demonology" : (specTab == 2 ? "destruction" : "affliction");
            case kClassPriest: return specTab == 2 ? "shadow" : (specTab == 1 ? "holy" : "discipline");
            case kClassDruid: return specTab == 1 ? "feral" : (specTab == 2 ? "restoration" : "balance");
            case kClassShaman: return specTab == 1 ? "enhancement" : (specTab == 2 ? "restoration" : "elemental");
            default: return "";
        }
    }

    inline char const* SchoolWord(int school)
    {
        switch (school)
        {
            case 0: return "normal";
            case 1: return "holy";
            case 2: return "fire";
            case 3: return "nature";
            case 4: return "frost";
            case 5: return "shadow";
            case 6: return "arcane";
            default: return "-";
        }
    }

    // The school to cast instead of `mainSchool`, or kSchoolStay.
    inline int PickSchool(SchoolState const& state, std::uint8_t knownMask, int mainSchool, int minGainPct)
    {
        if (mainSchool < 0 || mainSchool >= kSchoolCount)
            return kSchoolStay;

        std::uint32_t const immuneMask = state.immuneMask;
        bool const mainImmune = (immuneMask & (1u << mainSchool)) != 0;
        int const mainTaken = state.takenPct[mainSchool];

        int best = kSchoolStay;
        int bestTaken = 0;
        for (int school = 0; school < kSchoolCount; ++school)
        {
            if (school == mainSchool || (knownMask & (1u << school)) == 0)
                continue;
            if (immuneMask & (1u << school))
                continue;
            int const taken = state.takenPct[school];
            // The main school's immunity makes the switch a necessity: no gain needed beyond "works at all".
            if (!mainImmune && taken < mainTaken + minGainPct)
                continue;
            // Strictly more, so an equal percent keeps the earlier school: the row order of school-nukes.csv (which lists a
            // spec's fallback schools by increasing index).
            if (best == kSchoolStay || taken > bestTaken)
            {
                best = school;
                bestTaken = taken;
            }
        }
        return best;
    }
} // namespace ai
