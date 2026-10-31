// Standalone check for ai/playerbot/WarriorWwStancePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_warrior_ww_stance_policy.cpp -o /tmp/test_warrior_ww && /tmp/test_warrior_ww
#include "../ai/playerbot/WarriorWwStancePolicy.h"

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
    std::cout << "Starting TortoiseBots warrior whirlwind-stance policy tests...\n";

    // Whirlwind prereq: off cooldown and 25+ rage.
    CHECK(ShouldWhirlwindStanceDance(true, 25));
    CHECK(ShouldWhirlwindStanceDance(true, 100));
    CHECK(!ShouldWhirlwindStanceDance(false, 100)); // on cooldown: no dance
    CHECK(!ShouldWhirlwindStanceDance(true, 24));   // below the rage cost
    CHECK(!ShouldWhirlwindStanceDance(true, 0));
    CHECK(!ShouldWhirlwindStanceDance(false, 0));
    std::cout << "  [PASS] whirlwind stance dance gates on cooldown and rage\n";

    // Pummel prereq: off cooldown, 10+ rage, target casting.
    CHECK(ShouldPummelStanceDance(true, 10, true));
    CHECK(ShouldPummelStanceDance(true, 100, true));
    CHECK(!ShouldPummelStanceDance(false, 100, true)); // on cooldown: no dance
    CHECK(!ShouldPummelStanceDance(true, 9, true));     // below the rage cost
    CHECK(!ShouldPummelStanceDance(true, 10, false));   // nothing to interrupt
    CHECK(!ShouldPummelStanceDance(true, 0, false));
    std::cout << "  [PASS] pummel stance dance gates on cooldown, rage and cast\n";

    CHECK(WARRIOR_WHIRLWIND_RAGE_COST == 25);
    CHECK(WARRIOR_PUMMEL_RAGE_COST == 10);
    std::cout << "  [PASS] cost constants pinned\n";

    std::cout << "All TortoiseBots warrior whirlwind-stance policy tests passed.\n";
    return 0;
}
