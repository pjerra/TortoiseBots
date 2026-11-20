// Standalone check of host/cmd/TbReplacePure.h: which replaced piece `.tb equip ... replace` destroys instead of putting in the bags.
//   cd <worktree> && g++ -std=c++17 -Wall -Wextra -I forever/src forever/tools/test_equip_replace.cpp -o /tmp/test_equip_replace && /tmp/test_equip_replace
#include "host/cmd/TbReplacePure.h"

#include <cstdio>
#include <cstring>

using TortoiseBots::h3::replace::Disposable;
using TortoiseBots::h3::replace::DisposableReason;
using TortoiseBots::h3::replace::MayDestroyReplacedItem;
using TortoiseBots::h3::replace::ReasonName;

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
    // Quality and item level alone do not identify starter gear: a common piece the character did not start with is kept.
    Check(DisposableReason(true, false, kCommon, false, false) == Disposable::No, "common, not start outfit: bagged");
    Check(DisposableReason(true, false, kUncommon, false, false) == Disposable::No, "uncommon, not start outfit: bagged");
    Check(DisposableReason(true, false, kRare, false, false) == Disposable::No, "rare, not start outfit: bagged");

    // A start-outfit piece is destroyed, whatever its quality.
    Check(DisposableReason(true, true, kCommon, false, false) == Disposable::StartOutfit, "start outfit, common: destroyed");
    Check(DisposableReason(true, true, kPoor, false, false) == Disposable::StartOutfit, "start outfit, poor: destroyed");

    // A poor (grey) piece is disposable on its own.
    Check(DisposableReason(true, false, kPoor, false, false) == Disposable::Poor, "poor, not start outfit: destroyed");

    // Never when the equip itself failed: the piece is still on the body or in the bags, untouched.
    Check(DisposableReason(false, true, kCommon, false, false) == Disposable::No, "failed equip, start outfit: nothing destroyed");
    Check(DisposableReason(false, false, kPoor, false, false) == Disposable::No, "failed equip, poor: nothing destroyed");

    // Never a quest item, and never a bag that holds items, whatever the quality or provenance.
    Check(DisposableReason(true, true, kPoor, true, false) == Disposable::No, "start-outfit quest item: kept");
    Check(DisposableReason(true, false, kPoor, true, false) == Disposable::No, "poor quest item: kept");
    Check(DisposableReason(true, true, kPoor, false, true) == Disposable::No, "start-outfit bag with items: kept");
    Check(DisposableReason(true, false, kPoor, false, true) == Disposable::No, "poor bag with items: kept");

    // The words the log line and the reply carry.
    Check(std::strcmp(ReasonName(Disposable::StartOutfit), "start-outfit") == 0, "reason name: start-outfit");
    Check(std::strcmp(ReasonName(Disposable::Poor), "poor") == 0, "reason name: poor");
    Check(std::strcmp(ReasonName(Disposable::No), "") == 0, "reason name: none");

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
