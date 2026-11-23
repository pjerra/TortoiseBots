// Standalone policy test for ai/playerbot/policy/travel/MountBreakEvenPolicy.h (issue #152).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_mount_break_even_policy.cpp -o /tmp/test_mount_break_even_policy
//   /tmp/test_mount_break_even_policy

#include "../ai/playerbot/policy/travel/MountBreakEvenPolicy.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestNear(float got, float want)
{
    ++checks;
    if (std::fabs(got - want) > 0.001f)
    {
        std::fprintf(stderr, "FAIL near: got %f want %f\n", got, want);
        std::exit(1);
    }
}

int main()
{
    // A 60 % mount with a 3 s cast repays it after 52.5 yd at factor 1.5
    // (7 yd/s * 3 s / 0.6 * 1.5).
    TestNear(ai::MountBreakEvenDistance(7.0f, 3.0f, 60, 1.5f), 52.5f);
    // An epic 100 % mount repays the same cast sooner.
    TestNear(ai::MountBreakEvenDistance(7.0f, 3.0f, 100, 1.5f), 31.5f);
    // Factor 1 keeps the bot on foot for the bare repayment distance.
    TestNear(ai::MountBreakEvenDistance(7.0f, 3.0f, 60, 1.0f), 35.0f);
    // An instant form (cast time 0) is worth any trip.
    TestNear(ai::MountBreakEvenDistance(7.0f, 0.0f, 60, 1.5f), 0.0f);
    // 0 disables the threshold: mount for any trip.
    TestNear(ai::MountBreakEvenDistance(7.0f, 3.0f, 60, 0.0f), 0.0f);
    // An unusable mount can never repay the cast.
    CHECK(ai::MountBreakEvenDistance(7.0f, 3.0f, 0, 1.5f) == FLT_MAX);

    std::printf("PASS test_mount_break_even_policy: %d checks\n", checks);
    return 0;
}
