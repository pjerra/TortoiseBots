// Standalone test of the pure spell-school choice policy (ai/playerbot/policy/combat/SpellSchoolChoicePolicy.h; issue #48,
// school-switch round 1):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_spell_school_choice.cpp -o /tmp/test_spell_school_choice && /tmp/test_spell_school_choice
//
// The school indexes are the core's SpellEntry::School (normal 0, holy 1, fire 2, nature 3, frost 4, shadow 5, arcane 6); a
// taken percent of 100 is plain damage, 150 is half again as much (the Death Talon frost vulnerability).
#include "ai/playerbot/policy/combat/SpellSchoolChoicePolicy.h"

#include <cstdint>
#include <cstdio>

namespace P = ai;

namespace
{
int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

constexpr int Fire = 2, Frost = 4, Shadow = 5, Arcane = 6;
constexpr int NotSchool = -1;

std::uint8_t Bit(int school)
{
    return std::uint8_t(1u << school);
}

// Every school takes plain damage: the state of a creature with no immunity and no damage-taken aura.
P::SchoolState Plain()
{
    P::SchoolState state{};
    for (int school = 0; school < 7; ++school)
        state.takenPct[school] = 100;
    return state;
}

// The mage fire kit: fire is the main school, frost and arcane are the fallbacks.
constexpr std::uint8_t MageFire = 0x40 | 0x10 | 0x04;   // arcane | frost | fire

void MainImmuneSwitchesToBestKnown()
{
    // A template-immune fire mage must not stand idle on its main school: any known school the target takes is better, and
    // with every school equal the first fallback row (frost, the lower school index) wins.
    P::SchoolState state = Plain();
    state.immuneMask = Bit(Fire);
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Frost);

    // The best known fallback wins over the row order when it takes more damage.
    state.takenPct[Arcane] = 120;
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Arcane);
}

void VulnerableBelowGainStays()
{
    // The Death Talon case: one point below the gain the Fire mage keeps Fireball (the switch costs the main school's
    // talents and cooldowns for a gain too small to be worth it).
    P::SchoolState state = Plain();
    state.takenPct[Frost] = 109;
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == NotSchool);
}

void VulnerableAtGainSwitches()
{
    // At exactly the gain the switch happens (>=, not >).
    P::SchoolState state = Plain();
    state.takenPct[Frost] = 110;
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Frost);
}

void AllKnownImmuneStays()
{
    // Nothing to switch to: a frost mage on a frost- and fire-immune target stays (the rotation falls through on its own).
    P::SchoolState state = Plain();
    state.immuneMask = Bit(Fire) | Bit(Frost) | Bit(Arcane);
    CHECK(P::PickSchool(state, MageFire, Frost, 10) == NotSchool);

    // A creature immune to every school the bot's spec knows: still no pick, and no school outside the mask is ever picked.
    state.immuneMask = 0x7F;
    CHECK(P::PickSchool(state, 0, Fire, 10) == NotSchool);
}

void TieKeepsCsvOrder()
{
    // Frost and arcane take the same amount: the row that comes first in school-nukes.csv wins, and the rows are written in
    // school order (frost 4 before arcane 6) after the main row.
    P::SchoolState state = Plain();
    state.takenPct[Frost] = 150;
    state.takenPct[Arcane] = 150;
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Frost);

    // The same rule on the immunity path.
    state.immuneMask = Bit(Fire);
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Frost);
}

void UnknownSchoolsAreNotPicked()
{
    // Only the schools in knownMask are candidates, however good another school looks.
    P::SchoolState state = Plain();
    state.takenPct[Frost] = 200;
    CHECK(P::PickSchool(state, Bit(Fire) | Bit(Arcane), Fire, 10) == NotSchool);

    // A main school that is not a school at all has no baseline: no pick.
    CHECK(P::PickSchool(state, MageFire, NotSchool, 10) == NotSchool);
}

void ImmuneCandidatesAreSkipped()
{
    // The vulnerability path never picks a school the target is immune to, even at the larger gain.
    P::SchoolState state = Plain();
    state.takenPct[Frost] = 180;
    state.takenPct[Arcane] = 130;
    state.immuneMask = Bit(Frost);
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Arcane);

    // A main school the aura says is immune is the immune case, minGainPct and all: frost's 105 is not the baseline then.
    state = Plain();
    state.immuneMask = Bit(Fire);
    state.takenPct[Frost] = 105;
    CHECK(P::PickSchool(state, MageFire, Fire, 10) == Frost);
}

void PointsGainOnAScaledMainSchool()
{
    // The main school already takes more than plain damage, so a relative gain would demand a bigger fallback than the rule
    // does. 160 is a 10 point gain over 150 and switches; at 10 % relative it would not (150 + 10 % = 165).
    std::uint8_t const warlock = Bit(Shadow) | Bit(Fire);
    P::SchoolState state = Plain();
    state.takenPct[Shadow] = 150;
    state.takenPct[Fire] = 160;
    CHECK(P::PickSchool(state, warlock, Shadow, 10) == Fire);

    // One point below the points bar stays.
    state.takenPct[Fire] = 159;
    CHECK(P::PickSchool(state, warlock, Shadow, 10) == NotSchool);

    // A larger main percent does not widen the bar either: 200 switches to 210, where 10 % relative would ask for 220.
    state.takenPct[Shadow] = 200;
    state.takenPct[Fire] = 210;
    CHECK(P::PickSchool(state, warlock, Shadow, 10) == Fire);
}

void GainIsTheMainSchoolsPercentPlusMin()
{
    // Warlock: shadow main at 100, Searing Pain (fire) at 150. A 60 point minimum refuses the row, a 50 point one takes it
    // (the test is >=, exactly at the gain).
    std::uint8_t const warlock = Bit(Shadow) | Bit(Fire);
    P::SchoolState state = Plain();
    state.takenPct[Fire] = 150;
    CHECK(P::PickSchool(state, warlock, Shadow, 60) == NotSchool);
    CHECK(P::PickSchool(state, warlock, Shadow, 50) == Fire);

    // The main school's own damage-taken aura raises the bar with it: shadow at 150 needs fire at 210 for a 60 point gain.
    state.takenPct[Shadow] = 150;
    state.takenPct[Fire] = 200;
    CHECK(P::PickSchool(state, warlock, Shadow, 60) == NotSchool);
    CHECK(P::PickSchool(state, warlock, Shadow, 50) == Fire);   // 200 >= 150 + 50
    state.takenPct[Fire] = 210;
    CHECK(P::PickSchool(state, warlock, Shadow, 60) == Fire);
}
}

int main()
{
    MainImmuneSwitchesToBestKnown();
    VulnerableBelowGainStays();
    VulnerableAtGainSwitches();
    AllKnownImmuneStays();
    TieKeepsCsvOrder();
    UnknownSchoolsAreNotPicked();
    ImmuneCandidatesAreSkipped();
    PointsGainOnAScaledMainSchool();
    GainIsTheMainSchoolsPercentPlusMin();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}
