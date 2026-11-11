#pragma once

#include <cstdint>
#include <vector>

// Pure decisions for the healer top-up reserve (the `heal_reserve` boss-tactic row): outside the row's release window
// a healer tops up members in the almost-full tier only while its own mana is at or above the row's reserve percent.
// Two things release it: the window itself (the gather before the fight's cycle spell) and distress (the row's count
// of living non-tanks under the medium-health line). Tanks and the boss's current victim keep today's `MediumMana`
// rule at all times.
//
// No core includes: callers in strategy/ translate game state into these plain inputs, so the rules stay testable in
// tools/test_heal_reserve_policy.cpp without the server.

namespace ai
{
    struct HealReserveRead
    {
        bool live = false;
        float reservePct = 0.0f;
        // The winning row's release state: `released` is the OR of the two, so today's callers keep one question.
        bool released = false;
        bool windowReleased = false;
        bool distressReleased = false;
        // The winning row's distress count (0: no distress release) and the hurt count the caller read for this pick.
        std::uint32_t distressCount = 0;
        std::uint32_t hurtCount = 0;
        // The winning row's boss: the caller resolves the unit from it, so the boss and its victim never depend on
        // another kind's read (the seek clock's row).
        std::uint32_t bossEntry = 0;
        // The release clock the read used (the seek_aura cycle's): known only with that key and a live cycle.
        bool clockKnown = false;
        std::uint32_t dueInMs = 0;
    };

    // One live `heal_reserve` row as the pick sees it: its reserve percent, its release lead, its boss and the count
    // of living non-tanks under the medium-health line that releases it outside the window (0: never).
    struct HealReserveRow
    {
        float reservePct = 0.0f;
        float windowSec = 0.0f;
        std::uint32_t bossEntry = 0;
        std::uint32_t distressCount = 0;
    };

    // The almost-full top-up tier: the only health band the reserve governs. A member below `mediumPct` is another
    // tier and a refusal for it is the ordinary mana rule, not the reserve's.
    inline bool HealReserveTopUpTier(float healthPct, float mediumPct, float almostFullPct)
    {
        return healthPct >= mediumPct && healthPct < almostFullPct;
    }

    // The window release: the reserve lets go while the cycle clock is known and the next siphon is at most
    // `releaseLeadSec` away. No clock or a lead of 0 never releases it.
    inline bool HealReserveReleased(bool clockKnown, std::uint32_t dueInMs, float releaseLeadSec)
    {
        if (!clockKnown || !(releaseLeadSec > 0.0f))
            return false;
        return static_cast<float>(dueInMs) <= releaseLeadSec * 1000.0f;
    }

    // Outside the window the reserve also lets go while at least the row's count of living non-tanks sit under the
    // medium-health line. One hurt member is routine and the medium tier heals it; a count of 0 never releases.
    inline bool HealReserveDistress(std::uint32_t hurtCount, std::uint32_t distressCount)
    {
        return distressCount > 0 && hurtCount >= distressCount;
    }

    // Several live rows keep the highest `param`; its `window_s`, distress count and boss go with it. A tie keeps the
    // first row. `hurtCount` is the caller's raid read for this pick; 0 skips that read.
    inline HealReserveRead HealReservePick(const std::vector<HealReserveRow>& rows, bool clockKnown,
                                           std::uint32_t dueInMs, std::uint32_t hurtCount)
    {
        HealReserveRead out;
        out.clockKnown = clockKnown;
        out.dueInMs = clockKnown ? dueInMs : 0;
        out.hurtCount = hurtCount;
        for (const HealReserveRow& row : rows)
        {
            if (out.live && row.reservePct <= out.reservePct)
                continue;
            out.live = true;
            out.reservePct = row.reservePct;
            out.windowReleased = HealReserveReleased(clockKnown, dueInMs, row.windowSec);
            out.distressReleased = HealReserveDistress(hurtCount, row.distressCount);
            out.released = out.windowReleased || out.distressReleased;
            out.distressCount = row.distressCount;
            out.bossEntry = row.bossEntry;
        }
        return out;
    }

    // The row's reason word. The window wins when both releases hold.
    inline const char* HealReserveWhy(const HealReserveRead& read)
    {
        if (read.windowReleased)
            return "window";
        if (read.distressReleased)
            return "distress";
        return "none";
    }

    // The reserve's effective line: the row's percent, never below today's MediumMana rule.
    inline float HealReserveLine(const HealReserveRead& read, std::uint8_t mediumManaPct)
    {
        return read.reservePct > static_cast<float>(mediumManaPct) ? read.reservePct :
                                                                     static_cast<float>(mediumManaPct);
    }

    // The reserve refuses a top-up only while it is live, released by neither its window nor distress, for a
    // target that is not exempt, with the healer under its line. Everything else is today's rule. The pick reads this
    // to skip a refused top-up candidate; the gate reads it to answer the trigger.
    inline bool HealReserveRefuses(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                   std::uint8_t mediumManaPct, bool targetExempt)
    {
        if (!flagOn || !read.live || targetExempt || read.released)
            return false;
        return static_cast<float>(manaPct) < HealReserveLine(read, mediumManaPct);
    }

    // The candidate-level reserve decision both gates read: the reserve governs the almost-full top-up tier only,
    // and `healthPct` must be the effective health triage reads (HealingTriagePolicy.h HealEffectiveHealthPct /
    // PredictedHealHealth), never raw health. A member the incoming damage takes below the tier is a lower tier's
    // heal and is never refused; one the credited incoming heals lift into the tier is a top-up like any other.
    inline bool HealReserveSkipsTopUp(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                      std::uint8_t mediumManaPct, float healthPct, float mediumHealthPct,
                                      float almostFullPct, bool targetExempt)
    {
        return HealReserveTopUpTier(healthPct, mediumHealthPct, almostFullPct) &&
            HealReserveRefuses(flagOn, read, manaPct, mediumManaPct, targetExempt);
    }

    // Today's rule (`mana >= mediumMana`) whenever the key is off, no row is live, the target is exempt, either
    // release holds or the effective health is another tier; else the reserve, never below the medium line.
    inline bool HealTopUpAllowedAtHealth(bool flagOn, const HealReserveRead& read, std::uint8_t manaPct,
                                         std::uint8_t mediumManaPct, float healthPct, float mediumHealthPct,
                                         float almostFullPct, bool targetExempt)
    {
        if (HealReserveSkipsTopUp(flagOn, read, manaPct, mediumManaPct, healthPct, mediumHealthPct, almostFullPct,
                targetExempt))
            return false;
        return manaPct >= mediumManaPct;
    }
}
