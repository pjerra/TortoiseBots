// Standalone test of the thrown-flight decision (forever/src/host/TbThrownPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_thrown.cpp -o /tmp/test_thrown && /tmp/test_thrown
// Failing case first: a knockback off a ledge reports one Land with the apex and the landing z, and nothing more.
#include "forever/src/host/TbThrownPure.h"

#include <cstdio>

namespace kb = TortoiseBots::kb;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static kb::Sample Air(unsigned map, int instance, float x, float y, float z, unsigned now, unsigned remaining)
{
    kb::Sample s;
    s.effectMotion = true;
    s.splineDone = false;
    s.mapId = map;
    s.instanceId = instance;
    s.x = x;
    s.y = y;
    s.z = z;
    s.nowMs = now;
    s.remainingMs = remaining;
    return s;
}

static kb::Sample Ground(unsigned map, int instance, float x, float y, float z, unsigned now)
{
    kb::Sample s;
    s.effectMotion = false;
    s.splineDone = true;
    s.mapId = map;
    s.instanceId = instance;
    s.x = x;
    s.y = y;
    s.z = z;
    s.nowMs = now;
    return s;
}

static bool Dry() { return false; }
static bool Wet() { return true; }

static void knockback_off_ledge_lands_once_with_apex()
{
    kb::State state;
    // z 10, 13.5, 9 flying 20 yd, then the spline done at z -12 two yards further.
    kb::Step s = kb::Observe(state, Air(1, 0, 100.0f, 100.0f, 10.0f, 1000, 700), Dry);
    CHECK(s.kind == kb::Kind::Start);
    CHECK(s.apexZ == 10.0f);
    s = kb::Observe(state, Air(1, 0, 111.0f, 100.0f, 13.5f, 1050, 650), Dry);
    CHECK(s.kind == kb::Kind::Flying);
    s = kb::Observe(state, Air(1, 0, 118.0f, 100.0f, 9.0f, 1100, 500), Dry);
    CHECK(s.kind == kb::Kind::Flying);
    s = kb::Observe(state, Ground(1, 0, 120.0f, 100.0f, -12.0f, 1500), Dry);
    CHECK(s.kind == kb::Kind::Land);
    CHECK(s.apexZ == 13.5f);
    CHECK(s.landZ == -12.0f);
    CHECK(s.chargesFall);
    // exactly once
    s = kb::Observe(state, Ground(1, 0, 120.0f, 100.0f, -12.0f, 1550), Dry);
    CHECK(s.kind == kb::Kind::None);
}

static void TestNoLandWithoutStart()
{
    kb::State state;
    CHECK(kb::Observe(state, Ground(1, 0, 5.0f, 5.0f, 5.0f, 1000), Dry).kind == kb::Kind::None);
    kb::Sample halfWay = Ground(1, 0, 5.0f, 5.0f, 5.0f, 1050);
    halfWay.splineDone = false;
    CHECK(kb::Observe(state, halfWay, Dry).kind == kb::Kind::None);
}

static void TestRefreshNeverLowers()
{
    kb::State state;
    kb::Step s = kb::Observe(state, Air(1, 0, 10.0f, 10.0f, 10.0f, 1000, 500), Dry);
    CHECK(s.kind == kb::Kind::Start);
    CHECK(s.jumpUntilMs == 2500);
    s = kb::Observe(state, Air(1, 0, 11.0f, 10.0f, 11.0f, 1100, 100), Dry);
    CHECK(s.kind == kb::Kind::Flying);
    CHECK(s.jumpUntilMs == 2500);           // 1100 + 100 + 1000 is lower: the old deadline stays
    s = kb::Observe(state, Air(1, 0, 12.0f, 10.0f, 12.0f, 1200, 2000), Dry);
    CHECK(s.jumpUntilMs == 4200);           // a longer flight raises it
    s = kb::Observe(state, Ground(1, 0, 12.0f, 10.0f, -1.0f, 1300), Dry);
    CHECK(s.kind == kb::Kind::Land);
}

// A thrown flight that ends because the spline is done while the generator is still EFFECT_MOTION.
static void TestSplineDoneEnds()
{
    kb::State state;
    CHECK(kb::Observe(state, Air(1, 0, 3.0f, 3.0f, 3.0f, 10, 1000), Dry).kind == kb::Kind::Start);
    kb::Sample ended = Ground(1, 0, 3.5f, 3.0f, 2.0f, 20);
    ended.effectMotion = true;                 // generator still EFFECT_MOTION, the spline is done
    kb::Step s = kb::Observe(state, ended, Dry);
    CHECK(s.kind == kb::Kind::Land);
    CHECK(s.apexZ == 3.0f && s.landZ == 2.0f);
}

// A second throw after a landing starts over, apex from the new start.
static void TestSecondThrow()
{
    kb::State state;
    CHECK(kb::Observe(state, Air(1, 0, 4.0f, 4.0f, 4.0f, 10, 100), Dry).kind == kb::Kind::Start);
    CHECK(kb::Observe(state, Ground(1, 0, 4.0f, 4.0f, 4.0f, 20), Dry).kind == kb::Kind::Land);
    kb::Step s = kb::Observe(state, Air(1, 0, 8.0f, 4.0f, 8.0f, 30, 100), Dry);
    CHECK(s.kind == kb::Kind::Start);
    CHECK(s.apexZ == 8.0f);
}

