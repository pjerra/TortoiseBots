// Prayer of Healing only when 3+ hurt in the priest's own subgroup.
// Decision lives in ai/playerbot/policy/heal/PrayerOfHealingPolicy.h.

#include "../ai/playerbot/policy/heal/PrayerOfHealingPolicy.h"

#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PrayerOfHealingWorth;
using ai::kPrayerOfHealingMinHurt;

int main()
{
    std::cout << "Starting TortoiseBots prayer-of-healing policy tests...\n";

    CHECK(kPrayerOfHealingMinHurt == 3);
    CHECK(!PrayerOfHealingWorth(0));
    CHECK(!PrayerOfHealingWorth(1));
    CHECK(!PrayerOfHealingWorth(2));
    CHECK(PrayerOfHealingWorth(3));
    CHECK(PrayerOfHealingWorth(5));

    std::cout << "All prayer-of-healing policy tests passed.\n";
    return 0;
}
