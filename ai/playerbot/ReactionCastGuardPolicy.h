#pragma once

// Pure rule behind the reaction engine's cast break (PlayerbotAI::UpdateAIReaction) and
// AiPlayerbot.ReactionHealCastGuardPct.
//
// UpdateAIReaction breaks the bot's cast as soon as FindReaction PICKS a reaction whose
// action says it may (ShouldReactionInterruptCast), before that action's Execute can decide
// it has nothing to do. Every MovementAction says it may. Over walk 102's 150 healer-fights
// that cost each healer 10.5 casts a fight (the cut lands at a median 4 % of the cast, 79 %
// of the cuts under 10 %): `boss tactics seek` answers 74.5 % of the cuts the diagnostics
// could label, `raid spread` most of the rest, and their typical pass ends without a step
// (the seek's at-spot path returns false, a held slot does not walk).
//
// The guard stands only those two actions down (ReactionYieldsToHealCast), and only for a
// healer's heal that has not got far: the key's percent is the opening of the cast a
// yielding reaction may still cut, so the walk value 1 keeps a heal from its first percent
// on and the median 4 % cut is kept. Key off (0) is today's rule, and every other reaction
// still cuts: freeze, flee, bomb runout, avoid aoe, hazard, potions, victim escape, BoP and
// the dispels. A seek or spread that really walks still breaks the cast itself: MoveTo
// interrupts once it has a path.
//
// No core includes: the caller passes the generic slot's own read (Spell::getState,
// Spell::GetCastTime, Spell::GetCastedTime and, for a channel, the channel's duration) as a
// ReactionCastRead. This core counts a cast bar down in PREPARING and runs the channel phase
// in CASTING; it has no separate channeling state, the compat shim maps
// SPELL_STATE_CHANNELING onto CASTING.

#include <cmath>
#include <cstdint>
#include <limits>

namespace ai
{
    // Spell states from game/Spells/Spell.h, numeric to stay core-free.
    enum ReactionCastState : std::uint32_t
    {
        REACTION_CAST_PREPARING = 1,
        REACTION_CAST_CASTING = 2,
    };

    // The bot's generic cast slot as the caller reads it. A slot with no spell is the caller's null pointer.
    struct ReactionCastRead
    {
        std::uint32_t state = 0;       // Spell::getState()
        std::int32_t totalMs = 0;      // the countdown's total: the cast time on a bar, the channel's duration in CASTING
        std::uint32_t remainingMs = 0; // Spell::GetCastedTime() (m_timer), which starts at the total and counts to 0
        bool isHeal = false;           // the spell heals outright or over time, by its effects
    };

    // Is a cast under way to protect? A cast bar in PREPARING or a channel in CASTING, still with time left on
    // its countdown. NULL, FINISHED, IDLE and DELAYED (an instant whose missile is in flight) sit on no bar.
    inline bool ReactionCastRunning(ReactionCastRead const& cast)
    {
        return (cast.state == REACTION_CAST_PREPARING || cast.state == REACTION_CAST_CASTING) && cast.remainingMs > 0;
    }

    // Progress of the running cast in percent: 0 at the start, 100 at the end; -1 when no cast is running or its
    // total is unknown (an instant, nothing on a bar to lose). A remaining above the total reads as a fresh cast.
    // Same number the CastCancel diagnostic rows record, so a walk's cast_pct column reads as the guard sees it.
    inline int ReactionCastPct(ReactionCastRead const& cast)
    {
        if (!ReactionCastRunning(cast) || cast.totalMs <= 0)
            return -1;
        const std::int32_t remaining = cast.remainingMs > static_cast<std::uint32_t>(cast.totalMs)
            ? cast.totalMs : static_cast<std::int32_t>(cast.remainingMs);
        const int pct = static_cast<int>(100 * (cast.totalMs - remaining) / cast.totalMs);
        return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    }

    // Does a picked reaction break the bot's cast?
    // - not a cast-breaking reaction: no;
    // - key off, or a reaction that does not yield, or the bot is no healer: break, as before;
    // - no cast slot, a spell that does not heal, or no progress to read: break, as before;
    // - otherwise break only while the cast is under `guardPct` % done: past that the heal is too far along to drop
    //   for a positioning pass that usually ends in a no-op.
    inline bool ReactionCastBreakDue(bool shouldInterrupt, bool yieldsToHeal, int guardPct,
                                     bool botIsHealer, ReactionCastRead const* cast)
    {
        if (!shouldInterrupt) return false;
        if (guardPct <= 0 || !yieldsToHeal || !botIsHealer) return true;
        if (!cast || !cast->isHeal) return true;
        const int pct = ReactionCastPct(*cast);
        return pct < 0 || pct < guardPct;
    }

