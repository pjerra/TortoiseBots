#include "../../runtime/policy/class/HunterStarterAmmoPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    using TortoiseBots::HunterStarterAmmoId;

    assert(HunterStarterAmmoId(false, 1) == 2512);
    assert(HunterStarterAmmoId(false, 9) == 2512);
    assert(HunterStarterAmmoId(false, 10) == 2515);
    assert(HunterStarterAmmoId(false, 25) == 3030);
    assert(HunterStarterAmmoId(false, 40) == 11285);
    assert(HunterStarterAmmoId(false, 51) == 19316);
    assert(HunterStarterAmmoId(false, 60) == 19316);

    assert(HunterStarterAmmoId(true, 1) == 2516);
    assert(HunterStarterAmmoId(true, 10) == 2519);
    assert(HunterStarterAmmoId(true, 25) == 3033);
    assert(HunterStarterAmmoId(true, 40) == 11284);
    assert(HunterStarterAmmoId(true, 51) == 19317);
    assert(HunterStarterAmmoId(true, 60) == 19317);

    std::cout << "PASS: test_hunter_starter_ammo\n";
    return 0;
}
