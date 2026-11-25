// The resistance-gear swap rules (issue #20 round 1): ai/playerbot/policy/raid/ResistGearPolicy.h and
// ResistHoldPolicy.h (putting the set back: a dead bot keeps its hold, a partial pass keeps only the pieces it could
// not move, the retry slows down but never gives up, switching the key off releases).
// Build: g++ -std=c++20 -I. forever/tools/test_resist_gear_policy.cpp -o /tmp/t && /tmp/t
// (`policy-tests.sh` on the build VM compiles every forever/tools/test_*.cpp the same way.)
// Every case fails against a plausible wrong rule: the slot list, the 5-point gain, the pin, the ring pairing, an
// erased hold on a dead bot, a retry that stops after its fast budget.
#include "ai/playerbot/policy/raid/ResistGearPolicy.h"
#include "ai/playerbot/policy/raid/ResistHoldPolicy.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
constexpr int kFire = 2;

ai::Piece Worn(std::uint64_t guid, std::uint8_t slot, std::int32_t res, bool pinned = false)
{
    return ai::Piece{guid, slot, res, pinned};
}

ai::Piece Bag(std::uint64_t guid, std::uint8_t slot, std::int32_t res)
{
    return ai::Piece{guid, slot, res, false};
}

ai::Swap const* Find(std::vector<ai::Swap> const& swaps, std::uint8_t slot)
{
    for (ai::Swap const& swap : swaps)
        if (swap.slot == slot)
            return &swap;
    return nullptr;
}

// A tank and a damage dealer get the same answer on every slot where armour is at stake, and no answer on the slots
// the rule never touches. The list is spelled out because the slot numbers are the core's (Player.h:735-755) and the
// output lists `not allowed` slots by name only when a later change moves the numeric list.
void TankKeepsArmourSlots()
{
    // head, shoulders, chest, legs, feet, hands: the armour a tank's survivability is
    for (std::uint8_t slot : { 0, 2, 4, 6, 7, 9 })
    {
        assert(!ai::SlotAllowed(slot, true));
        assert(ai::SlotAllowed(slot, false));
    }
    // neck, waist, wrists, both fingers, back: no armour to lose
    for (std::uint8_t slot : { 1, 5, 8, 10, 11, 14 })
    {
        assert(ai::SlotAllowed(slot, true));
        assert(ai::SlotAllowed(slot, false));
    }
    // shirt (3), trinkets (12, 13), weapons (15, 16, 17) and the tabard (18) never
    for (std::uint8_t slot : { 3, 12, 13, 15, 16, 17, 18 })
    {
        assert(!ai::SlotAllowed(slot, true));
        assert(!ai::SlotAllowed(slot, false));
    }

    // A plan for a tank fills the neck and leaves the head: the slot rule reaches PlanSwaps, not just SlotAllowed.
    std::vector<ai::Piece> const worn = { Worn(1, ai::kSlotHead, 0), Worn(2, ai::kSlotNeck, 0) };
    std::vector<ai::Piece> const bag = { Bag(11, ai::kSlotHead, 60), Bag(12, ai::kSlotNeck, 60) };
    std::vector<ai::Swap> const tankPlan = ai::PlanSwaps(kFire, true, worn, bag);
    assert(tankPlan.size() == 1);
    assert(tankPlan[0].slot == ai::kSlotNeck);
    assert(tankPlan[0].resistGuid == 12);

    std::vector<ai::Swap> const dpsPlan = ai::PlanSwaps(kFire, false, worn, bag);
    assert(dpsPlan.size() == 2);
}

void TrinketNeverSwapped()
{
    std::vector<ai::Piece> worn;   // every slot empty
    std::vector<ai::Piece> bag;
    for (std::uint8_t slot : { 3, 12, 13, 15, 16, 17, 18 })
        bag.push_back(Bag(100 + slot, slot, 200));
    assert(ai::PlanSwaps(kFire, false, worn, bag).empty());
    assert(ai::PlanSwaps(kFire, true, worn, bag).empty());

    // ... even next to a piece the plan does take.
    bag.push_back(Bag(200, ai::kSlotNeck, 30));
    std::vector<ai::Swap> const swaps = ai::PlanSwaps(kFire, false, worn, bag);
    assert(swaps.size() == 1);
    assert(swaps[0].slot == ai::kSlotNeck);
}

