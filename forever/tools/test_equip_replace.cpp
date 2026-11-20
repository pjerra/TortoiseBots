// Standalone check of host/cmd/TbReplacePure.h: which replaced piece `.tb equip ... replace` destroys instead of putting in the bags.
//   cd <worktree> && g++ -std=c++17 -Wall -Wextra -I forever/src forever/tools/test_equip_replace.cpp -o /tmp/test_equip_replace && /tmp/test_equip_replace
#include "host/cmd/TbReplacePure.h"

#include <cstdio>

using TortoiseBots::h3::replace::IsStarterUpgrade;
using TortoiseBots::h3::replace::MayDestroyReplacedItem;
using TortoiseBots::h3::replace::StarterPiece;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, char const* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

constexpr unsigned kPoor = 0, kCommon = 1, kUncommon = 2, kRare = 3;
}

int main()
{
    // Starter gear is poor or common; anything better is worth keeping.
    Check(StarterPiece(kPoor), "poor is starter gear");
    Check(StarterPiece(kCommon), "common is starter gear");
    Check(!StarterPiece(kUncommon), "uncommon is not starter gear");
    Check(!StarterPiece(kRare), "rare is not starter gear");

    // The incoming piece must be strictly better in item level.
    Check(IsStarterUpgrade(kPoor, 1, 2), "poor, lower item level: destroyed");
    Check(IsStarterUpgrade(kCommon, 10, 12), "common, lower item level: destroyed");
    Check(!IsStarterUpgrade(kCommon, 12, 12), "equal item level: kept");
    Check(!IsStarterUpgrade(kPoor, 13, 12), "better old item level: kept");
    Check(!IsStarterUpgrade(kUncommon, 1, 40), "better quality: kept");
    Check(!IsStarterUpgrade(kRare, 1, 40), "rare: kept");

    // Only a host bot, or the sender's own character when the sender is a GM.
    Check(MayDestroyReplacedItem(true, false, false), "host bot: may lose a piece");
    Check(MayDestroyReplacedItem(true, true, false), "host bot of a GM: may lose a piece");
    Check(MayDestroyReplacedItem(false, true, true), "the GM's own character: may lose a piece");
    Check(!MayDestroyReplacedItem(false, false, true), "a non-GM's own character: kept");
    Check(!MayDestroyReplacedItem(false, true, false), "another player's character: never touched");

    if (failures)
    {
        std::printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("all %d checks passed\n", checks);
    return 0;
}
