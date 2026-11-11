#pragma once

// The seek_park boss tactic's pure rules (Hakkar's Sons of Hakkar today, any seek_aura source): a source the seek
// walked down is parked under a breakable control while the run waits for its cycle, nobody damages it, and at the
// row's release lead a tank takes it and the burn is forced so the kill (and the cloud) lands late. Tested in
// tools/test_seek_park_policy.cpp; plain numbers only, so this header stays free of core includes and the row's
// columns arrive exactly as BossTacticsMgr reads them.

#include <cstdint>
#include <cstring>
#include <vector>

namespace ai
{
    // The control is re-cast once this much of it is left: a Polymorph holds 41-43 s (walk 72 probe), so the refresh
    // lands with 8 s to spare for a 1.5 s cast and one resist.
    constexpr std::uint32_t kSeekParkRefreshMs = 8000;
    // The park needs this much room past the release lead to be worth starting: the first cast, one resist and the
    // budget.
    constexpr std::uint32_t kSeekParkMinHoldMs = 8000;
    // Two casts of one control are this far apart at the least.
    constexpr std::uint32_t kSeekParkCastGapMs = 2500;
    // The released source is held from damage this long after the release when no tank has it yet.
    constexpr std::uint32_t kSeekParkReleaseHoldMs = 4000;

    enum class SeekParkPhase : std::uint8_t
    {
        None = 0,    // new cycle, new source, or the source died
        Apply = 1,   // the park bot casts the control; nobody touches the source
        Held = 2,    // the source carries the control; the park bot refreshes it near its end
        Release = 3, // the receiver tank takes the source, the burn is forced, the raid gathers at the spot
        Failed = 4,  // today's take and burn at once
        Released = 5, // a tank had the source inside the release's window: the park is done, and it cannot fail
    };

    // The seek_park row as the read passes it: the key and row on, the release lead (`param`), the apply budget
    // (`window_s`) and the most re-applies after a break (`priority`).
    struct SeekParkRules
    {
        bool on = false;
        std::uint32_t releaseLeadMs = 0;
        std::uint32_t applyBudgetMs = 0;
        std::uint8_t maxReapplies = 0;
    };

    // One instance's park: the source and cycle it belongs to, the phase, the clocks the log and the cast read, the
    // re-apply count, the park bot and the failure's word.
    struct SeekParkState
    {
        std::uint64_t son = 0;
        std::uint32_t cycleNo = 0;
        std::uint32_t fightStartMs = 0; // the fight's engage clock, keyed with the cycle (no re-pull carry-over)
        SeekParkPhase phase = SeekParkPhase::None;
        std::uint32_t applySinceMs = 0;   // when the current Apply began
        std::uint32_t budgetSinceMs = 0;  // when the park bot first stood in cast reach this Apply
        std::uint32_t lastCastMs = 0;     // the park bot's last cast (the action notes it)
        std::uint32_t heldSinceMs = 0;    // when the current Held episode began
        std::uint32_t heldMs = 0;         // the completed Held episodes' total
        std::uint32_t releaseMs = 0;      // when Release began
        std::uint8_t reapplies = 0;
        std::uint64_t parkBot = 0;
        const char* why = "";
    };

    // What the step asks of the world this read: the source and the seek's own pick, whether it is alive, fighting,
    // inside the raid circle and the pick, whether it carries a breakable control and for how long, whether a park
    // bot is left and in cast reach, the due time, whether the source is on a tank, and whether a receiver qualifies.
    struct SeekParkInput
    {
        std::uint64_t son = 0;
        std::uint64_t pickedSon = 0; // the seek's picked source (the burn, kill and gather key on it)
        std::uint32_t cycleNo = 0;
        std::uint32_t fightStartMs = 0;
        bool sonAlive = false;
        bool sonInCombat = false;
        bool sonIsPick = false;
        bool sonInCircle = false;
        bool controlled = false;
        std::uint32_t controlLeftMs = 0;
        bool parkBotOk = false;
        bool controllerInReach = false;
        bool dueKnown = false;
        std::uint32_t dueInMs = 0;
        bool sonOnTank = false;
        bool receiverOk = false;
    };