// The jump deadline: 0 is "none set" (the engine's unset jump time), and the compare must cross the uint32 wrap. A plain
// `int32(wanted - current) > 0` answers false at both ends: an unset project on a server up more than 24.8 days never gets
// a deadline.
static void TestDeadlineAtZeroAndAcrossWrap()
{
    CHECK(kb::RaisesDeadline(0, 1000));                  // no deadline yet: set it
    CHECK(kb::RaisesDeadline(0, 0x90000000u));           // 24.8 days of uptime: still an unset deadline
    CHECK(kb::RaisesDeadline(3000, 4000));
    CHECK(!kb::RaisesDeadline(4000, 3000));
    CHECK(!kb::RaisesDeadline(4000, 4000));
    CHECK(kb::RaisesDeadline(0xFFFFFF00u, 0x00000100u)); // across the wrap: 512 ms later
    CHECK(!kb::RaisesDeadline(0x00000100u, 0xFFFFFF00u)); // and not backwards across it
}

// The clock near the wrap in the state machine itself: a start and a refresh both keep a usable future deadline.
static void TestStateClockNearWrap()
{
    kb::State state;
    kb::Step s = kb::Observe(state, Air(1, 0, 10.0f, 10.0f, 10.0f, 0xFFFFFC00u, 512), Dry);
    CHECK(s.kind == kb::Kind::Start);
    CHECK(s.jumpUntilMs == 0xFFFFFC00u + 512u + 1000u);   // wrapped past zero
    s = kb::Observe(state, Air(1, 0, 11.0f, 10.0f, 10.0f, 16u, 2000), Dry);
    CHECK(s.kind == kb::Kind::Flying);
    CHECK(s.jumpUntilMs == 16u + 2000u + 1000u);          // the refresh is later across the wrap: raised
}

// A teleport mid-flight: the bot lands somewhere it was not thrown to. The core's HandleFall would charge the fall from the
// old apex at the new spot (another map, another instance, or 20 yd away in the same one).
static void TestTeleportMidFlightIsNotLanded()
{
    {
        kb::State state;
        CHECK(kb::Observe(state, Air(1, 0, 100.0f, 100.0f, 20.0f, 1000, 800), Dry).kind == kb::Kind::Start);
        CHECK(kb::Observe(state, Air(1, 0, 112.0f, 100.0f, 22.0f, 1050, 700), Dry).kind == kb::Kind::Flying);
        kb::Step s = kb::Observe(state, Ground(1, 0, 120.0f, 100.0f, 5.0f, 1500), Dry);
        CHECK(s.kind == kb::Kind::Land);
        CHECK(!s.chargesFall);                      // 8 yd from the last airborne sample
    }
    {
        kb::State state;
        CHECK(kb::Observe(state, Air(1, 0, 100.0f, 100.0f, 20.0f, 1000, 800), Dry).kind == kb::Kind::Start);
        CHECK(kb::Observe(state, Air(1, 0, 101.0f, 100.0f, 21.0f, 1050, 700), Dry).kind == kb::Kind::Flying);
        kb::Step s = kb::Observe(state, Ground(530, 0, 101.0f, 100.0f, 21.0f, 1500), Dry);
        CHECK(s.kind == kb::Kind::Land);
        CHECK(!s.chargesFall);                      // another map
    }
    {
        kb::State state;
        CHECK(kb::Observe(state, Air(33, 4, 100.0f, 100.0f, 20.0f, 1000, 800), Dry).kind == kb::Kind::Start);
        CHECK(kb::Observe(state, Air(33, 4, 101.0f, 100.0f, 21.0f, 1050, 700), Dry).kind == kb::Kind::Flying);
        kb::Step s = kb::Observe(state, Ground(33, 9, 101.0f, 100.0f, 21.0f, 1500), Dry);
        CHECK(s.kind == kb::Kind::Land);
        CHECK(!s.chargesFall);                      // another instance of the same map
    }
}

// A landing in water is a swim start on a client, not CMSG_MOVE_FALL_LAND: no fall damage.
static void TestWaterLandingIsNotCharged()
{
    kb::State state;
    CHECK(kb::Observe(state, Air(1, 0, 50.0f, 50.0f, 30.0f, 1000, 800), Dry).kind == kb::Kind::Start);
    CHECK(kb::Observe(state, Air(1, 0, 60.0f, 50.0f, 31.0f, 1050, 700), Dry).kind == kb::Kind::Flying);
    kb::Step s = kb::Observe(state, Ground(1, 0, 62.0f, 50.0f, 0.5f, 1500), Wet);
    CHECK(s.kind == kb::Kind::Land);
    CHECK(!s.chargesFall);
    CHECK(s.apexZ == 31.0f);
}

// The same landing dry is charged, so the water and teleport rules do not swallow the knockback itself.
static void TestDryLandingIsCharged()
{
    kb::State state;
    CHECK(kb::Observe(state, Air(1, 0, 50.0f, 50.0f, 30.0f, 1000, 800), Dry).kind == kb::Kind::Start);
    CHECK(kb::Observe(state, Air(1, 0, 60.0f, 50.0f, 31.0f, 1050, 700), Dry).kind == kb::Kind::Flying);
    kb::Step s = kb::Observe(state, Ground(1, 0, 62.0f, 50.0f, 0.5f, 1500), Dry);
    CHECK(s.kind == kb::Kind::Land);
    CHECK(s.chargesFall);
    CHECK(s.apexZ == 31.0f);
}

int main()
{
    knockback_off_ledge_lands_once_with_apex();
    TestNoLandWithoutStart();
    TestRefreshNeverLowers();
    TestSplineDoneEnds();
    TestSecondThrow();
    TestDeadlineAtZeroAndAcrossWrap();
    TestStateClockNearWrap();
    TestTeleportMidFlightIsNotLanded();
    TestWaterLandingIsNotCharged();
    TestDryLandingIsCharged();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
