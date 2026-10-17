#include "../runtime/LftCompPolicy.h"

#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::LftCompEntry;
using TortoiseBots::LftCompResult;
using TortoiseBots::LftComputeMissingRoles;
using TortoiseBots::LftCompPickRole;
using TortoiseBots::kLftRoleTank;
using TortoiseBots::kLftRoleHealer;
using TortoiseBots::kLftRoleDamage;

namespace
{
    // Solo entrant: its own block, slot order irrelevant.
    LftCompEntry Solo(uint32_t guid, uint32_t join, uint8_t mask, bool human = true)
    {
        return {guid, join, mask, guid, 0, human};
    }
    // Party member: shared block, group-slot order.
    LftCompEntry Member(uint32_t guid, uint32_t join, uint8_t mask, uint32_t leader, uint32_t slot, bool human = true)
    {
        return {guid, join, mask, leader, slot, human};
    }
    LftCompResult Run(std::vector<LftCompEntry> entries)
    {
        return LftComputeMissingRoles(entries);
    }
}

int main()
{
    std::cout << "Starting TortoiseBots LFT composition tests...\n";

    // (1) Core PickRole parity: tank > healer > damage first-fit.
    CHECK(LftCompPickRole(kLftRoleTank | kLftRoleHealer | kLftRoleDamage, 0, 0, 0) == kLftRoleTank);
    CHECK(LftCompPickRole(kLftRoleTank | kLftRoleHealer | kLftRoleDamage, 1, 0, 0) == kLftRoleHealer);
    CHECK(LftCompPickRole(kLftRoleTank | kLftRoleHealer | kLftRoleDamage, 1, 1, 0) == kLftRoleDamage);
    CHECK(LftCompPickRole(kLftRoleTank | kLftRoleHealer | kLftRoleDamage, 1, 1, 3) == 0);
    CHECK(LftCompPickRole(kLftRoleDamage, 0, 0, 0) == kLftRoleDamage);
    CHECK(LftCompPickRole(kLftRoleDamage, 0, 0, 3) == 0);
    std::cout << "  [PASS] pick order matches the core matcher\n";

    // (2) Solo humans: the queued role decides the missing slots.
    {
        LftCompResult tank = Run({Solo(7, 100, kLftRoleTank)});
        CHECK(tank.needTank == 0 && tank.needHeal == 1 && tank.needDps == 3 && !tank.blockedHumans);
        LftCompResult dps = Run({Solo(7, 100, kLftRoleDamage)});
        CHECK(dps.needTank == 1 && dps.needHeal == 1 && dps.needDps == 2 && !dps.blockedHumans);
        LftCompResult heal = Run({Solo(7, 100, kLftRoleHealer)});
        CHECK(heal.needTank == 1 && heal.needHeal == 0 && heal.needDps == 3 && !heal.blockedHumans);
    }
    std::cout << "  [PASS] solo tank/healer/dps queue the complement\n";

    // (3) Multi-role human takes the first free slot, like the core will.
    {
        LftCompResult both = Run({Solo(7, 100, kLftRoleTank | kLftRoleHealer)});
        CHECK(both.needTank == 0 && both.needHeal == 1 && both.needDps == 3);
    }
    std::cout << "  [PASS] multi-role human counted as the core assigns them\n";

    // (4) Same-second ties break deterministically (guidLow): the earlier
    // guid takes the tank, the other counts as healer. No more unspecified
    // sort order between the fill's count and the core's assignment.
    {
        LftCompResult r = Run({Solo(9, 100, kLftRoleTank), Solo(5, 100, kLftRoleTank | kLftRoleHealer)});
        CHECK(r.needTank == 0 && r.needHeal == 1 && r.needDps == 3 && !r.blockedHumans);
        CHECK(r.covered == 1 && r.members == 2);
    }
    std::cout << "  [PASS] join-time ties are deterministic\n";

    // (5) A party that fits covers its slots: tank + healer party needs 3 dps.
    {
        LftCompResult r = Run({Member(11, 200, kLftRoleTank, 11, 0), Member(12, 200, kLftRoleHealer, 11, 1)});
        CHECK(r.needTank == 0 && r.needHeal == 0 && r.needDps == 3 && !r.blockedHumans);
        CHECK(r.covered == 2);
    }
    std::cout << "  [PASS] fitting party leaves only the missing roles\n";

    // (6) Slot order is honoured inside a block, like the core queue order:
    // the tank-first member takes the tank and sinks a tank-only partner,
    // while the reverse slot order covers tank + healer.
    {
        uint8_t const both = static_cast<uint8_t>(kLftRoleTank | kLftRoleHealer);
        LftCompResult stuck = Run({Member(21, 300, both, 21, 0), Member(22, 300, kLftRoleTank, 21, 1)});
        CHECK(stuck.blockedHumans && stuck.needTank == 0 && stuck.needHeal == 0 && stuck.needDps == 0);
        LftCompResult fits = Run({Member(22, 300, kLftRoleTank, 21, 0), Member(21, 300, both, 21, 1)});
        CHECK(!fits.blockedHumans && fits.needTank == 0 && fits.needHeal == 0 && fits.needDps == 3);
    }
    std::cout << "  [PASS] party block evaluated atomically in slot order\n";

    // (7) Solo entrants that fit nowhere sit out; the formable set still fills.
    {
        LftCompResult r = Run({Solo(31, 400, kLftRoleTank), Solo(32, 401, kLftRoleTank)});
        CHECK(!r.blockedHumans && r.needTank == 0 && r.needHeal == 1 && r.needDps == 3);
        CHECK(r.covered == 1 && r.members == 2);
    }
    std::cout << "  [PASS] duplicate solo roles do not block the fill\n";

    // (8) A complete set needs nothing; bots are never queued into covered roles.
    {
        LftCompResult r = Run({Solo(41, 500, kLftRoleTank, false), Solo(42, 501, kLftRoleHealer, false),
                               Solo(43, 502, kLftRoleDamage, false), Solo(44, 503, kLftRoleDamage, false),
                               Solo(45, 504, kLftRoleDamage, false)});
        CHECK(r.needTank == 0 && r.needHeal == 0 && r.needDps == 0 && !r.blockedHumans);
        CHECK(r.covered == 5);
    }
    std::cout << "  [PASS] complete set queues nothing\n";

    // (9) Nothing covered means the full set is missing (the caller only
    // passes entries while a waiting human exists, and excludes committed
    // in-offer members that keep their assigned slots).
    {
        LftCompResult r = Run({});
        CHECK(r.needTank == 1 && r.needHeal == 1 && r.needDps == 3 && !r.blockedHumans);
    }
    std::cout << "  [PASS] empty simulation reports the full set\n";

    std::cout << "TortoiseBots LFT composition tests passed.\n";
    return 0;
}