void GainBelowFiveIgnored()
{
    // 4 points over the worn piece is not worth the swap; 5 is (the threshold is inclusive).
    {
        std::vector<ai::Piece> const worn = { Worn(1, ai::kSlotNeck, 20) };
        std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotNeck, 24) };
        assert(ai::PlanSwaps(kFire, false, worn, bag).empty());
    }
    {
        std::vector<ai::Piece> const worn = { Worn(1, ai::kSlotNeck, 20) };
        std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotNeck, 25) };
        std::vector<ai::Swap> const swaps = ai::PlanSwaps(kFire, false, worn, bag);
        assert(swaps.size() == 1);
        assert(swaps[0].wornRes == 20);
        assert(swaps[0].resistRes == 25);
    }
    // An EMPTY slot is 0: a piece with less than 5 of the school stays in the bag.
    {
        std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotNeck, 4) };
        assert(ai::PlanSwaps(kFire, false, {}, bag).empty());
    }
    {
        std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotNeck, 5) };
        std::vector<ai::Swap> const swaps = ai::PlanSwaps(kFire, false, {}, bag);
        assert(swaps.size() == 1);
        assert(swaps[0].wornGuid == 0);
    }
}

void PinnedPieceStays()
{
    std::vector<ai::Piece> const worn = { Worn(1, ai::kSlotBack, 5, true) };
    std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotBack, 40) };
    assert(ai::PlanSwaps(kFire, false, worn, bag).empty());

    std::vector<ai::Piece> const unpinned = { Worn(1, ai::kSlotBack, 5, false) };
    std::vector<ai::Swap> const swaps = ai::PlanSwaps(kFire, false, unpinned, bag);
    assert(swaps.size() == 1);
    assert(swaps[0].wornGuid == 1);
    assert(swaps[0].resistGuid == 2);
}

void BestOfTwoRingsBothFingers()
{
    // Both fingers take a ring; the best ring goes to the first finger and the next best to the second, and one ring is
    // never planned twice (the caller lists every ring for both finger slots: either finger accepts it).
    std::vector<ai::Piece> const worn = { Worn(1, ai::kSlotFinger1, 0), Worn(2, ai::kSlotFinger2, 0) };
    std::vector<ai::Piece> bag;
    for (std::uint64_t guid : { 30ull, 10ull, 20ull })
    {
        bag.push_back(Bag(guid, ai::kSlotFinger1, guid == 20 ? 15 : guid == 10 ? 30 : 25));
        bag.push_back(Bag(guid, ai::kSlotFinger2, guid == 20 ? 15 : guid == 10 ? 30 : 25));
    }
    std::vector<ai::Swap> const swaps = ai::PlanSwaps(kFire, false, worn, bag);
    assert(swaps.size() == 2);
    ai::Swap const* first = Find(swaps, ai::kSlotFinger1);
    ai::Swap const* second = Find(swaps, ai::kSlotFinger2);
    assert(first && second);
    assert(first->resistGuid == 10);    // the most resistance
    assert(second->resistGuid == 30);   // the next most, not the same ring again
    assert(first->wornGuid == 1);
    assert(second->wornGuid == 2);

    // A second ring under the 5-point gain stays home: one finger filled, the other as it was.
    std::vector<ai::Piece> const thin = { Bag(40, ai::kSlotFinger1, 30), Bag(41, ai::kSlotFinger1, 33) };
    std::vector<ai::Piece> const oneWorn = { Worn(1, ai::kSlotFinger1, 30), Worn(2, ai::kSlotFinger2, 0) };
    std::vector<ai::Swap> const one = ai::PlanSwaps(kFire, false, oneWorn, thin);
    assert(one.empty());
}

// The school guard: 0 (no school) plans nothing, so a caller that could not resolve a boss's school never swaps.
void NoSchoolNoPlan()
{
    std::vector<ai::Piece> const bag = { Bag(2, ai::kSlotNeck, 40) };
    assert(ai::PlanSwaps(0, false, {}, bag).empty());
}

// A dead bot's restore must not touch the hold: every piece stays recorded for the living retry.
void RestoreLeavesADeadBotsHold()
{
    assert(!ai::MayRestoreHold(false));
    assert(ai::MayRestoreHold(true));
}

// One restore pass: the slot no longer holding the resist piece is stale, the own piece goes back when the core
// accepts it, an empty slot takes a free bag position, and everything else stays in the hold (never dropped).
void PartialRestoreKeepsTheRest()
{
    using ai::PieceBack;
    assert(ai::PlanPieceBack(false, false, true, true, true) == PieceBack::DropStale);
    assert(ai::PlanPieceBack(true, false, true, true, false) == PieceBack::SwapOwn);
    assert(ai::PlanPieceBack(true, false, false, false, true) == PieceBack::Hold);
    assert(ai::PlanPieceBack(true, false, true, false, true) == PieceBack::Hold);
    assert(ai::PlanPieceBack(true, true, false, false, true) == PieceBack::BagResist);
    assert(ai::PlanPieceBack(true, true, false, false, false) == PieceBack::Hold);

    assert(ai::HoldHasPieces(1));
    assert(!ai::HoldHasPieces(0));
}