    // The seek's heal wait (AiPlayerbot.ReactionHealSeekWaitMs): `boss tactics seek` walks the bot into the row's
    // cloud or to the gather around a dying source, and its walk (MoveTo) breaks whatever cast is under way. A healer
    // that is nearly through a heal has nothing to gain from that: the walk stands for the rest of the cast when the
    // remainder, the walk there and a margin all fit inside the time left to the siphon.
    //
    // Walk 103 measured 410 `seek_move` rows with a heal under way (a median cut at 43 % of the cast, ~3.1 per
    // healer-fight). 94 % of the known-spell rows had 2 s or less left and the siphon was still due 3 s or more past
    // that: the heal was worth more than the walk, so the walk is what should have waited.
    constexpr std::uint32_t kSeekHealWaitMarginMs = 3000;  // slack over the walk's own length
    constexpr std::uint32_t kSeekHealRewaitMs = 5000;      // no second wait within this of the last one's end
    constexpr std::uint32_t kSeekHealWaitEndSlackMs = 200; // the wait reads as over this long past its end

    // How long a walk of `distYd` takes at `speedYdS`. A speed of 0 (rooted, or no run speed read to trust) makes
    // the walk unbounded, which refuses every wait: the bot walks as before.
    inline std::uint32_t SeekTravelMs(float distYd, float speedYdS)
    {
        if (!(speedYdS > 0.0f))
            return std::numeric_limits<std::uint32_t>::max();
        if (!(distYd > 0.0f))
            return 0;
        const double ms = static_cast<double>(distYd) / static_cast<double>(speedYdS) * 1000.0;
        const double limit = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        return ms >= limit ? std::numeric_limits<std::uint32_t>::max() : static_cast<std::uint32_t>(std::ceil(ms));
    }

    // One seek pass as the wait reads it: the key, the bot's role, the generic slot's read, the time left to the
    // siphon, the walk's own length, the cloud's time left when the walk is into one, and the wait's own state.
    struct SeekHealWaitIn
    {
        int waitMaxMs = 0;                        // AiPlayerbot.ReactionHealSeekWaitMs, clamped 0-3000
        bool botIsHealer = false;
        ReactionCastRead const* cast = nullptr;   // the bot's generic slot (NULL: no cast)
        std::uint32_t dueInMs = 0;                // the seek's task.dueInMs: 0 means the clock is unknown
        std::uint32_t travelMs = 0;               // SeekTravelMs(dist, the bot's run speed)
        std::int32_t cloudLeftMs = -1;            // the cloud's own duration left (cloud phase); -1: the gather
        bool inWait = false;                      // a wait already runs (its cast's remainder has not run out)
        std::uint32_t msSinceWaitEnd = std::numeric_limits<std::uint32_t>::max(); // none yet
    };

    // Does the seek stand its walk down for the heal under way?
    // - no key, no healer, no cast / not a heal / no progress to read: no;
    // - more of the cast left than the key allows: no (the walk goes, and breaks it as before);
    // - the siphon due before the cast, the walk and the margin are done: no (walking late beats healing now);
    // - a cloud that runs out inside that same time: no (the poison on the ground is gone by the time it would land);
    // - a wait that ended within kSeekHealRewaitMs: no (the bot walks, so a chain of heals cannot park it).
    inline bool SeekHealWaitDue(SeekHealWaitIn const& in)
    {
        if (in.waitMaxMs <= 0) return false;
        if (!in.botIsHealer) return false;
        if (!in.cast || !in.cast->isHeal) return false;
        if (ReactionCastPct(*in.cast) < 0) return false;
        // ReactionCastPct above proves a total above 0; a countdown over it reads as a fresh cast.
        const std::uint32_t rem = in.cast->remainingMs > static_cast<std::uint32_t>(in.cast->totalMs)
            ? static_cast<std::uint32_t>(in.cast->totalMs) : in.cast->remainingMs;
        if (rem > static_cast<std::uint32_t>(in.waitMaxMs)) return false;
        const std::uint64_t need = static_cast<std::uint64_t>(rem) + in.travelMs + kSeekHealWaitMarginMs;
        if (in.dueInMs == 0 || static_cast<std::uint64_t>(in.dueInMs) < need) return false;
        if (in.cloudLeftMs >= 0 && static_cast<std::uint64_t>(in.cloudLeftMs) < need) return false;
        if (!in.inWait && in.msSinceWaitEnd < kSeekHealRewaitMs) return false;
        return true;
    }
} // namespace ai
