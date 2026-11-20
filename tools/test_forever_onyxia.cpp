#include "../forever/src/host/TbOnyxiaPolicy.h"
#include <cassert>
#include <iostream>
using namespace TortoiseBots::onyxia;
int main()
{
    Encounter encounter;
    Observation boss{true, true, false, false, 100, 249, 7, 10184};
    assert(!encounter.Update(boss, 500));
    assert(encounter.elapsedMs == 0);
    boss.combat = true;
    assert(encounter.Update(boss, 500));
    assert(encounter.phase == Phase::GroundOne);
    boss.healthPct = 64;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::GroundOne); // HP threshold alone does not prove takeoff.
    boss.flying = true;
    assert(encounter.Update(boss, 500));
    assert(encounter.phase == Phase::Air);
    boss.healthPct = 39;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::Air); // Landing waits for the actual flight state.
    boss.flying = false;
    assert(encounter.Update(boss, 500));
    assert(encounter.phase == Phase::GroundThree);
    boss.alive = false;
    assert(encounter.Update(boss, 500));
    assert(encounter.phase == Phase::Dead);
    boss = {true, true, false, false, 100, 249, 7, 10184};
    assert(encounter.Update(boss, 500));
    assert(encounter.phase == Phase::Idle && !encounter.sawAir && !encounter.elapsedMs);
    boss.combat = true; boss.flying = true;
    encounter.Update(boss, 500);
    boss.instance = 8; boss.flying = false;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::GroundOne && !encounter.sawAir && encounter.elapsedMs == 500);
    boss.map = 389;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::Idle && encounter.instance == 0);
    boss.map = 249; boss.entry = 11262;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::Idle);
    boss.entry = 10184; boss.healthPct = 35;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::GroundThree); // Attaching to an already landed fight.
    boss.present = false;
    encounter.Update(boss, 500);
    assert(encounter.phase == Phase::Idle && !encounter.elapsedMs);
    std::cout << "PASS: Onyxia phase transitions, late attach and cleanup\n";
}
