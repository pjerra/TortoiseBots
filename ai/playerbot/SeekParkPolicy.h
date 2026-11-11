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
    // The receiver starts its walk to the parked source this long before the release lead.
    constexpr std::uint32_t kSeekParkPreWalkMs = 8000;
    // The release waits this long past the lead for a receiver in strike reach (one 10 s charm less the pre-walk's
    // margin).
    constexpr std::uint32_t kSeekParkReceiverWaitMs = 8000;
    // The release's window while a receiver exists (kSeekParkReleaseHoldMs stays the window with none).
    constexpr std::uint32_t kSeekParkReleaseMaxMs = 8000;
    // Apply ends this long after its budget whatever the park bot's reach was.
    constexpr std::uint32_t kSeekParkWalkCapMs = 6000;
    // The receiver strikes from this reach (Unit::GetDistance, edge to edge) and stands inside it while it waits.
    constexpr float kSeekParkStrikeYd = 5.0f;
    // The receiver's walk aims this far from the source's centre.
    constexpr float kSeekParkStandYd = 2.0f;

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
        // The cycle that started this park (or declined one) is spent: no second park in it, for any source.
        bool spent = false;
        // The release's receiver this read (the caller's pick, not the machine's).
        std::uint64_t receiver = 0;
        // The first Held read at or under the release lead (0: not waiting).
        std::uint32_t leadSinceMs = 0;
        // The park bot swaps this Apply survived (one restarts its budget clock).
        std::uint8_t botSwaps = 0;
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
        std::uint64_t parkBot = 0;      // the park bot this read picked (0: none)
        bool receiverInReach = false;   // the release's receiver stands in strike reach
    };

    // Whether the source counts as taken by a tank: its victim is a player tank, that tank's own current target is the
    // source, and the source carries no breakable control. A creature under Polymorph keeps its old victim, and that
    // stale victim still targets anything; only a tank that has actually engaged the woken source counts.
    inline bool SeekParkSonOnTank(bool victimIsTank, bool tankEngaged, bool controlled)
    {
        return victimIsTank && tankEngaged && !controlled;
    }

    // Whether the release's receiver stands close enough to strike the source this tick (edge to edge).
    inline bool SeekParkStrikeInReach(float reachYd)
    {
        return reachYd <= kSeekParkStrikeYd;
    }

    // How long the release may run: with a receiver the extra wait's window, without one today's.
    inline std::uint32_t SeekParkReleaseWindowMs(bool receiverOk)
    {
        return receiverOk ? kSeekParkReleaseMaxMs : kSeekParkReleaseHoldMs;
    }

    // Whether the release is live: the window is open (Release) or a tank completed it (Released). The release's
    // damage hold, gather, takes and lead read the same in both, so latching success changes nothing but the failure.
    inline bool SeekParkReleaseLive(SeekParkPhase phase)
    {
        return phase == SeekParkPhase::Release || phase == SeekParkPhase::Released;
    }

    // Whether the release may order its own take in this read: only while the source has no tank victim. A Son already
    // on a tank is that tank's (the take's swing is what hands it over); ordering another tank onto it is the
    // tank-to-tank handoff walk 73 measured as harmful.
    inline bool SeekParkReleaseTakeDue(bool releaseLive, bool sonOnTank, bool isReceiver)
    {
        return releaseLive && !sonOnTank && isReceiver;
    }

    // Whether this bot is the release's tank: the tank holding the source while it is on one (that holder keeps it),
    // else the scan's receiver while the source has no tank victim. Every other tank's park path skips the source.
    inline bool SeekParkReleaseTank(bool releaseLive, bool sonOnTank, bool isHolder, bool isReceiver)
    {
        if (!releaseLive)
            return false;
        return sonOnTank ? isHolder : isReceiver;
    }

    // Whether the receiver should already be walking to the parked source: Held with the due time known, within the
    // pre-walk's lead of the release.
    inline bool SeekParkPreWalk(SeekParkPhase phase, bool dueKnown, std::uint32_t dueInMs,
                                std::uint32_t releaseLeadMs)
    {
        return phase == SeekParkPhase::Held && dueKnown && dueInMs <= releaseLeadMs + kSeekParkPreWalkMs;
    }

    // Held at or under the lead: release now when the receiver is in reach or the wait is used up.
    inline bool SeekParkReleaseNow(bool receiverInReach, std::uint32_t dueInMs, std::uint32_t releaseLeadMs)
    {
        return receiverInReach || dueInMs + kSeekParkReceiverWaitMs <= releaseLeadMs;
    }

    // Whether this tank must stand down: Apply or Held, a tank, and the parked source is its victim or its target.
    inline bool SeekParkTankStandsDown(SeekParkPhase phase, bool isTank, bool hasSon)
    {
        return (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held) && isTank && hasSon;
    }

    // One receiver candidate: watched by the caller's own eligibility test, already in strike reach, its distance to
    // the source and its raw guid.
    struct SeekParkReceiverCand
    {
        bool eligible = false;
        bool inReach = false;
        float sonYd = 0.0f;
        std::uint64_t guid = 0;
    };

    // The release's receiver: the kept guid while it still qualifies, else one in reach before one that must walk,
    // then the nearer to the source, then the lower raw guid; -1 for none.
    inline int PickSeekParkReceiver(const std::vector<SeekParkReceiverCand>& cands, std::uint64_t kept)
    {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(cands.size()); ++i)
        {
            const SeekParkReceiverCand& cand = cands[static_cast<std::size_t>(i)];
            if (!cand.eligible)
                continue;
            if (kept != 0 && cand.guid == kept)
                return i;
            if (pick < 0)
            {
                pick = i;
                continue;
            }
            const SeekParkReceiverCand& best = cands[static_cast<std::size_t>(pick)];
            if (cand.inReach != best.inReach)
            {
                if (cand.inReach)
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

    // The phase machine (D5). True: the phase changed. A new cycle, a new fight or the key off starts the record over
    // at None; a live record is stepped only with its own source's facts, and its source dying spends the cycle.
    // Unsigned subtraction throughout; a nowMs of 0 is stored as 1.
    inline bool SeekParkStep(SeekParkState& state, const SeekParkRules& rules, const SeekParkInput& in,
                             std::uint32_t nowMs)
    {
        if (nowMs == 0)
            nowMs = 1;
        const SeekParkPhase before = state.phase;
        // A new cycle, a new fight or the key off starts the record over; nothing else does.
        if (state.cycleNo != in.cycleNo || state.fightStartMs != in.fightStartMs || !rules.on)
        {
            state = SeekParkState();
            state.cycleNo = in.cycleNo;
            state.fightStartMs = in.fightStartMs;
            if (!rules.on)
                return before != SeekParkPhase::None;
        }
        if (state.phase != SeekParkPhase::None)
        {
            // A live record is stepped only with its own source's facts: another bot's pick neither resets it nor
            // drives its phase.
            if (in.son != state.son)
                return false;
            if (!in.sonAlive)
            {
                // The source died or left: the park is over and the cycle is spent.
                const std::uint64_t son = state.son;
                const std::uint32_t cycleNo = state.cycleNo, fightStartMs = state.fightStartMs;
                state = SeekParkState();
                state.son = son;
                state.cycleNo = cycleNo;
                state.fightStartMs = fightStartMs;
                state.spent = true;
                return true;
            }
        }
        switch (state.phase)
        {
            case SeekParkPhase::None:
            {
                // A spent cycle, no source, or a dead one is no park.
                if (state.spent || in.son == 0 || !in.sonAlive)
                    return false;
                // A source that is not fighting, is not the seek's pick, or stands outside the raid circle is no park:
                // the take would refuse its chase and the burn, kill and gather key on another Son.
                if (!in.sonInCombat || !in.sonIsPick || !in.sonInCircle)
                    return false;
                // A source that joined too late for a clean release, or one nobody can control, follows today's take
                // and burn at once.
                if (in.dueKnown && in.dueInMs <= rules.releaseLeadMs + kSeekParkMinHoldMs)
                {
                    state.son = in.son;
                    state.spent = true;
                    state.parkBot = in.parkBot;
                    state.phase = SeekParkPhase::Failed;
                    state.why = "late";
                    break;
                }
                if (!in.parkBotOk)
                {
                    state.son = in.son;
                    state.spent = true;
                    state.parkBot = in.parkBot;
                    state.phase = SeekParkPhase::Failed;
                    state.why = "no_bot";
                    break;
                }
                if (!in.dueKnown)
                    return false;
                state.son = in.son;
                state.spent = true;
                state.parkBot = in.parkBot;
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
                    // No wait happened: the release row's waited_ms reads 0.
                    state.leadSinceMs = 0;
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
                // A park bot swap (the old one became the boss's victim, or died) restarts the budget clock once: the
                // new bot has to walk to the source before it can cast.
                if (in.parkBot != 0 && state.parkBot != 0 && in.parkBot != state.parkBot && state.botSwaps == 0)
                {
                    ++state.botSwaps;
                    state.budgetSinceMs = in.controllerInReach ? nowMs : 0;
                }
                // The budget counts from the first tick the controller stands in cast reach: the walk to the source
                // is not the control's time.
                if (in.controllerInReach && state.budgetSinceMs == 0)
                    state.budgetSinceMs = nowMs;
                if (state.budgetSinceMs != 0 && nowMs - state.budgetSinceMs > rules.applyBudgetMs)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "budget";
                    break;
                }
                // The walk is bounded whatever the park bot managed: a bot that never came into cast reach fails the
                // park instead of holding it for the whole cycle.
                if (state.phase == SeekParkPhase::Apply &&
                    nowMs - state.applySinceMs > rules.applyBudgetMs + kSeekParkWalkCapMs)
                {
                    state.phase = SeekParkPhase::Failed;
                    state.why = "reach";
                }
                break;
            }
            case SeekParkPhase::Held:
            {
                if (in.dueKnown && in.dueInMs <= rules.releaseLeadMs)
                {
                    if (state.leadSinceMs == 0)
                        state.leadSinceMs = nowMs;
                    if (SeekParkReleaseNow(in.receiverInReach, in.dueInMs, rules.releaseLeadMs))
                    {
                        SeekParkCloseHeld(state, nowMs);
                        state.phase = SeekParkPhase::Release;
                        state.releaseMs = nowMs;
                        state.why = "";
                        break;
                    }
                }
                else
                    state.leadSinceMs = 0;
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
                        state.botSwaps = 0;
                        state.leadSinceMs = 0;
                        state.why = "";
                    }
                }
                break;
            }
            case SeekParkPhase::Release:
            {
                // The release's own window: the source must be on a tank, with no breakable control, within the
                // receiver's window. The first tick a tank has it completes the release for good: a later threat
                // change, control effect, death or retarget cannot turn a release that happened into a failure.
                if (in.sonOnTank)
                {
                    state.phase = SeekParkPhase::Released;
                    state.why = "";
                    break;
                }
                // An eligible tank that cannot reach, taunt or attack is no release, and the park fails to today's
                // take.
                if (state.releaseMs != 0 && nowMs - state.releaseMs >= SeekParkReleaseWindowMs(in.receiverOk))
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
        if (state.phase != SeekParkPhase::None)
            state.parkBot = in.parkBot;
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
    // pull), and every task but the release's own in the release, whoever holds the source. Only the release's
    // receiver touches the source in that window; once it is latched on a tank, only that holder does, and a latched
    // release that broke onto a non-tank has its take veto lifted, so the ordinary nearest, healer-clock and relief
    // takes apply.
    inline bool SeekParkVetoesTankTask(SeekParkPhase phase, const char* takeSrc, bool sonOnTank)
    {
        const bool ownTake = takeSrc && std::strcmp(takeSrc, "park_release") == 0;
        if (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held)
            return true;
        if (phase == SeekParkPhase::Release)
            return !ownTake;
        if (phase == SeekParkPhase::Released)
            return sonOnTank && !ownTake;
        return false;
    }

    // Whether the wrapped tank body is told to skip this source outright: Apply and Held (every tank task is the
    // park's), the release for every tank but the release's own (its own-source drag is the release's second half),
    // and a latched release on a tank for every tank but the holder. The body then returns its next task (another Son
    // on a healer) instead of an empty one.
    inline bool SeekParkExcludesSource(SeekParkPhase phase, bool sonOnTank, bool isReleaseTank)
    {
        if (phase == SeekParkPhase::Apply || phase == SeekParkPhase::Held)
            return true;
        if (phase == SeekParkPhase::Release)
            return !isReleaseTank;
        if (phase == SeekParkPhase::Released)
            return sonOnTank && !isReleaseTank;
        return false;
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

    // The damage dealers stay off the released source until its victim is a tank, or the release's window after
    // the release, whichever is first.
    inline bool SeekParkDpsHeld(SeekParkPhase phase, bool sonOnTank, std::uint32_t releaseMs, std::uint32_t nowMs,
                                bool receiverOk)
    {
        if (!SeekParkReleaseLive(phase) || sonOnTank || releaseMs == 0)
            return false;
        return nowMs - releaseMs < SeekParkReleaseWindowMs(receiverOk);
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
