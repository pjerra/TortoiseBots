// Standalone check of host/cmd/TbReplacePure.h: which replaced piece `.tb equip ... replace` destroys instead of putting in the bags.
//   cd <worktree> && g++ -std=c++17 -Wall -Wextra -I forever/src forever/tools/test_equip_replace.cpp -o /tmp/test_equip_replace && /tmp/test_equip_replace
#include "host/cmd/TbReplacePure.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using TortoiseBots::h3::replace::ActiveObjective;
using TortoiseBots::h3::replace::Disposable;
using TortoiseBots::h3::replace::DisposableReason;
using TortoiseBots::h3::replace::DisplacedFate;
using TortoiseBots::h3::replace::Fate;
using TortoiseBots::h3::replace::IsQuestItem;
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

// A model of the core's swap, to run the handler's flow against: the worn slot holds one item, the bags hold stacks. SwapItem takes the old piece
// off and stores it with StoreItem, which MERGES a stackable piece into an equal stack already in the bags and FREES the moved item.
struct FakeItem
{
    int guid = 0;
    int entry = 0;
    unsigned count = 1;
    bool stackable = false;
    bool freed = false;   // the Item object no longer exists: any dereference is a use-after-free
};

struct FakeCharacter
{
    std::vector<FakeItem> store;   // every Item object ever made (pointers into it stay stable: indexes)
    int worn = -1;                 // index of the item in the slot
    std::vector<int> bags;         // indexes
    int destroyedGuid = 0;
    unsigned destroyedCount = 0;
    int uaf = 0;                   // dereferences of a freed Item

    int Make(int entry, unsigned count, bool stackable)
    {
        store.push_back({int(store.size()) + 100, entry, count, stackable, false});
        return int(store.size()) - 1;
    }
    // The core's SwapItem of the bag item `fresh` into the worn slot.
    void Swap(int fresh)
    {
        int const old = worn;
        bags.erase(std::find(bags.begin(), bags.end(), fresh));
        worn = fresh;
        if (old < 0)
            return;
        if (store[old].stackable)
            for (int b : bags)
                if (store[b].entry == store[old].entry)
                {
                    store[b].count += store[old].count;   // merged into the bag stack
                    store[old].freed = true;               // and the moved Item is freed
                    return;
                }
        bags.push_back(old);
    }
    int ByGuid(int guid) const   // GetItemByGuid: a freed item is no longer found
    {
        for (size_t i = 0; i < store.size(); ++i)
            if (!store[i].freed && store[i].guid == guid)
                return int(i);
        return -1;
    }
    bool InBags(int i) const { return std::find(bags.begin(), bags.end(), i) != bags.end(); }
    void Destroy(int i)
    {
        destroyedGuid = store[i].guid;
        destroyedCount = store[i].count;
        store[i].count = 0;
        bags.erase(std::find(bags.begin(), bags.end(), i));
    }
};

// The OLD flow: keep the raw item across the swap, use it afterwards, destroy whatever it is.
void OldFlow(FakeCharacter& c, int fresh, Disposable why)
{
    int const displaced = c.worn;   // the raw Item*
    c.Swap(fresh);
    if (c.store[displaced].freed)
        ++c.uaf;                    // GetBagSlot() on a freed Item
    else if (c.InBags(displaced) && why != Disposable::No)
        c.Destroy(displaced);
}

