#pragma once

// Pure decision rules for the Four Horsemen mark rotation (Naxxramas, 533).
// Behaviour harvested from mod-playerbots @ 79bd4281
// (src/Ai/Raid/Naxx/Action/NaxxActions_FourHorsemen.cpp,
// src/Ai/Raid/Naxx/NaxxBossHelper.h FourHorsemenBossHelper): bots trade
// horsemen so nobody keeps stacking one mark. The donor drives a 3.x clock
// over two fixed WotLK spots where Lady and Sir stand still; vanilla has
// no such spots, so it is reimplemented as a stack-driven rotation:
// - Core truth (tortoise-wow boss_four_horsemen.cpp + tw_world_creature):
//   the four spawn in a line (2513-2524, -2955..-2944) and have no
//   waypoints; the raid's tanks drag each one to its own corner. A
//   horseman's live position IS its corner, so the corner positions are
//   read from the live units, not from a fixed table.
// - At kFourHorsemenRotateStacks of one mark the bot goes to the next
//   corner clockwise around the horsemen's centre, preferring the corner
//   whose mark it carries fewest stacks of.
// Marks: Thane Korth'azz 28832 (16064), Lady Blaumeux 28833 (16065),
// Highlord Mograine 28834 (16062), Sir Zeliek 28835 (16063).
// No core includes: the action translates game state into plain inputs,
// so tools/test_four_horsemen_rotation_policy.cpp can test the rules.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ai
{
    // Trigger threshold (stack 3 deals 1000, stack 4 3000, stack 5 5000:
    // boss_four_horsemen.cpp SpellHitTarget), kept at the legacy trigger's 3.
    constexpr uint32_t kFourHorsemenRotateStacks = 3;
    constexpr std::size_t kFourHorsemenCount = 4;

    // Index order shared by the mark and entry tables below.
    constexpr uint32_t kFourHorsemenMarks[kFourHorsemenCount] = { 28832, 28833, 28834, 28835 };
    constexpr uint32_t kFourHorsemenEntries[kFourHorsemenCount] = { 16064, 16065, 16062, 16063 };

    // Where a non-tank waits at its new corner: melee next to the
    // horseman, ranged and healers this far out toward the room centre.
    constexpr float kFourHorsemenMeleeOffsetYd = 3.0f;
    constexpr float kFourHorsemenRangedOffsetYd = 20.0f;
    // Slack around the stand-off before the bot counts as arrived: stop
    // re-issuing the move so the combat engine can fight from there.
    constexpr float kFourHorsemenArrivedYd = 4.0f;

    // Index into the tables for a horseman creature entry, or -1.
    inline int FourHorsemenIndexForEntry(uint32_t entry)
    {
        for (std::size_t i = 0; i < kFourHorsemenCount; ++i)
        {
            if (kFourHorsemenEntries[i] == entry)
                return static_cast<int>(i);
        }
        return -1;
    }

    // One horseman as the bot sees it.
    struct HorsemanSpot
    {
        bool alive = false;   // a living horseman was found (position valid)
        float x = 0.0f;
        float y = 0.0f;
        uint32_t stacks = 0;  // the bot's stacks of this horseman's mark
    };

    // The mark that needs rotating away from: the highest stack count at or
    // above the threshold, or -1 when nothing needs to move.
    inline int FourHorsemenStackingIndex(const HorsemanSpot* spots)
    {
        int best = -1;
        for (std::size_t i = 0; i < kFourHorsemenCount; ++i)
        {
            if (spots[i].stacks < kFourHorsemenRotateStacks)
                continue;
            if (best < 0 || spots[i].stacks > spots[best].stacks)
                best = static_cast<int>(i);
        }
        return best;
    }

    // Clockwise angular distance (0, 2*pi] from `from` to `to` around
    // (cx, cy). WoW angles grow counter-clockwise seen from above, so
    // clockwise means the angle falls.
    inline float FourHorsemenClockwiseGap(float cx, float cy, float fromX, float fromY, float toX, float toY)
    {
        const float twoPi = 6.28318530718f;
        float a = std::atan2(fromY - cy, fromX - cx);
        float b = std::atan2(toY - cy, toX - cx);
        float gap = std::fmod(a - b, twoPi);
        if (gap <= 0.0f)
            gap += twoPi;
        return gap;
    }

    // The corner to rotate to: a living horseman other than the stacking
    // one, fewest own stacks first, then the nearest clockwise from the
    // stacking corner (index order when the stacking horseman is gone).
    // `currentIdx` is the bot's stored choice while it holds, else the
    // horseman it stands at now (-1 if unknown): a bot that already rotated
    // to a corner still under the threshold stays there instead of hopping
    // on at every new mark tick.
    // -1 when no living alternative exists (stay and fight).
    inline int FourHorsemenNextIndex(const HorsemanSpot* spots, int stackingIdx, int currentIdx)
    {
        if (stackingIdx < 0 || stackingIdx >= static_cast<int>(kFourHorsemenCount))
            return -1;
        if (currentIdx >= 0 && currentIdx < static_cast<int>(kFourHorsemenCount) &&
            currentIdx != stackingIdx && spots[currentIdx].alive &&
            spots[currentIdx].stacks < kFourHorsemenRotateStacks)
            return currentIdx;
        float cx = 0.0f, cy = 0.0f;
        int alive = 0;
        for (std::size_t i = 0; i < kFourHorsemenCount; ++i)
        {
            if (!spots[i].alive)
                continue;
            cx += spots[i].x;
            cy += spots[i].y;
            ++alive;
        }
        if (alive == 0)
            return -1;
        cx /= alive;
        cy /= alive;
        const HorsemanSpot& from = spots[stackingIdx];
        int best = -1;
        float bestGap = 0.0f;
        for (std::size_t i = 0; i < kFourHorsemenCount; ++i)
        {
            if (static_cast<int>(i) == stackingIdx || !spots[i].alive)
                continue;
            float gap = from.alive
                ? FourHorsemenClockwiseGap(cx, cy, from.x, from.y, spots[i].x, spots[i].y)
                : static_cast<float>((i + kFourHorsemenCount - static_cast<std::size_t>(stackingIdx)) % kFourHorsemenCount);
            if (best < 0 || spots[i].stacks < spots[best].stacks ||
                (spots[i].stacks == spots[best].stacks && gap < bestGap))
            {
                best = static_cast<int>(i);
                bestGap = gap;
            }
        }
        return best;
    }

    // The bot's rotation choice is stored per bot ("manual int" values,
    // index + 1, 0 = none): the mark it rotated away from and the corner it
    // chose. FourHorsemenRotateAction writes it, DpsTargetValue reads it.
    constexpr const char* kFourHorsemenFromKey = "four horsemen rotate from";
    constexpr const char* kFourHorsemenToKey = "four horsemen rotate to";

    // A stored choice still holds while the bot carries 3+ stacks of the
    // mark it left and the chosen horseman lives with its mark still under
    // the threshold. While it holds the bot keeps walking to that corner
    // (the nearest horseman mid-walk does not take over) and its dps target
    // is the chosen horseman. It lapses on its own once the old mark drops
    // under 3 or the chosen corner stacks up or dies.
    inline bool FourHorsemenChoiceHolds(const HorsemanSpot* spots, int fromIdx, int toIdx)
    {
        const int count = static_cast<int>(kFourHorsemenCount);
        if (fromIdx < 0 || fromIdx >= count || toIdx < 0 || toIdx >= count || fromIdx == toIdx)
            return false;
        return spots[fromIdx].stacks >= kFourHorsemenRotateStacks &&
            spots[toIdx].alive && spots[toIdx].stacks < kFourHorsemenRotateStacks;
    }

    // At the chosen corner already: the nearest living horseman is the
    // chosen one and the bot is within its stand-off (+ slack). Lets melee
    // stand on any side of the horseman without re-issuing the move.
    inline bool FourHorsemenArrived(bool nearestIsChosen, float distToChosen, float offset)
    {
        return nearestIsChosen && distToChosen <= offset + kFourHorsemenArrivedYd;
    }

    // Where the bot stands at the chosen corner: `offset` yards from the
    // horseman toward the horsemen's centre (the open room, not the wall).
    inline void FourHorsemenCornerSpot(const HorsemanSpot* spots, int idx, float offset, float& outX, float& outY)
    {
        float cx = 0.0f, cy = 0.0f;
        int alive = 0;
        for (std::size_t i = 0; i < kFourHorsemenCount; ++i)
        {
            if (!spots[i].alive)
                continue;
            cx += spots[i].x;
            cy += spots[i].y;
            ++alive;
        }
        outX = spots[idx].x;
        outY = spots[idx].y;
        if (alive == 0)
            return;
        cx /= alive;
        cy /= alive;
        float dx = cx - outX, dy = cy - outY;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.01f)
            return;
        float step = offset < len ? offset : len;
        outX += dx / len * step;
        outY += dy / len * step;
    }
} // namespace ai
