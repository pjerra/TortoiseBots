#include "../../runtime/policy/class/RogueSapApproachPolicy.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using namespace TortoiseBots;

    SapPoint const targetAt{0.0f, 0.0f};
    float const targetOri = 0.0f; // facing +x (east)
    float const standDist = 4.0f;
    float const frontAvoidRadius = 15.0f;

    // 1. Rogue directly in front (+x) must flank around outside the front detection cone,
    //    never walk straight to (+standDist, 0) in front of the mob.
    SapPoint const stepFromFront = SapBehindStep(SapPoint{20.0f, 1.0f}, targetAt, targetOri, standDist, frontAvoidRadius);
    assert(stepFromFront.x <= 0.5f);
    assert(std::hypot(stepFromFront.x, stepFromFront.y) >= 14.0f);

    // 2. Rogue already behind (-x) goes straight to the behind stand point (-standDist, 0).
    SapPoint const stepFromBehind = SapBehindStep(SapPoint{-10.0f, 1.0f}, targetAt, targetOri, standDist, frontAvoidRadius);
    assert(std::fabs(stepFromBehind.x - (-standDist)) < 0.05f);
    assert(std::fabs(stepFromBehind.y) < 0.05f);

    // 3. Rogue on the flank (x == 0, y == +15) goes straight to the behind stand point (-standDist, 0).
    SapPoint const stepFromFlank = SapBehindStep(SapPoint{0.0f, 15.0f}, targetAt, targetOri, standDist, frontAvoidRadius);
    assert(std::fabs(stepFromFlank.x - (-standDist)) < 0.05f);
    assert(std::fabs(stepFromFlank.y) < 0.05f);

    std::cout << "PASS: test_rogue_sap_behind\n";
    return 0;
}