// The NEW flow: keep the guid, re-resolve it after the swap, and let DisplacedFate decide.
Fate NewFlow(FakeCharacter& c, int fresh, Disposable why)
{
    int const guid = c.store[c.worn].guid;
    c.Swap(fresh);
    int const piece = c.ByGuid(guid);
    Fate const fate = DisplacedFate(piece >= 0, piece >= 0 && c.InBags(piece), piece >= 0 && piece == c.worn,
        piece >= 0 ? c.store[piece].count : 0, why);
    if (fate == Fate::Destroy)
        c.Destroy(piece);
    return fate;
}
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

    // A quest item by class, one that bonds as a quest item, one that starts a quest, and one an active quest requires are all kept even when
    // poor and not start-outfit. The fourth is read from the character's active quests' item objectives (QUEST_OBJECTIVE_ITEM = 1), not only
    // from the template: an objective row of another type that happens to name the same entry does not count.
    constexpr std::uint32_t kPoorQuestObjectiveItem = 3412;
    std::vector<ActiveObjective> const active = {
        { 0, std::int32_t(kPoorQuestObjectiveItem) },   // a monster objective with the same id: not an item requirement
        { 1, 99 },                               // a different required item
    };
    std::vector<ActiveObjective> const requiresIt = {
        { 1, std::int32_t(kPoorQuestObjectiveItem) },
    };
    Check(IsQuestItem(kPoorQuestObjectiveItem, true, false, false, active), "quest class: protected");
    Check(IsQuestItem(kPoorQuestObjectiveItem, false, true, false, active), "quest bonding: protected");
    Check(IsQuestItem(kPoorQuestObjectiveItem, false, false, true, active), "starts a quest: protected");
    Check(IsQuestItem(kPoorQuestObjectiveItem, false, false, false, requiresIt), "required by an active quest: protected");
    Check(!IsQuestItem(kPoorQuestObjectiveItem, false, false, false, active), "only a non-item objective names it: not a quest item");
    Check(!IsQuestItem(kPoorQuestObjectiveItem, false, false, false, {}), "no quest of the character names it: not a quest item");
    Check(DisposableReason(true, false, kPoor, IsQuestItem(kPoorQuestObjectiveItem, false, false, false, requiresIt), false) == Disposable::No,
        "poor quest-objective item, not start outfit: kept");
    Check(DisposableReason(true, false, kPoor, IsQuestItem(kPoorQuestObjectiveItem, false, false, false, active), false) == Disposable::Poor,
        "poor non-quest item with unrelated quest rows: destroyed");

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


    // The swap model (the use-after-free review finding). A poor thrown-weapon stack replaced while an equal stack is in the bags merges and the
    // Item is freed: the old flow dereferences it; the new flow finds nothing by guid and destroys nothing.
    {
        FakeCharacter old;
        int const worn = old.Make(2947, 1, true);
        old.worn = worn;
        int const bagStack = old.Make(2947, 4, true);
        int const fresh = old.Make(3111, 1, false);
        old.bags = {bagStack, fresh};
        OldFlow(old, fresh, Disposable::Poor);
        Check(old.uaf == 1, "model: the old flow dereferences the merged (freed) piece");

        FakeCharacter now;
        now.worn = now.Make(2947, 1, true);
        int const bagStack2 = now.Make(2947, 4, true);
        int const fresh2 = now.Make(3111, 1, false);
        now.bags = {bagStack2, fresh2};
        Check(NewFlow(now, fresh2, Disposable::Poor) == Fate::Gone, "merged stack: the guid is gone, fate Gone");
        Check(now.destroyedGuid == 0 && now.store[bagStack2].count == 5, "merged stack: nothing destroyed, the bag stack holds the 5");
    }
    // A stack of 5 that does NOT merge (no equal stack in the bags): the old flow destroys all 5, the new flow keeps it in the bags.
    {
        FakeCharacter old;
        old.worn = old.Make(2947, 5, false);   // a non-merging stack
        int const fresh = old.Make(3111, 1, false);
        old.bags = {fresh};
        OldFlow(old, fresh, Disposable::Poor);
        Check(old.destroyedCount == 5, "model: the old flow destroys the whole stack of 5");

        FakeCharacter now;
        now.worn = now.Make(2947, 5, false);
        int const fresh2 = now.Make(3111, 1, false);
        now.bags = {fresh2};
        Check(NewFlow(now, fresh2, Disposable::Poor) == Fate::KeepStack, "stack of 5: fate KeepStack");
        Check(now.destroyedGuid == 0 && now.bags.size() == 1 && now.store[now.bags[0]].count == 5, "stack of 5: left in the bags whole");
    }
    // A single disposable item: destroyed, exactly that one.
    {
        FakeCharacter now;
        now.worn = now.Make(2947, 1, false);
        int const fresh = now.Make(3111, 1, false);
        now.bags = {fresh};
        int const guid = now.store[now.worn].guid;
        Check(NewFlow(now, fresh, Disposable::StartOutfit) == Fate::Destroy, "single item: fate Destroy");
        Check(now.destroyedGuid == guid && now.destroyedCount == 1, "single item: that one item destroyed");
    }
    // Not disposable: kept whatever the count; the piece just equipped is never destroyed; not in the bags is kept.
    Check(DisplacedFate(true, true, false, 1, Disposable::No) == Fate::Keep, "not disposable: kept");
    Check(DisplacedFate(true, true, true, 1, Disposable::Poor) == Fate::Keep, "the worn piece: kept");
    Check(DisplacedFate(true, false, false, 1, Disposable::Poor) == Fate::Keep, "not in the bags: kept");
    Check(DisplacedFate(false, false, false, 0, Disposable::Poor) == Fate::Gone, "not found: gone");
    Check(DisplacedFate(true, true, false, 0, Disposable::Poor) == Fate::KeepStack, "count 0: never destroyed");

    if (failures)
    {
        std::printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("all %d checks passed\n", checks);
    return 0;
}
