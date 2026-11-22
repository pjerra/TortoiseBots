#include "../../runtime/policy/class/ShamanStarterTotemPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    using TortoiseBots::ShamanStarterTotemForSlot;

    assert(ShamanStarterTotemForSlot(0, 1) == 0);
    assert(ShamanStarterTotemForSlot(0, 4) == 5175);
    assert(ShamanStarterTotemForSlot(1, 9) == 0);
    assert(ShamanStarterTotemForSlot(1, 10) == 5176);
    assert(ShamanStarterTotemForSlot(2, 19) == 0);
    assert(ShamanStarterTotemForSlot(2, 20) == 5177);
    assert(ShamanStarterTotemForSlot(3, 29) == 0);
    assert(ShamanStarterTotemForSlot(3, 30) == 5178);
    assert(ShamanStarterTotemForSlot(3, 60) == 5178);
    assert(ShamanStarterTotemForSlot(4, 60) == 0);

    std::cout << "PASS: test_fx_palsham\n";
    return 0;
}
