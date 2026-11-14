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
// No core includes: the caller passes the engine's own numbers (Spell::GetCastTime and
// Spell::GetCastedTime, through ReactionCastPct).

#include <cstdint>

namespace ai
{
    // Progress of a cast in percent: 0 at the start, 100 at the end. `remainingMs` is the core's countdown
    // (Spell::GetCastedTime, m_timer), which starts at the total and counts to 0. -1 when the total is unknown
    // (an instant spell, nothing on a bar to lose); a remaining above the total reads as a fresh cast. Same
    // number the CastCancel diagnostic rows record, so a walk's cast_pct column reads as the guard sees it.
    inline int ReactionCastPct(std::int32_t totalMs, std::uint32_t remainingMs)
    {
        if (totalMs <= 0)
            return -1;
        const std::int32_t remaining = remainingMs > static_cast<std::uint32_t>(totalMs)
            ? totalMs : static_cast<std::int32_t>(remainingMs);
        const int pct = static_cast<int>(100 * (totalMs - remaining) / totalMs);
        return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    }

    // Does a picked reaction break the bot's cast?
    // - not a cast-breaking reaction: no;
    // - key off, or a reaction that does not yield, or the bot is no healer, or the cast is no heal, or the cast's
    //   progress is unknown: break, as before;
    // - otherwise break only while the cast is under `guardPct` % done: past that the heal is too far along to drop
    //   for a positioning pass that usually ends in a no-op.
    inline bool ReactionCastBreakDue(bool shouldInterrupt, bool yieldsToHeal, int guardPct,
                                     bool botIsHealer, bool castIsHeal, int castPct)
    {
        if (!shouldInterrupt) return false;
        if (guardPct <= 0 || !yieldsToHeal || !botIsHealer || !castIsHeal || castPct < 0) return true;
        return castPct < guardPct;
    }
} // namespace ai
