// Standalone check for runtime/TotemSpellPolicy.h (mirrors Totem::IsImmuneToSpellEffect):
//   g++ -std=c++17 -Wall -Wextra tools/test_totem_spell_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/TotemSpellPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

// Effects aimed at the single unit target (TARGET_UNIT_ENEMY 6).
static bool Works(bool positive, std::uint32_t e0, std::uint32_t a0, std::uint32_t e1 = 0, std::uint32_t a1 = 0,
    std::uint32_t e2 = 0, std::uint32_t a2 = 0)
{
    std::uint32_t const effects[3] = {e0, e1, e2};
    std::uint32_t const auras[3] = {a0, a1, a2};
    std::uint32_t const targets[3] = {6, 6, 6};
    return SpellWorksOnTotem(effects, auras, targets, 3, positive);
}

static bool WorksAt(std::uint32_t e0, std::uint32_t a0, std::uint32_t t0, std::uint32_t e1 = 0, std::uint32_t a1 = 0,
    std::uint32_t t1 = 0)
{
    std::uint32_t const effects[3] = {e0, e1, 0};
    std::uint32_t const auras[3] = {a0, a1, 0};
    std::uint32_t const targets[3] = {t0, t1, 0};
    return SpellWorksOnTotem(effects, auras, targets, 3, false);
}

int main()
{
    // Not positive, aura effects only: blocked (Corruption: APPLY_AURA
    // PERIODIC_DAMAGE 3; Hunter's Mark; Faerie Fire; Polymorph; a stun; a sting).
    CHECK(!Works(false, 6, 3));
    CHECK(!Works(false, 6, 12, 6, 61)); // Polymorph: MOD_CONFUSE + transform
    CHECK(!Works(false, 6, 12, 129, 22)); // an area aura on enemies
    CHECK(!Works(false, 134, 3));
    // A taunt, heals, energize: blocked whatever the spell is.
    CHECK(!Works(false, 114, 0));
    CHECK(!Works(true, 10, 0));
    CHECK(!Works(true, 67, 0, 75, 0, 30, 0));
    // An aura effect without an aura name is not "applies aura".
    CHECK(Works(false, 6, 0));
    // Direct damage, strikes, auto attack, a leech, a power burn: they work,
    // also with an aura beside them (Frostbolt: damage + slow).
    CHECK(Works(false, 2, 0, 6, 33));
    CHECK(Works(false, 58, 0));
    CHECK(Works(false, 121, 0, 6, 22));
    CHECK(Works(false, 78, 0));
    CHECK(Works(false, 9, 0));
    // PERSISTENT_AREA_AURA (Rain of Fire) is not in the core's list.
    CHECK(Works(false, 27, 3));
    // Positive spells: only the periodic regeneration auras are blocked.
    CHECK(!Works(true, 6, 8));
    CHECK(!Works(true, 6, 24));
    CHECK(!Works(true, 6, 62));
    CHECK(Works(true, 6, 22)); // a positive non-regen aura is not immune
    // No effects at all: left to the cast checks.
    CHECK(Works(false, 0, 0));
    // Caster-centred area spells are not judged by the current target: a
    // Demoralizing Shout (APPLY_AURA on enemies around the caster, target 22
    // source + 15 enemies in area), Challenging Shout (taunt around the
    // caster), Psychic Scream / Intimidating Shout (fear around the caster).
    CHECK(WorksAt(6, 99, 15));
    CHECK(WorksAt(114, 0, 15, 6, 0, 15));
    CHECK(WorksAt(6, 7, 22));
    // TARGET_UNIT (25) counts as the single unit target too: Hunter's Mark.
    CHECK(!WorksAt(6, 26, 25));
    // A spell with a single-target damage effect and an area aura works.
    CHECK(WorksAt(2, 0, 6, 6, 33, 15));

    // Crowd control: a totem never; outside a run a mob's summoned pet or
    // skeleton stays a candidate; in a run only a kill_first summon is left out.
    CHECK(CcLeavesOut(true, false, false, false));
    CHECK(!CcLeavesOut(false, true, false, false));
    CHECK(!CcLeavesOut(false, true, false, true));
    CHECK(!CcLeavesOut(false, true, true, false));
    CHECK(CcLeavesOut(false, true, true, true));
    CHECK(!CcLeavesOut(false, false, true, true));
    std::cout << "test_totem_spell_policy: ok\n";
    return 0;
}