    // Whether the release is live: the window is open (Release) or a tank completed it (Released). The release's
    // damage hold, gather, takes and lead read the same in both, so latching success changes nothing but the failure.
    inline bool SeekParkReleaseLive(SeekParkPhase phase)
    {
        return phase == SeekParkPhase::Release || phase == SeekParkPhase::Released;
    }

    // The row's word for a phase (the `phase=` field of the `seek park` row).
    inline const char* SeekParkPhaseWord(SeekParkPhase phase)
    {
        switch (phase)
        {
            case SeekParkPhase::Apply:
                return "apply";
            case SeekParkPhase::Held:
                return "held";
            case SeekParkPhase::Release:
                return "release";
            case SeekParkPhase::Released:
                return "released";
            case SeekParkPhase::Failed:
                return "failed";
            case SeekParkPhase::None:
                break;
        }
        return "none";
    }

    // Closes the current Held episode into heldMs (the state's own clock).
    inline void SeekParkCloseHeld(SeekParkState& state, std::uint32_t nowMs)
    {
        if (state.heldSinceMs != 0)
            state.heldMs += nowMs - state.heldSinceMs;
        state.heldSinceMs = 0;
    }

    // The time the source has been held so far, the running episode included (the `held_ms=` field).
    inline std::uint32_t SeekParkHeldMs(const SeekParkState& state, std::uint32_t nowMs)
    {
        if (state.phase != SeekParkPhase::Held || state.heldSinceMs == 0)
            return state.heldMs;
        return state.heldMs + (nowMs - state.heldSinceMs);
    }

