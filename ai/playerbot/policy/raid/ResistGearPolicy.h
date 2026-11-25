// Resistance gear for a raid boss whose damage is mostly one school (issue #20 round 1): WHICH worn pieces a bot swaps
// for spells of that school, and for which: the pure rules, with no core include (fully testable on the toolchain alone).
//
// The host (forever/src/host/TbResistGear.cpp) turns the bot's equipment and bags into Pieces, calls PlanSwaps and runs
// the swaps through the core's Player::SwapItem; this header decides nothing about the core and nothing about timing
// (when a swap is armed or released is the brain's business: SPEC-20 round 2).
//
// Slots are the core's EquipmentSlots numbers (Player.h:735-755 at the pin): head 0, neck 1, shoulders 2, BODY 3 (the
// shirt), chest 4, waist 5, legs 6, feet 7, wrists 8, hands 9, finger1 10, finger2 11, trinket1 12, trinket2 13, back
// 14, main hand 15, off hand 16, ranged 17, tabard 18. They are written as plain numbers on purpose: this header is the
// core-free half, and its test names the slots the same way.
//
// Tests: forever/tools/test_resist_gear_policy.cpp (TankKeepsArmourSlots, TrinketNeverSwapped, GainBelowFiveIgnored,
// PinnedPieceStays, BestOfTwoRingsBothFingers).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
constexpr std::uint8_t kSlotHead = 0;
constexpr std::uint8_t kSlotNeck = 1;
constexpr std::uint8_t kSlotShoulders = 2;
constexpr std::uint8_t kSlotChest = 4;
constexpr std::uint8_t kSlotWaist = 5;
constexpr std::uint8_t kSlotLegs = 6;
constexpr std::uint8_t kSlotFeet = 7;
constexpr std::uint8_t kSlotWrists = 8;
constexpr std::uint8_t kSlotHands = 9;
constexpr std::uint8_t kSlotFinger1 = 10;
constexpr std::uint8_t kSlotFinger2 = 11;
constexpr std::uint8_t kSlotBack = 14;

// The swap may change a piece by at least this much of the school. A tank gives up armour for it, so a marginal piece
// is not worth the bag churn (and the run back for the set if the raid wipes on the pull).
constexpr std::int32_t kResistGain = 5;

// The slots a plan may touch. The armour slots (head, shoulders, chest, legs, feet, hands) are the non-tank's only:
// a tank keeps its armour and health. Neck, waist, wrists, both fingers and back carry no armour and are always
// allowed; the shirt, trinkets, weapons, ranged and tabard are never swapped (a trinket's resist aura is often its use,
// and a weapon's resist enchant is the player's choice).
inline bool SlotAllowed(std::uint8_t slot, bool tank)
{
    switch (slot)
    {
        case 1: case 5: case 8: case 10: case 11: case 14:
            return true;
        case 0: case 2: case 4: case 6: case 7: case 9:
            return !tank;
        default:
            return false;
    }
}

// One piece of gear and what it is worth for the school being planned.
//   guid    the piece's identity for THIS plan: the host's token for it (ForeverCore's ObjectGuid is 128 bits and this
//           header is core-free, so the host numbers the pieces; one guid is one token however many slots it can fill).
//   slot    the equipment slot it can fill. A ring that fits either finger is listed for BOTH finger slots by the
//           caller: the plan uses it once, on the first finger that takes it.
//   res     its resistance of the planned school (0 for a slot's missing piece: an empty slot is a Piece with res 0).
//   pinned  worn pieces only: the bot's keep/equip order holds this item, so it stays where it is whatever it resists.
struct Piece
{
    std::uint64_t guid = 0;
    std::uint8_t slot = 0;
    std::int32_t res = 0;
    bool pinned = false;
};

// One slot to swap: the bag piece to put on in place of the worn one. `wornGuid` is 0 for an empty slot; the
// guids are the caller's tokens, handed back unchanged.
struct Swap
{
    std::uint8_t slot = 0;
    std::uint64_t wornGuid = 0;
    std::int32_t wornRes = 0;
    std::uint64_t resistGuid = 0;
    std::int32_t resistRes = 0;
};

// The plan for one bot: per allowed slot (ascending, so both fingers take the best two rings in a stable order), the
// bag piece with the most resistance that beats the worn piece by at least kResistGain and is not already planned for
// another slot. A pinned worn piece stays; an empty slot counts as 0. `school` 0 means "no school resolved" and plans
// nothing, so a caller with an unknown boss entry cannot swap at all.
inline std::vector<Swap> PlanSwaps(int school, bool tank, std::vector<Piece> const& worn, std::vector<Piece> const& bag)
{
    std::vector<Swap> swaps;
    if (school <= 0)
        return swaps;

    // By GUID, not by index: a ring is listed once per finger slot it can fill, and must still be planned only once.
    std::vector<std::uint64_t> usedGuids;
    for (std::uint8_t slot : { kSlotHead, kSlotNeck, kSlotShoulders, kSlotChest, kSlotWaist, kSlotLegs, kSlotFeet,
             kSlotWrists, kSlotHands, kSlotFinger1, kSlotFinger2, kSlotBack })
    {
        if (!SlotAllowed(slot, tank))
            continue;

        Piece const* wornPiece = nullptr;
        for (Piece const& piece : worn)
            if (piece.slot == slot)
            {
                wornPiece = &piece;
                break;
            }
        if (wornPiece && wornPiece->pinned)
            continue;

        std::int32_t const base = wornPiece ? wornPiece->res : 0;
        std::size_t best = bag.size();
        for (std::size_t i = 0; i < bag.size(); ++i)
        {
            if (bag[i].slot != slot || bag[i].res - base < kResistGain)
                continue;
            if (std::find(usedGuids.begin(), usedGuids.end(), bag[i].guid) != usedGuids.end())
                continue;
            // More resistance wins; the lower token breaks the tie, so a re-plan on the same bags gives the same set.
            if (best == bag.size() || bag[i].res > bag[best].res ||
                (bag[i].res == bag[best].res && bag[i].guid < bag[best].guid))
                best = i;
        }
        if (best == bag.size())
            continue;

        usedGuids.push_back(bag[best].guid);
        Swap swap;
        swap.slot = slot;
        swap.wornGuid = wornPiece ? wornPiece->guid : 0;
        swap.wornRes = base;
        swap.resistGuid = bag[best].guid;
        swap.resistRes = bag[best].res;
        swaps.push_back(swap);
    }
    return swaps;
}
}
