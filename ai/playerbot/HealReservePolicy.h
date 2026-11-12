#pragma once

#include <cstdint>
#include <vector>

// Pure decisions for the healer top-up reserve (the `heal_reserve` boss-tactic row): outside the row's release window
// a healer tops up members in the almost-full tier only while its own mana is at or above the row's reserve percent.
// Two things release it: the window itself (the gather before the fight's cycle spell) and distress (the row's count
// of living non-tanks under the medium-health line, counted on the same predicted health the heal pick triages on).
// The distress release is latched per healer: once it fires it holds until the raid has been calm for
// `kHealReserveDistressHoldMs`, so a count hovering on the line cannot flap it. Tanks and the boss's current victim
// keep today's `MediumMana` rule at all times.
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

    // The latch's recovery margin in health points: the release stays while members sit under the medium line plus
    // this, so a member hovering on the line cannot flap the release.
    inline constexpr float kHealReserveRecoveryMarginPct = 10.0f;
    // How long the raid must stay calm before the latch lets go.
    inline constexpr std::uint32_t kHealReserveDistressHoldMs = 3000;

    // The distress counts of one raid read: `hurt` sits under the medium line and fires the release; `calm` sits
    // under the line plus the recovery margin and keeps it from clearing.
    struct HealReserveCounts
    {
        std::uint32_t hurt = 0;
        std::uint32_t calm = 0;
    };

    // One group member as the distress count reads it. `effectiveHealthPct` is the same predicted health the heal
    // pick triages on (EffectiveHealHealthPct), never raw health.
    struct HealReserveMember
    {
        bool alive = false;
        bool sameMap = false;
        bool tank = false;
        float effectiveHealthPct = 100.0f;
    };

    // The healer's own read of one member: alive non-tanks on its map count, the healer itself included, no distance
    // test.
    inline void HealReserveCountMember(HealReserveCounts& counts, const HealReserveMember& member,
                                       float mediumHealthPct)
    {
        if (!member.alive || !member.sameMap || member.tank)
            return;
        if (member.effectiveHealthPct < mediumHealthPct)
            ++counts.hurt;
        if (member.effectiveHealthPct < mediumHealthPct + kHealReserveRecoveryMarginPct)
            ++counts.calm;
    }

    // The per-healer distress latch: `released` holds the release, and `calm` times the run of calm frames. It belongs
    // to one fight (the boss guid and the fight's combat start), so a latch can never cross into the next pull.
    struct HealReserveLatch
    {
        bool released = false;
        bool calm = false;
        std::uint32_t calmSinceMs = 0;
        std::uint64_t fightBossGuid = 0;
        std::uint32_t fightStartMs = 0;
    };

    // One fight as the reserve identifies it: the live boss and the fight's combat start (the seek record's engage, or
    // the bot's own combat start without it).
    struct HealReserveFightId
    {
        std::uint64_t bossGuid = 0;
        std::uint32_t startMs = 0;
    };

    // The identity a read binds the latch to: the seek record's engage whenever the read carries one (exposed
    // directly, so the read that sets it already names the pull although its elapsed time is 0), else the bot's own
    // combat start. One source for the whole pull, so the latch cannot reset when the elapsed time first becomes
    // readable.
    inline HealReserveFightId HealReserveFightIdentity(std::uint64_t bossGuid, std::uint32_t engageMs,
                                                       std::uint32_t combatStartMs)
    {
        HealReserveFightId fight;
        fight.bossGuid = bossGuid;
        fight.startMs = engageMs != 0 ? engageMs : combatStartMs;
        return fight;
    }

    inline void HealReserveLatchReset(HealReserveLatch& latch)
    {
        latch.released = false;
        latch.calm = false;
        latch.calmSinceMs = 0;
    }

    // Bind the latch to this read's fight. Anything the latch held belongs to the old fight and is dropped the moment
    // either half of the identity differs; the fight's own later reads keep it.
    inline void HealReserveLatchBind(HealReserveLatch& latch, const HealReserveFightId& fight)
    {
        if (latch.fightBossGuid == fight.bossGuid && latch.fightStartMs == fight.startMs)
            return;
        HealReserveLatchReset(latch);
        latch.fightBossGuid = fight.bossGuid;
        latch.fightStartMs = fight.startMs;
    }

    // The latched distress release. Members under the medium line fire it; it then holds while at least the row's
    // count sit under the line plus the recovery margin, and lets go only after the hold time of fewer. A count of 0
    // is no distress release and drops any latch (the reserve is window-only).
    inline bool HealReserveDistressLatched(HealReserveLatch& latch, std::uint32_t hurtCount, std::uint32_t calmCount,
                                           std::uint32_t distressCount, std::uint32_t nowMs)
    {
        if (distressCount == 0)
        {
            HealReserveLatchReset(latch);
            return false;
        }
        if (HealReserveDistress(hurtCount, distressCount))
        {
            latch.released = true;
            latch.calm = false;
            latch.calmSinceMs = 0;
            return true;
        }
        if (!latch.released)
            return false;
        if (calmCount >= distressCount)
        {
            latch.calm = false;
            latch.calmSinceMs = 0;
            return true;
        }
        if (!latch.calm)
        {
            latch.calm = true;
            latch.calmSinceMs = nowMs;
        }
        if (nowMs - latch.calmSinceMs >= kHealReserveDistressHoldMs)
        {
            HealReserveLatchReset(latch);
            return false;
        }
        return true;
    }

    // Apply the latch to a pick: the latch is bound to this read's fight first, so a read of another fight starts
    // unreleased; then the distress release is the latch's, and `released` is the window or it. A read with no live
    // row is a fight's end, so the latch goes with it.
    inline void HealReserveApplyDistressLatch(HealReserveRead& read, HealReserveLatch& latch, std::uint32_t calmCount,
                                              std::uint32_t nowMs, const HealReserveFightId& fight)
    {
        HealReserveLatchBind(latch, fight);
        if (!read.live)
        {
            HealReserveLatchReset(latch);
            return;
        }
        read.distressReleased =
            HealReserveDistressLatched(latch, read.hurtCount, calmCount, read.distressCount, nowMs);
        read.released = read.windowReleased || read.distressReleased;
    }

    // The refusal count belongs to its row's window: a refusal counted more than two log gaps after the window's last
    // row starts a fresh count. The restamp moves the stamp one gap back instead of zeroing it, so the clear keeps
    // running when only the pick asks and no trigger row moves the stamp.
    inline bool HealReserveRefusalStale(std::uint32_t stampMs, std::uint32_t nowMs, std::uint32_t gapMs)
    {
        return stampMs != 0 && nowMs - stampMs > 2 * gapMs;
    }

    inline std::uint32_t HealReserveRefusalRestamp(std::uint32_t nowMs, std::uint32_t gapMs)
    {
        return nowMs - gapMs;
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
