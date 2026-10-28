// Boost triggers arm only when the spell is trained and off cooldown; the
// decision lives in ai/playerbot/BoostReadinessPolicy.h and these tests pin
// the pure rule.

#include "../ai/playerbot/BoostReadinessPolicy.h"

#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BoostReady;
using ai::HealBoostReady;
using ai::RacialBoostReady;

int main()
{
    std::cout << "Starting TortoiseBots boost-readiness policy tests...\n";

    // A boost trigger arms only on trained + off cooldown.
    CHECK(BoostReady(true, true));
    CHECK(!BoostReady(false, true));   // never trained (night elf "berserking")
    CHECK(!BoostReady(true, false));   // trained, cooldown running
    CHECK(!BoostReady(false, false));

    // A heal boost (Ascendance) arms only when trained, off cooldown, and
    // someone needs healing: a low party member or a hurt group.
    CHECK(!HealBoostReady(true, true, false, false));
    CHECK(HealBoostReady(true, true, true, false));
    CHECK(HealBoostReady(true, true, false, true));
    CHECK(HealBoostReady(true, true, true, true));
    CHECK(!HealBoostReady(false, true, true, true));
    CHECK(!HealBoostReady(true, false, true, true));

    // The shared racial row arms when at least one of its spells is castable.
    CHECK(RacialBoostReady(true, false));
    CHECK(RacialBoostReady(false, true));
    CHECK(!RacialBoostReady(false, false));
    CHECK(RacialBoostReady(true, true));

    std::cout << "All boost-readiness policy tests passed.\n";
    return 0;
}