// The automatic retry waits for a living bot out of combat and never stops: the fast passes are seconds apart, then
// the hold waits a minute between passes and keeps waiting for as long as the bot lives.
void RetryNeverStopsWhileTheBotCanMove()
{
    assert(!ai::ShouldRetryRestore(false, false));
    assert(!ai::ShouldRetryRestore(false, true));
    assert(!ai::ShouldRetryRestore(true, true));
    assert(ai::ShouldRetryRestore(true, false));

    for (std::uint32_t attempts = 0; attempts < ai::kMaxRestoreAttempts; ++attempts)
    {
        assert(ai::ShouldRetryRestore(true, false));
        assert(ai::RetryDelaySeconds(attempts) == ai::kRetrySeconds);
    }
    for (std::uint32_t attempts : { ai::kMaxRestoreAttempts, ai::kMaxRestoreAttempts + 1u, 1000u })
    {
        assert(ai::ShouldRetryRestore(true, false));
        assert(ai::RetryDelaySeconds(attempts) == ai::kBackoffRetrySeconds);
    }
}

// The shape of the fix Codex asked for: a full bag outlasts every fast pass, the hold keeps waiting, and the bag
// position that frees up later still gets the set back.
void BackoffWaitsUntilTheBagFrees()
{
    std::uint32_t attempts = 0;
    while (attempts < 40)
    {
        assert(ai::ShouldRetryRestore(true, false));
        // The slot was empty when the set went on and no bag position is free: the piece stays in the hold.
        assert(ai::PlanPieceBack(true, true, false, false, false) == ai::PieceBack::Hold);
        assert(ai::HoldHasPieces(1));
        ++attempts;
    }
    assert(ai::RetryDelaySeconds(attempts) == ai::kBackoffRetrySeconds);
    // A bag position is free again: the next pass takes the resist piece off the slot.
    assert(ai::ShouldRetryRestore(true, false));
    assert(ai::PlanPieceBack(true, true, false, false, true) == ai::PieceBack::BagResist);
}

// Switching the feature off does not resolve a hold the bot cannot put back yet: a dead or fighting bot keeps every
// piece recorded, and the first living pass out of combat takes them.
void DisableWaitsForABotThatCanMove()
{
    assert(ai::ReleaseOnDisable(true, false, true));
    assert(!ai::MayRestoreHold(false));
    assert(!ai::ShouldRetryRestore(false, false));
    assert(!ai::ShouldRetryRestore(true, true));
    assert(ai::ShouldRetryRestore(true, false));
}

// Turning TortoiseBots.RaidResistGear.Enable off while a set is held releases it; a world that started at 0, a world
// still at 1 and a bot holding nothing are not release edges.
void DisableReleasesTheHold()
{
    assert(ai::ReleaseOnDisable(true, false, true));
    assert(!ai::ReleaseOnDisable(false, false, true));
    assert(!ai::ReleaseOnDisable(true, true, true));
    assert(!ai::ReleaseOnDisable(true, false, false));
}

// A hold read back from the characters store is restored on login unless the bot already holds one.
void AStoredHoldIsRecoveredOnLogin()
{
    assert(ai::RecoverOnLogin(true, false));
    assert(!ai::RecoverOnLogin(false, false));
    assert(!ai::RecoverOnLogin(true, true));
}

// A fresh set is HELD: the world tick must not touch it, however long the bot keeps it on. Only a restore that was
// asked for (and did not finish) is RELEASING, and only that state is retried.
void OnlyARequestedReleaseIsRetried()
{
    assert(!ai::ShouldRetryHold(ai::HoldState::Held));
    assert(ai::ShouldRetryHold(ai::HoldState::Releasing));
}
} // namespace

int main()
{
    TankKeepsArmourSlots();
    TrinketNeverSwapped();
    GainBelowFiveIgnored();
    PinnedPieceStays();
    BestOfTwoRingsBothFingers();
    NoSchoolNoPlan();
    RestoreLeavesADeadBotsHold();
    PartialRestoreKeepsTheRest();
    RetryNeverStopsWhileTheBotCanMove();
    BackoffWaitsUntilTheBagFrees();
    DisableWaitsForABotThatCanMove();
    DisableReleasesTheHold();
    AStoredHoldIsRecoveredOnLogin();
    OnlyARequestedReleaseIsRetried();
    std::cout << "PASS: test_resist_gear_policy (14 cases)" << std::endl;
    return 0;
}
