// Standalone check for ai/playerbot/OutcomeReasonPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_outcome_reason_policy.cpp -o /tmp/test_outcome_reason_policy
//   /tmp/test_outcome_reason_policy
#include "../ai/playerbot/OutcomeReasonPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

static const char* g_case = "(unnamed)";
#define CASE(name) (g_case = (name))

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << " in " << g_case << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots outcome reason policy tests...\n";

    CASE("NoCheckMeansNoReason");
    CHECK(OutcomeTailWord(0xFF) == nullptr);

    CASE("ZeroMeansCombat");
    CHECK(OutcomeTailWord(0) != nullptr);
    CHECK(std::strcmp(OutcomeTailWord(0), "combat") == 0);

    CASE("UnknownCodeMeansOther");
    CHECK(std::strcmp(OutcomeTailWord(77), "other") == 0);

    std::cout << "outcome reason policy: all tests passed\n";
    return 0;
}
