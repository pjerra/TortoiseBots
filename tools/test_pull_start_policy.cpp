#include "../ai/playerbot/PullStartPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PullStartFacts;
using ai::PullWalkStarted;

int main()
{
    // Mob 38 yd away, pull range 30, tank standing, nothing launched: not started.
    PullStartFacts facts;
    facts.distance = 38.0f;
    facts.range = 30.0f;
    CHECK(!PullWalkStarted(facts));

    // The walk was launched this tick but the unit does not read as moving yet.
    facts.motionLaunched = true;
    CHECK(PullWalkStarted(facts));

    // Already moving.
    facts = PullStartFacts();
    facts.distance = 38.0f;
    facts.range = 30.0f;
    facts.moving = true;
    CHECK(PullWalkStarted(facts));

    // Already in range: the shot is ready, nothing to walk.
    facts = PullStartFacts();
    facts.distance = 30.0f;
    facts.range = 30.0f;
    CHECK(PullWalkStarted(facts));

    // The target is gone: nothing to judge.
    facts = PullStartFacts();
    facts.hasTarget = false;
    facts.distance = 99.0f;
    CHECK(PullWalkStarted(facts));

    std::cout << "pull start policy: ok\n";
    return 0;
}
