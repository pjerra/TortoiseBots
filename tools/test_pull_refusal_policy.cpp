// Standalone test for the pull refusal codes: `tank-busy` when the only tank
// is in combat, `no-tank` when there is none.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pull_refusal_policy.cpp -o /tmp/test_pull_refusal_policy
//   /tmp/test_pull_refusal_policy

#include "../ai/playerbot/PullRefusalPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    CHECK(std::strcmp(ai::PullNoExecutorCode(false), "no-tank") == 0);
    CHECK(std::strcmp(ai::PullNoExecutorCode(true), "tank-busy") == 0);
    // The two answers read differently, and neither is empty.
    CHECK(ai::PullNoExecutorText(false) != ai::PullNoExecutorText(true));
    CHECK(!ai::PullNoExecutorText(true).empty());
    CHECK(ai::PullNoExecutorText(true).find("combat") != std::string::npos);
    CHECK(ai::PullNoExecutorText(false).find("No live tank") != std::string::npos);
    // The optional detail names the exclusion reason (issue #908); empty by
    // default, and protocol-safe (no '|' characters).
    CHECK(ai::PullNoExecutorText(false).find("(") == std::string::npos);
    CHECK(ai::PullNoExecutorText(false, "dead").find("(dead)") != std::string::npos);
    CHECK(ai::PullNoExecutorText(true, "in combat").find("(in combat)") != std::string::npos);
    std::printf("pull refusal policy: %d checks passed\n", checks);
    return 0;
}
