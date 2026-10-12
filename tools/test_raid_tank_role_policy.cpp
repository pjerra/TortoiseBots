#include "../ai/playerbot/RaidTankRolePolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::OrderRaidTankRoles;
using ai::RaidTankCandidate;

namespace
{
    // usable, isTank, isMainTankFlag, isMainAssistFlag, isRaidAssistant
    RaidTankCandidate Tank() { return {true, true, false, false, false}; }
    RaidTankCandidate Dps() { return {true, false, false, false, false}; }
    RaidTankCandidate DeadTank() { return {false, true, false, false, false}; }

    bool Is(std::vector<std::size_t> const& got, std::vector<std::size_t> const& want)
    {
        return got == want;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots raid tank role tests...\n";

    // (1) No flags: the old election, live tanks in slot order.
    {
        std::vector<RaidTankCandidate> slots = {Dps(), Tank(), DeadTank(), Tank(), Dps()};
        CHECK(Is(OrderRaidTankRoles(slots), {1, 3}));
        CHECK(Is(OrderRaidTankRoles({Dps(), Dps()}), {}));
        CHECK(Is(OrderRaidTankRoles({}), {}));
        std::cout << "  [PASS] no flags keeps slot order and skips dead tanks\n";
    }

    // (2) Main Tank flag beats slot order.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Dps(), Tank(), Tank()};
        slots[3].isMainTankFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {3, 0, 2}));
        std::cout << "  [PASS] flagged main tank first, others keep slot order\n";
    }

    // (3) Flagged Main Tank is honoured whatever its spec (explicit order).
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Dps()};
        slots[1].isMainTankFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {1, 0}));
        std::cout << "  [PASS] flagged non-tank main tank still holds the job\n";
    }

    // (4) Dead or absent Main Tank: the job falls to the next in order.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), DeadTank(), Tank()};
        slots[1].isMainTankFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {0, 2}));
        std::cout << "  [PASS] dead flagged main tank hands over to slot order\n";
    }

    // (5) Main Assist: first assist tank when it is a tank.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Tank(), Tank(), Tank()};
        slots[2].isMainTankFlag = true;
        slots[3].isMainAssistFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {2, 3, 0, 1}));
        std::cout << "  [PASS] tank main assist is the first assist tank\n";
    }

    // (6) Main Assist without a Main Tank flag leads the tank order.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Tank()};
        slots[1].isMainAssistFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {1, 0}));
        std::cout << "  [PASS] tank main assist leads when no main tank is set\n";
    }

    // (7) A DPS Main Assist takes no tank job.
    {
        std::vector<RaidTankCandidate> slots = {Dps(), Tank(), Tank()};
        slots[0].isMainAssistFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {1, 2}));
        std::cout << "  [PASS] non-tank main assist is ignored\n";
    }

    // (8) One member flagged both ways appears once.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Tank()};
        slots[1].isMainTankFlag = true;
        slots[1].isMainAssistFlag = true;
        CHECK(Is(OrderRaidTankRoles(slots), {1, 0}));
        std::cout << "  [PASS] member with both flags is listed once\n";
    }

    // (9) Promoted raid assistants lead the assist tanks (donor
    //     IsAssistTankOfIndex), but do not pick the main tank.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Tank(), Tank(), Tank()};
        slots[3].isRaidAssistant = true;
        CHECK(Is(OrderRaidTankRoles(slots), {0, 3, 1, 2}));
        slots[0].isRaidAssistant = true;
        CHECK(Is(OrderRaidTankRoles(slots), {0, 3, 1, 2}));
        std::cout << "  [PASS] promoted tanks lead the assists, slot 1 stays main\n";
    }

    // (10) Full order: MT flag, MA tank, promoted tanks, then the rest.
    {
        std::vector<RaidTankCandidate> slots = {Tank(), Tank(), Dps(), Tank(), Tank(), Tank()};
        slots[4].isMainTankFlag = true;
        slots[5].isMainAssistFlag = true;
        slots[3].isRaidAssistant = true;
        slots[2].isRaidAssistant = true; // promoted DPS takes no tank job
        CHECK(Is(OrderRaidTankRoles(slots), {4, 5, 3, 0, 1}));
        std::cout << "  [PASS] full order MT, MA, promoted, slot order\n";
    }

    std::cout << "All raid tank role checks PASSED!\n";
    return 0;
}
