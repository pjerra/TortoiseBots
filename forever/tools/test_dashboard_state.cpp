// Standalone test of the dashboard's macro-state refinement (forever/src/dash/TbDashboardStatePure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_dashboard_state.cpp -o /tmp/test_dashboard_state && /tmp/test_dashboard_state
//
// The rules the sender applies at every 2 s snapshot: a base state other than idle passes through; a standing bot
// that did real work inside 45 s is busy; a bot whose only activity was churn for 45 s is stalled, unless it fought
// or gained XP in the last 60 s (a spawn camp between pulls stays busy).

#include "../src/dash/TbDashboardStatePure.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ds = TortoiseBots::dashboard_state;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } \
} while (0)

int main()
{
    std::uint32_t const now = 100000;

    CHECK(ds::RefineIdle(ds::Combat, now, 0, 0, 0, 0) == ds::Combat);
    CHECK(ds::RefineIdle(ds::Moving, now, 0, 0, 0, 0) == ds::Moving);
    CHECK(ds::RefineIdle(ds::Resting, now, 0, 0, 0, 0) == ds::Resting);
    CHECK(ds::RefineIdle(ds::Dead, now, 0, 0, 0, 0) == ds::Dead);

    // Real work inside 45 s: busy.
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, 0, 0, 0) == ds::Busy);

    // No activity at all for 45 s: idle.
    CHECK(ds::RefineIdle(ds::Idle, now, now - ds::IdleAfterMs, 0, 0, 0) == ds::Idle);

    // Churn for the whole idle window, no fight or XP: stalled.
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs, 0, 0) == ds::Stalled);
    // Churn started too recently: still busy.
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs + 1, 0, 0) == ds::Busy);

    // A fight or an XP gain inside 60 s vetoes stalled.
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs, now - 1000, 0) == ds::Busy);
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs, 0, now - 1000) == ds::Busy);
    // ... but an older one does not.
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs, now - ds::RecentFightMs, 0) == ds::Stalled);
    CHECK(ds::RefineIdle(ds::Idle, now, now - 1000, now - ds::IdleAfterMs, 0, now - ds::RecentFightMs) == ds::Stalled);

    // Names round-trip in the daemon's order.
    CHECK(std::strcmp(ds::Name(ds::Combat), "combat") == 0);
    CHECK(std::strcmp(ds::Name(ds::Moving), "moving") == 0);
    CHECK(std::strcmp(ds::Name(ds::Busy), "busy") == 0);
    CHECK(std::strcmp(ds::Name(ds::Stalled), "stalled") == 0);
    CHECK(std::strcmp(ds::Name(ds::Resting), "resting") == 0);
    CHECK(std::strcmp(ds::Name(ds::Dead), "dead") == 0);
    CHECK(std::strcmp(ds::Name(ds::Idle), "idle") == 0);

    std::printf("test_dashboard_state: %d checks passed\n", checks);
    return 0;
}
