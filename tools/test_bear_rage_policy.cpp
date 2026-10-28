#include "../ai/playerbot/policy/class/BearRagePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots bear rage policy tests...\n";

    // Maul costs 15 rage (DB 9881): queued from 15, not 45.
    CHECK(!ShouldQueueMaul(14));
    CHECK(ShouldQueueMaul(15));
    CHECK(ShouldQueueMaul(45));
    CHECK(kBearMaulMinRage == 15);

    // Savage Bite: Clearcasting or 50+ rage, never below.
    CHECK(!ShouldSavageBite(49, false));
    CHECK(ShouldSavageBite(50, false));
    CHECK(ShouldSavageBite(100, false));
    CHECK(ShouldSavageBite(0, true));
    CHECK(ShouldSavageBite(49, true));
    CHECK(kBearSavageBiteRage == 50);

    std::cout << "  [PASS] maul queued from 15 rage, savage bite gated on rage/clearcasting\n";
    std::cout << "All TortoiseBots bear rage policy tests passed.\n";
    return 0;
}
