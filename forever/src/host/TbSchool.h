// #48 round 1: the runtime of the spell-school switch. The decision itself is pure (ai/playerbot/policy/combat/
// SpellSchoolChoicePolicy.h, forever/tools/test_spell_school_choice.cpp); this file turns a target and a bot into its inputs and
// the data files into an index.
//
// What the target contributes (per school): the creature template's immunity mask, the core's own immunity read and the
// damage-taken multiplier (the Death Talon vulnerability), each through the compat header; the data row of
// creature-schools.csv adds an immunity bit and, where the core shows nothing (plain 100), 50 points of damage taken.
//
// What the bot contributes: the schools its class and spec can cast, from school-nukes.csv (class + the kit spec name of its
// talent tab). A spec with no rows (every healer and melee spec) has no main school, so it never switches.
//
// TortoiseBots.SpellSchool.Enable (default 0) gates everything: off means no index of resolved spells, no pick, and no change
// to any bot. TortoiseBots.SpellSchool.VulnerableMinPct (default 10) is the gain the switch needs. Both are read by
// LoadConfig from the world's OnConfigLoad list.
//
// Threading: world thread only (OnConfigLoad, OnWorldInitialized, the `.tb school` console/GM command). The index is an
// immutable shared_ptr swapped under a mutex, so a future reader on a map thread sees a whole index or none.
#pragma once

#include "ai/playerbot/policy/combat/SpellSchoolChoicePolicy.h"   // the repository root is on the include path in both builds

#include <cstdint>
#include <string>

class Player;
class Unit;

namespace TortoiseBots
{
namespace school
{
// The two keys. Safe from any thread (atomics; both are read from the config load's thread-set at startup and a reload).
void LoadConfig();
bool Enabled();
int MinGainPct();

// World thread, once the spell store is loaded (the module's OnWorldInitialized, after tbcompat::InitSpellTable). Off: only the
// spec skeleton from the CSV (no spell is looked up, so `.tb school` still names a bot's main school). On: every spell name is
// resolved in the mirror and its school compared against the row; a name the store lacks or a school that differs is logged
// and that row is skipped. Also called on a config reload that turns the switch on.
void BuildIndex();

// The schools one bot can cast, from school-nukes.csv: its spec's name, the main school (order 0) and a bit per known school.
struct SpecSchools
{
    std::string spec;
    int mainSchool = -1;
    std::uint8_t knownMask = 0;
};

// `specTab` is the bot's own talent tab (AiFactory::GetPlayerSpecTab at the call site: the engine header that answers it needs
// the legacy pre-included core set, which this file deliberately does not).
SpecSchools SchoolsOf(Player const* bot, int specTab);

// One target's immunity and damage taken per school, and where the statement came from.
struct Reading
{
    ai::SchoolState state{};
    bool coreReading = false;   // the template mask, the core's immunity read or a damage-taken aura said something
    bool dataRow = false;       // creature-schools.csv has a row for the target's entry
};

Reading Read(Unit const* target);
ai::SchoolState StateOf(Unit const* target);

// The school to cast instead of the spec's main one against this target, or ai::kSchoolStay. kSchoolStay too while the switch
// is off, with no target, or for a spec without rows.
int PickFor(SpecSchools const& spec, Unit const* target);
}
}
