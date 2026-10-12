#include "../ai/playerbot/AoeOffPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsAreaSpellAction;

int main()
{
    std::cout << "Starting TortoiseBots aoe-off policy tests...\n";

    // Area spells from the class aoe strategies and from plain rotations.
    CHECK(IsAreaSpellAction("blizzard"));
    CHECK(IsAreaSpellAction("cone of cold"));
    CHECK(IsAreaSpellAction("frost nova"));
    CHECK(IsAreaSpellAction("flamestrike"));
    CHECK(IsAreaSpellAction("arcane explosion"));
    CHECK(IsAreaSpellAction("rain of fire"));
    CHECK(IsAreaSpellAction("multi-shot"));
    CHECK(IsAreaSpellAction("volley"));
    CHECK(IsAreaSpellAction("explosive trap on target"));
    CHECK(IsAreaSpellAction("consecration"));
    CHECK(IsAreaSpellAction("whirlwind"));
    CHECK(IsAreaSpellAction("cleave"));
    CHECK(IsAreaSpellAction("thunder clap"));
    CHECK(IsAreaSpellAction("hurricane"));
    CHECK(IsAreaSpellAction("swipe (bear)"));
    CHECK(IsAreaSpellAction("chain lightning"));
    CHECK(IsAreaSpellAction("magma totem"));
    CHECK(IsAreaSpellAction("blade flurry"));
    CHECK(IsAreaSpellAction("holy nova"));
    CHECK(IsAreaSpellAction("psychic scream"));
    CHECK(IsAreaSpellAction("war stomp"));
    CHECK(IsAreaSpellAction("goblin sapper"));
    CHECK(IsAreaSpellAction("dps aoe"));
    std::cout << "  [PASS] area spells are vetoed\n";

    // Single-target play, dot spreading, heals and defensive clouds stay.
    CHECK(!IsAreaSpellAction("frostbolt"));
    CHECK(!IsAreaSpellAction("fireball"));
    CHECK(!IsAreaSpellAction("shadow bolt"));
    CHECK(!IsAreaSpellAction("aimed shot"));
    CHECK(!IsAreaSpellAction("heroic strike"));
    CHECK(!IsAreaSpellAction("sunder armor"));
    CHECK(!IsAreaSpellAction("corruption on attacker"));
    CHECK(!IsAreaSpellAction("prayer of healing"));
    CHECK(!IsAreaSpellAction("chain heal"));
    CHECK(!IsAreaSpellAction("smoke bomb"));
    CHECK(!IsAreaSpellAction("polymorph"));
    CHECK(!IsAreaSpellAction("dps assist"));
    CHECK(!IsAreaSpellAction("avoid aoe"));
    CHECK(!IsAreaSpellAction(""));
    std::cout << "  [PASS] single-target and heals are not vetoed\n";

    std::cout << "All aoe-off policy tests passed.\n";
    return 0;
}
