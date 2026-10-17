// Standalone check for runtime/HunterFeignDeathPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_hunter_feign_death_policy.cpp -o /tmp/test_hunter_fd && /tmp/test_hunter_fd
#include "../runtime/HunterFeignDeathPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

int main()
{
    // Nothing on the hunter (threat climbing on a mob the tank holds): as before.
    CHECK(HunterFeignDeathAllowed({}));
    // A mob on the hunter with the tank (and others) on its threat list: it turns to them.
    CHECK(HunterFeignDeathAllowed({3}));
    CHECK(HunterFeignDeathAllowed({1, 4}));
    // Uldaman 19z12, Keepers attempt 1: the fourth keeper woke onto the hunter,
    // nobody else on its threat list. Feigning would make it evade (FAIL).
    CHECK(!HunterFeignDeathAllowed({0}));
    // One such mob among several is enough to hold the feign back.
    CHECK(!HunterFeignDeathAllowed({2, 0, 5}));
    std::cout << "test_hunter_feign_death_policy: ok\n";
    return 0;
}