    // The phase machine (D5). True: the phase changed. A new cycle, a new source, the key off or a dead source
    // starts the record over at None. Unsigned subtraction throughout; a nowMs of 0 is stored as 1.
    inline bool SeekParkStep(SeekParkState& state, const SeekParkRules& rules, const SeekParkInput& in,
                             std::uint32_t nowMs)
    {
        if (nowMs == 0)
            nowMs = 1;
        const SeekParkPhase before = state.phase;
        if (state.son != in.son || state.cycleNo != in.cycleNo || state.fightStartMs != in.fightStartMs ||
            !rules.on || !in.sonAlive)
        {
            state = SeekParkState();
            state.son = in.son;
            state.cycleNo = in.cycleNo;
            state.fightStartMs = in.fightStartMs;
            // A record that was in a phase starts its new one at None; a fresh record goes on into the None case.
            if (before != SeekParkPhase::None)
                return true;
            if (!rules.on || !in.sonAlive)
                return false;
        }
        // The park is only ever the seek's own pick: its burn, kill and gather key on that Son. A pick that moved away
        // while this record held or released ends the record at None (a new source starts a fresh park).
        if ((state.phase == SeekParkPhase::Held || SeekParkReleaseLive(state.phase)) &&
            in.pickedSon != state.son)
        {
            state = SeekParkState();
            state.son = in.son;
            state.cycleNo = in.cycleNo;
            state.fightStartMs = in.fightStartMs;
            return true;
        }
        switch (state.phase)
        {
            case SeekParkPhase::None:
            {
                // A source that is not fighting, is not the seek's pick, or stands outside the raid circle is no park:
                // the take would refuse its chase and the burn, kill and gather key on another Son.
                if (!in.sonInCombat || !in.sonIsPick || !in.sonInCircle)
                    return false;
                // A source that joined too late for a clean release, or one nobody can control, follows today's take
                // and burn at once.
                if (in.dueKnown && in.dueInMs <= rules.releaseLeadMs + kSeekParkMinHoldMs)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "late";
                    break;
                }
                if (!in.parkBotOk)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "no_bot";
                    break;
                }
                if (!in.dueKnown)
                    return false;
                state.phase = SeekParkPhase::Apply;
                state.applySinceMs = nowMs;
                state.budgetSinceMs = in.controllerInReach ? nowMs : 0;
                state.why = "";
                break;
            }
            case SeekParkPhase::Apply:
            {
                if (in.dueKnown && in.dueInMs <= rules.releaseLeadMs)
                {
                    state.phase = SeekParkPhase::Release;
                    state.releaseMs = nowMs;
                    state.why = "";
                    break;
                }
                if (in.controlled)
                {
                    state.phase = SeekParkPhase::Held;
                    state.heldSinceMs = nowMs;
                    break;
                }
                if (!in.parkBotOk)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "no_bot";
                    break;
                }
                // The budget counts from the first tick the controller stands in cast reach: the walk to the source
                // is not the control's time.
                if (in.controllerInReach && state.budgetSinceMs == 0)
                    state.budgetSinceMs = nowMs;
                if (state.budgetSinceMs != 0 && nowMs - state.budgetSinceMs > rules.applyBudgetMs)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "budget";
                }
                break;
            }
            case SeekParkPhase::Held:
            {
                if (in.dueKnown && in.dueInMs <= rules.releaseLeadMs)
                {
                    SeekParkCloseHeld(state, nowMs);
                    state.phase = SeekParkPhase::Release;
                    state.releaseMs = nowMs;
                    state.why = "";
                    break;
                }
                if (!in.parkBotOk)
                {
                    SeekParkCloseHeld(state, nowMs);
                    state.phase = SeekParkPhase::Failed;
                    state.why = "no_bot";
                    break;
                }
                if (!in.controlled)
                {
                    // The control broke: count it and re-apply, up to the row's cap (a third break near the source
                    // means something keeps breaking it, so today's take is the cheaper answer).
                    SeekParkCloseHeld(state, nowMs);
                    ++state.reapplies;
                    if (state.reapplies > rules.maxReapplies)
                    {
                        state.phase = SeekParkPhase::Failed;
                        state.why = "breaks";
                    }
                    else
                    {
                        state.phase = SeekParkPhase::Apply;
                        state.applySinceMs = nowMs;
                        state.budgetSinceMs = in.controllerInReach ? nowMs : 0;
                        state.why = "";
                    }
                }
                break;
            }
            case SeekParkPhase::Release:
            {
                // The release's own window: the source must be on a tank within kSeekParkReleaseHoldMs, whether a
                // receiver exists or not. The first tick a tank has it completes the release for good: a later threat
                // change, control effect, death or retarget cannot turn a release that happened into a failure.
                if (in.sonOnTank)
                {
                    state.phase = SeekParkPhase::Released;
                    state.why = "";
                    break;
                }
                // An eligible tank that cannot reach, taunt or attack is no release, and the park fails to today's
                // take.
                if (state.releaseMs != 0 && nowMs - state.releaseMs >= kSeekParkReleaseHoldMs)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "no_receiver";
                }
                break;
            }
            case SeekParkPhase::Released:
                break;
            case SeekParkPhase::Failed:
                break;
        }
        return state.phase != before;
    }

    // Whether the phase holds every source damage: Apply and Held (nobody touches a parked source, the park bot
    // included: the control breaks on any damage).
    inline bool SeekParkHolds(SeekParkPhase phase)
    {
        return phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held;
    }

    // Whether a tank task on the parked source is vetoed in this phase: every task in Apply and Held (the meet, the
    // receiver, the pickup, the pull join, the nearest take, the healer clock, the victim relief, the drag and the
    // pull), and every task but the release's own take in the release while the source is on a tank. In the release
    // with the source on a non-tank (the control broke onto someone, or a latched release lost its tank afterwards)
    // the normal takes apply: the park's task is the release's own, and the ordinary nearest, healer-clock and relief
    // picks may take the source back.
    inline bool SeekParkVetoesTankTask(SeekParkPhase phase, const char* takeSrc, bool sonOnTank)
    {
        if (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held)
            return true;
        if (!SeekParkReleaseLive(phase) || !sonOnTank)
            return false;
        return !(takeSrc && std::strcmp(takeSrc, "park_release") == 0);
    }

    // Whether the wrapped tank body is told to skip this source outright: Apply and Held (every tank task is the
    // park's), and the release on a tank for every bot but the receiver (its own-source drag is the release's second
    // half). The body then returns its next task (another Son on a healer) instead of an empty one.
    inline bool SeekParkExcludesSource(SeekParkPhase phase, bool sonOnTank, bool isReceiver)
    {
        if (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held)
            return true;
        if (!SeekParkReleaseLive(phase) || !sonOnTank)
            return false;
        return !isReceiver;
    }

    // Whether this bot's attack on `target` may outrank the invalid-target rule: the release's receiver, its released
    // source, and only while the release is live. A polymorphed Son is otherwise no valid current target, so the
    // receiver's Attack() would be undone by "select new target" every tick.
    inline bool SeekParkReleaseTargetAllowed(SeekParkPhase phase, bool isReceiver, bool isParkSon)
    {
        return SeekParkReleaseLive(phase) && isReceiver && isParkSon;
    }

    // The late-engage lead the fight read burns with: a parked or released source is not burned on arrival (its
    // whole point is to die late), so the row's release lead replaces the seek_pull lead while the park holds or
    // releases. Every other phase, and every read whose pick is not the parked Son, burns at the seek_pull lead.
    inline std::uint32_t SeekParkLateLeadMs(SeekParkPhase phase, bool rulesOn, bool parkedIsPick,
                                            std::uint32_t pullLeadMs, std::uint32_t releaseLeadMs)
    {
        if (!rulesOn || !parkedIsPick)
            return pullLeadMs;
        if (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held || SeekParkReleaseLive(phase))
            return releaseLeadMs;
        return pullLeadMs;
    }

    // Whether the park bot casts this tick: in Apply at once and then every kSeekParkCastGapMs (the first cast, a
    // resist, the retry); in Held only once the control has kSeekParkRefreshMs or less left, with the same gap.
    inline bool SeekParkCastDue(const SeekParkState& state, const SeekParkInput& in, std::uint32_t nowMs)
    {
        const bool gapOk = state.lastCastMs == 0 || nowMs - state.lastCastMs >= kSeekParkCastGapMs;
        if (state.phase == SeekParkPhase::Apply)
            return gapOk;
        if (state.phase == SeekParkPhase::Held)
            return gapOk && in.controlLeftMs <= kSeekParkRefreshMs;
        return false;
    }

    // The damage dealers stay off the released source until its victim is a tank, or kSeekParkReleaseHoldMs after
    // the release, whichever is first.
    inline bool SeekParkDpsHeld(SeekParkPhase phase, bool sonOnTank, std::uint32_t releaseMs, std::uint32_t nowMs)
    {
        if (!SeekParkReleaseLive(phase) || sonOnTank || releaseMs == 0)
            return false;
        return nowMs - releaseMs < kSeekParkReleaseHoldMs;
    }

    // In the release the gather point is the seek spot while the source still stands more than kSeekSpotYd (8 yd)
    // from it: the tank drags the source there and the cloud lands on the raid, not wherever the source was parked.
    inline bool SeekParkGatherAtSpot(SeekParkPhase phase, float sonToSpotYd)
    {
        return SeekParkReleaseLive(phase) && sonToSpotYd > 8.0f;
    }

    // A fixed spread slot gives way while the park bot walks to its source or holds it: a slot walk would cut the
    // control's cast (Apply) and interrupt the refresh (Held).
    inline bool SeekParkExemptsBot(SeekParkPhase phase, std::uint64_t parkBot, std::uint64_t botGuid)
    {
        return (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held) && parkBot != 0 && parkBot == botGuid;
    }

    // One park candidate: eligible, the control's rank (0 Polymorph, 1 Hibernate), its 2D distance to the source and
    // its raw guid.
    struct SeekParkCand
    {
        bool eligible = false;
        int spellRank = 0;
        float sonYd = 0.0f;
        std::uint64_t guid = 0;
    };

    // The park bot (D6): Polymorph before Hibernate, the nearer to the source, the lower raw guid; -1 for none.
    inline int PickSeekParkBot(const std::vector<SeekParkCand>& cands)
    {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(cands.size()); ++i)
        {
            const SeekParkCand& cand = cands[static_cast<std::size_t>(i)];
            if (!cand.eligible)
                continue;
            if (pick < 0)
            {
                pick = i;
                continue;
            }
            const SeekParkCand& best = cands[static_cast<std::size_t>(pick)];
            if (cand.spellRank != best.spellRank)
            {
                if (cand.spellRank < best.spellRank)
                    pick = i;
            }
            else if (cand.sonYd != best.sonYd)
            {
                if (cand.sonYd < best.sonYd)
                    pick = i;
            }
            else if (cand.guid < best.guid)
                pick = i;
        }
        return pick;
    }
}
