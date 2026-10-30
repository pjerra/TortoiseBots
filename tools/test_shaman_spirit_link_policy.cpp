// Standalone check for ai/playerbot/policy/class/ShamanSpiritLinkPolicy.h:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_shaman_spirit_link_policy.cpp -o /tmp/test_shaman_spirit_link && /tmp/test_shaman_spirit_link
#include "../ai/playerbot/policy/class/ShamanSpiritLinkPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldSpiritLink;

int main()
{
    std::cout << "Starting shaman spirit-link policy tests...\n";

    // Hold for the save: a healthy tank keeps the 10-min link.
    CHECK(ShouldSpiritLink(false, true, true, 51, 50) == false);
    CHECK(ShouldSpiritLink(false, true, true, 90, 50) == false);
    std::cout << "  [PASS] healthy tank holds the link\n";

    // Tank at or below the low-health line takes it.
    CHECK(ShouldSpiritLink(false, true, true, 50, 50) == true);
    CHECK(ShouldSpiritLink(false, true, true, 20, 50) == true);
    std::cout << "  [PASS] low tank takes the link\n";

    // Group dupe guard and combat gate still hold.
    CHECK(ShouldSpiritLink(true, true, true, 20, 50) == false);
    CHECK(ShouldSpiritLink(false, true, false, 20, 50) == false);
    CHECK(ShouldSpiritLink(false, false, true, 20, 50) == false);
    std::cout << "  [PASS] link up, out of combat, and buffed tank refuse\n";

    std::cout << "All shaman spirit-link policy checks PASSED!\n";
    return 0;
}
