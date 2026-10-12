#include "../ai/playerbot/JumpPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

int main()
{
    std::cout << "Starting TortoiseBots jump policy tests...\n";

    // Dungeons and raids (Map::IsDungeon()): no jumping.
    CHECK(!ai::JumpAllowedOnMap(true));
    std::cout << "  [PASS] no jumps in dungeons/raids\n";

    // Open world and battlegrounds: unchanged.
    CHECK(ai::JumpAllowedOnMap(false));
    std::cout << "  [PASS] jumps allowed elsewhere\n";

    std::cout << "All TortoiseBots jump policy tests passed.\n";
    return 0;
}
