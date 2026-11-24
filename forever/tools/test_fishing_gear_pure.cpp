// The weapon a fishing-pole repair puts back: the best carried melee weapon,
// never a pole, never one the core refuses, deterministic on ties.
#include "forever/src/host/TbFishingGearPure.h"

#include <cassert>

int main()
{
    using TortoiseBots::fishinggear::BestWeaponIndex;
    using TortoiseBots::fishinggear::WeaponCandidate;

    assert(BestWeaponIndex({}) == -1);

    // Nothing carried may be equipped: no restore.
    assert(BestWeaponIndex({ { 2092, 4, false, false } }) == -1);

    // A pole is a tool: it never wins, even with the highest item level.
    assert(BestWeaponIndex({ { 6256, 40, true, true }, { 2092, 4, false, true } }) == 1);
    assert(BestWeaponIndex({ { 6256, 40, true, true } }) == -1);

    // The highest equippable item level wins.
    assert(BestWeaponIndex({ { 2092, 4, false, true }, { 17752, 50, false, true } }) == 1);

    // Equal item levels: the lower item id, so the choice is stable.
    assert(BestWeaponIndex({ { 3000, 50, false, true }, { 17752, 50, false, true } }) == 0);
    assert(BestWeaponIndex({ { 17752, 50, false, true }, { 3000, 50, false, true } }) == 1);

    // Ineligible entries are skipped, not ranked last.
    assert(BestWeaponIndex({ { 1, 99, false, false }, { 2, 10, false, true }, { 3, 20, true, true } }) == 1);

    return 0;
}
