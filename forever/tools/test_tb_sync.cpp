// Standalone check for TB sync: elite danger 10-level margin (ai/playerbot/policy/travel/PointDangerPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_tb_sync.cpp -o /tmp/test_tb_sync && /tmp/test_tb_sync
#include "ai/playerbot/policy/travel/PointDangerPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    // Elites within 9 levels below the bot are still dangerous (e.g. lvl 22 vs 31, lvl 55 vs 60).
    assert(ai::EliteCampDangerous(true, 30, 31));
    assert(ai::EliteCampDangerous(true, 22, 31));
    assert(!ai::EliteCampDangerous(true, 21, 31));
    assert(ai::EliteCampDangerous(true, 55, 60));
    assert(!ai::EliteCampDangerous(true, 50, 60));
    assert(!ai::EliteCampDangerous(false, 55, 60));

    std::cout << "PASS: test_tb_sync (6 checks, 0 failed)\n";
    return 0;
}
