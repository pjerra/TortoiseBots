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
using ai::PullReadyFacts;
using ai::PullReadySteps;
using ai::ReadyTankForPull;
using ai::RetryPullWalk;
using ai::PullReturnPoint;
using ai::PullReturnPointOf;

// The pull's return point (camp-pull B): the command's anchor when one is set (the run leader's camp), else
// the requester's spot, else the tank's own.
static void TestPullAnchorOverrideIsTheReturnPoint()
{
    PullReturnPoint const tank = { 1.0f, 2.0f, 3.0f, 1 };
    PullReturnPoint const requester = { 10.0f, 20.0f, 30.0f, 1 };
    PullReturnPoint const camp = { -5.0f, -6.0f, -7.0f, 1 };
    PullReturnPoint point = PullReturnPointOf(false, camp, true, requester, tank);
    CHECK(point.x == requester.x && point.y == requester.y && point.z == requester.z);
    point = PullReturnPointOf(false, camp, false, requester, tank);
    CHECK(point.x == tank.x && point.y == tank.y && point.z == tank.z);
    point = PullReturnPointOf(true, camp, true, requester, tank);
    CHECK(point.x == camp.x && point.y == camp.y && point.z == camp.z);
}

int main()
{
    TestPullAnchorOverrideIsTheReturnPoint();
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

    // Uldaman run (build 19z11): the pullback came a second after the fight,
    // the tank sitting with the meal cast (24005) live. The order interrupts
    // the cast and stands the tank up before its decisions.
    PullReadyFacts ready;
    ready.casting = true;
    ready.seated = true;
    PullReadySteps steps = ReadyTankForPull(ready);
    CHECK(steps.interrupt && steps.stand);
    steps = ReadyTankForPull(PullReadyFacts());
    CHECK(!steps.interrupt && !steps.stand);
    ready = PullReadyFacts();
    ready.seated = true;
    steps = ReadyTankForPull(ready);
    CHECK(!steps.interrupt && steps.stand);

    // A ranged pull not started after the decisions: one direct reach pull.
    facts = PullStartFacts();
    facts.distance = 54.7f;
    facts.range = 29.5f;
    CHECK(RetryPullWalk(facts, false));
    // A body pull is never judged, and a started walk needs no retry.
    CHECK(!RetryPullWalk(facts, true));
    facts.motionLaunched = true;
    CHECK(!RetryPullWalk(facts, false));

    std::cout << "pull start policy: ok\n";
    return 0;
}
