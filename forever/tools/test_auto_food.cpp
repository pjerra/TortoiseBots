#include "forever/src/host/TbAutoFoodPure.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    using namespace TortoiseBots::autofood;

    // Every level 1-60 resolves to a non-zero food item and a non-zero drink item.
    for (uint8_t lvl = 1; lvl <= 60; ++lvl)
    {
        for (uint32_t pick = 0; pick < 6; ++pick)
            assert(PickFoodItem(lvl, pick) != 0);
        assert(PickDrinkItem(lvl) != 0);
    }

    // Level 0 clamps to tier 1.
    assert(PickFoodItem(0, 0) == 117);
    assert(PickDrinkItem(0) == 159);

    // Tier boundaries for food (pick 0 = meat/jerky):
    assert(PickFoodItem(1, 0) == 117);    // Tough Jerky (req 1)
    assert(PickFoodItem(4, 0) == 117);
    assert(PickFoodItem(5, 0) == 2287);   // Haunch of Meat (req 5)
    assert(PickFoodItem(14, 0) == 2287);
    assert(PickFoodItem(15, 0) == 3770);  // Mutton Chop (req 15)
    assert(PickFoodItem(24, 0) == 3770);
    assert(PickFoodItem(25, 0) == 3771);  // Wild Hog Shank (req 25)
    assert(PickFoodItem(34, 0) == 3771);
    assert(PickFoodItem(35, 0) == 4599);  // Cured Ham Steak (req 35)
    assert(PickFoodItem(44, 0) == 4599);
    assert(PickFoodItem(45, 0) == 8952);  // Roasted Quail (req 45)
    assert(PickFoodItem(60, 0) == 8952);

    // Pick rotates deterministically through the 6 food varieties per tier.
    assert(PickFoodItem(1, 1) == 4540);   // Tough Hunk of Bread
    assert(PickFoodItem(1, 6) == 117);    // wraps around

    // Tier boundaries for drink:
    assert(PickDrinkItem(1) == 159);      // Refreshing Spring Water (req 1)
    assert(PickDrinkItem(4) == 159);
    assert(PickDrinkItem(5) == 1179);     // Ice Cold Milk (req 5)
    assert(PickDrinkItem(14) == 1179);
    assert(PickDrinkItem(15) == 1205);    // Melon Juice (req 15)
    assert(PickDrinkItem(24) == 1205);
    assert(PickDrinkItem(25) == 1708);    // Sweet Nectar (req 25)
    assert(PickDrinkItem(34) == 1708);
    assert(PickDrinkItem(35) == 1645);    // Moonberry Juice (req 35)
    assert(PickDrinkItem(44) == 1645);
    assert(PickDrinkItem(45) == 8766);    // Morning Glory Dew (req 45)
    assert(PickDrinkItem(60) == 8766);

    // Lower-tier items strictly below current tier are returned for cleanup, never current or higher.
    std::vector<uint32_t> const staleFoodAt5 = StaleLadderFoodItems(5);
    assert(staleFoodAt5.size() == 6);
    assert(staleFoodAt5[0] == 117);
    assert(StaleLadderFoodItems(1).empty());

    std::vector<uint32_t> const staleDrinkAt5 = StaleLadderDrinkItems(5);
    assert(staleDrinkAt5.size() == 1 && staleDrinkAt5[0] == 159);
    assert(StaleLadderDrinkItems(1).empty());

    std::cout << "PASS: test_auto_food\n";
    return 0;
}
