#pragma once
// tank_first (AiPlayerbot.DungeonRunAddTankFirst): an add a row names takes no damage from any bot but a tank until
// a tank has been its victim for the row's dwell, and again after each loss (the victim went back to a non-tank). One
// record per add and instance, shared by every bot of the raid, advanced at most every kTankFirstTickMs (D7):
//
//   wait  -- a tank is the victim --------------------------------> dwell
//   dwell -- a tank held it for the dwell ------------------------> burn
//   dwell -- a non-tank is the victim again ----------------------> wait (or lost)
//   burn  -- a non-tank is the victim, above 20 % and budget left -> lost (a new episode)
//   wait/dwell/lost -- the episode is window_s old ---------------> open (for good)
//
// The episode clock starts at the first sighting (wait) or at the loss (lost) and one episode never holds longer than
// the row's window_s. A dwell that falls back does not restart it. A victim that is no victim at all (a stun, a swap
// between two reads) neither completes nor breaks the dwell and opens no loss. Two releases override the phase each
// tick (D8): no usable tank on the map, and a seek source whose siphon is 15 s away or less.
//
// Pure logic: no game headers. The behaviour is tested in tools/test_tank_first_policy.cpp; the read and the hooks are
// pinned in tools/test_tank_first_wiring.py.

#include <cstdint>
#include <cstring>

namespace ai
{
    constexpr float kTankFirstLossMinHpPct = 20.0f;        // a loss at or under this health opens no hold
    constexpr std::uint32_t kTankFirstTauntHowMs = 2000;   // a taunt this recent makes the take `how=taunt`
    constexpr std::uint32_t kTankFirstTickMs = 250;        // the record advances at most this often
    constexpr std::uint32_t kTankFirstStaleMs = 10000;     // a record nobody read this long is dropped
    constexpr std::uint32_t kTankFirstPruneMs = 600000;    // an instance nobody read this long is gone (map unload)
    constexpr float kTankFirstTankReachYd = 100.0f;        // a tank farther than this is not usable
    constexpr int kTankFirstSiphonMs = 15000;              // a seek source this close to its cycle is free (= kSeekForceMs)

    enum class TankFirstPhase : std::uint8_t
    {
        Wait,  // held: no tank has had the add yet
        Dwell, // held: a tank has it, counting the dwell
        Burn,  // free: a tank held it for the dwell
        Lost,  // held: a non-tank took it back, counting the next dwell
        Open,  // free for the rest of its life
    };
    enum class TankFirstVictim : std::uint8_t
    {
        None,    // no victim this read: unknown, it neither completes nor breaks a dwell
        Tank,    // a living player tank, no creature charmer
        NonTank, // anything else: a pet, a totem, a healer, a damage dealer, a charmed unit
    };
    enum class TankFirstEvent : std::uint8_t
    {
        None,
        Arm,         // the record starts (the first sighting)
        OnTank,      // wait/lost -> dwell
        Settled,     // dwell -> burn
        Loss,        // burn -> lost
        LossIgnored, // a loss at or under 20 % health, or with the loss budget spent
        Release,     // the cap, or an override starting
    };

    struct TankFirstRules
    {
        std::uint32_t dwellMs = 0;
        std::uint32_t capMs = 0;
        std::uint8_t maxLosses = 0;
    };

    struct TankFirstRecord
    {
        TankFirstPhase phase = TankFirstPhase::Wait;
        TankFirstPhase dwellFrom = TankFirstPhase::Wait; // where a broken dwell falls back to
        std::uint32_t firstMs = 0;      // the first sighting
        std::uint32_t episodeMs = 0;    // the running hold's start
        std::uint32_t tankSinceMs = 0;  // the dwell's start (0: no tank)
        std::uint32_t settledMs = 0;    // the last burn's start
        std::uint32_t tickMs = 0;       // the last advance
        std::uint32_t seenMs = 0;       // the last read
        std::uint64_t tank = 0;         // the tank of the running dwell or burn
        std::uint64_t tauntBy = 0;      // the last taunt noted on this add
        std::uint32_t tauntMs = 0;
        std::uint32_t heldTotalMs = 0;  // every held episode, summed (the end row)
        std::uint8_t losses = 0;        // losses that opened a hold
        std::uint8_t settles = 0;
        bool armed = false;
        bool lossIgnoredLogged = false;
    };

    struct TankFirstIn
    {
        TankFirstVictim victim = TankFirstVictim::None;
        std::uint64_t victimGuid = 0;
        float hpPct = 100.0f;
        std::uint32_t nowMs = 0;
    };

    struct TankFirstOut
    {
        TankFirstEvent event = TankFirstEvent::None;
        const char* why = "";
        std::uint32_t ms = 0;
    };

    // An age that never wraps. Every stamp here is written under the record's lock from that writer's own clock and a
    // reader's sample can be older (it was taken before the lock), so an unsigned subtraction would read ~4.3e9 ms and
    // settle, release or erase on the spot. A stamp newer than the read counts as age 0 (SeekAgeMs's rule).
    inline std::uint32_t TankFirstAgeMs(std::uint32_t nowMs, std::uint32_t stampMs)
    {
        return nowMs < stampMs ? 0 : nowMs - stampMs;
    }

    // One advance of the record (D6, D7). The caller throttles with TankFirstTickDue.
    inline TankFirstOut TankFirstTick(TankFirstRecord& record, const TankFirstRules& rules, const TankFirstIn& in)
    {
        TankFirstOut out;
        const std::uint32_t now = in.nowMs;
        const bool first = !record.armed;
        if (first)
        {
            // The first sighting starts the episode; the victim read below still runs, so an add first seen on a tank
            // is in `dwell` the moment its record exists. The event stays Arm for that call.
            record.armed = true;
            record.firstMs = now;
            record.episodeMs = now;
            record.tickMs = now;
            record.phase = TankFirstPhase::Wait;
            out.event = TankFirstEvent::Arm;
        }
        record.tickMs = now;
        switch (record.phase)
        {
            case TankFirstPhase::Open:
                break;
            case TankFirstPhase::Burn:
                if (in.victim == TankFirstVictim::NonTank)
                {
                    if (in.hpPct <= kTankFirstLossMinHpPct)
                    {
                        // A loss near death opens no hold: the add dies before a second dwell could matter.
                        if (!record.lossIgnoredLogged)
                        {
                            record.lossIgnoredLogged = true;
                            out.event = TankFirstEvent::LossIgnored;
                            out.why = "low_hp";
                        }
                    }
                    else if (record.losses >= rules.maxLosses)
                    {
                        if (!record.lossIgnoredLogged)
                        {
                            record.lossIgnoredLogged = true;
                            out.event = TankFirstEvent::LossIgnored;
                            out.why = "loss_cap";
                        }
                    }
                    else
                    {
                        ++record.losses;
                        record.phase = TankFirstPhase::Lost;
                        record.dwellFrom = TankFirstPhase::Lost;
                        record.episodeMs = now;
                        record.tankSinceMs = 0;
                        record.lossIgnoredLogged = false;
                        out.event = TankFirstEvent::Loss;
                        out.ms = TankFirstAgeMs(now, record.settledMs);
                    }
                }
                else if (in.victim == TankFirstVictim::Tank)
                    record.tank = in.victimGuid;
                break;
            case TankFirstPhase::Wait:
            case TankFirstPhase::Lost:
                if (TankFirstAgeMs(now, record.episodeMs) >= rules.capMs)
                {
                    record.heldTotalMs += TankFirstAgeMs(now, record.episodeMs);
                    record.phase = TankFirstPhase::Open;
                    out.event = TankFirstEvent::Release;
                    out.why = "cap";
                    out.ms = TankFirstAgeMs(now, record.episodeMs);
                }
                else if (in.victim == TankFirstVictim::Tank)
                {
                    record.dwellFrom = record.phase;
                    record.phase = TankFirstPhase::Dwell;
                    record.tankSinceMs = now;
                    record.tank = in.victimGuid;
                    out.event = TankFirstEvent::OnTank;
                    out.ms = TankFirstAgeMs(now, record.episodeMs);
                }
                break;
            case TankFirstPhase::Dwell:
                if (in.victim == TankFirstVictim::Tank)
                {
                    record.tank = in.victimGuid;
                    if (TankFirstAgeMs(now, record.tankSinceMs) >= rules.dwellMs)
                    {
                        // The settle is checked before the cap, so a dwell that completes on the cap's tick burns.
                        record.heldTotalMs += TankFirstAgeMs(now, record.episodeMs);
                        record.phase = TankFirstPhase::Burn;
                        record.settledMs = now;
                        ++record.settles;
                        record.lossIgnoredLogged = false;
                        out.event = TankFirstEvent::Settled;
                        out.ms = TankFirstAgeMs(now, record.episodeMs);
                        break;
                    }
                }
                else if (in.victim == TankFirstVictim::NonTank)
                {
                    // A non-tank took the add back: the phase it came from holds again, and the episode clock is not
                    // touched.
                    record.phase = record.dwellFrom;
                    record.tankSinceMs = 0;
                }
                if (record.phase == TankFirstPhase::Dwell || record.phase == TankFirstPhase::Wait ||
                    record.phase == TankFirstPhase::Lost)
                {
                    if (TankFirstAgeMs(now, record.episodeMs) >= rules.capMs)
                    {
                        record.heldTotalMs += TankFirstAgeMs(now, record.episodeMs);
                        record.phase = TankFirstPhase::Open;
                        out.event = TankFirstEvent::Release;
                        out.why = "cap";
                        out.ms = TankFirstAgeMs(now, record.episodeMs);
                    }
                }
                break;
        }
        if (first)
            out.event = TankFirstEvent::Arm;
        return out;
    }

    // Whether the record's own clock allows another advance now.
    inline bool TankFirstTickDue(const TankFirstRecord& r, std::uint32_t nowMs)
    {
        return !r.armed || TankFirstAgeMs(nowMs, r.tickMs) >= kTankFirstTickMs;
    }

    // Whether the record is stale (nobody read it for kTankFirstStaleMs): the sweep drops it. A read that arrived
    // before the stamp another thread wrote is not stale.
    inline bool TankFirstStale(std::uint32_t nowMs, std::uint32_t seenMs)
    {
        return TankFirstAgeMs(nowMs, seenMs) > kTankFirstStaleMs;
    }

    // Whether the phase holds the add's damage (wait, dwell and a lost add; burn and open do not).
    inline bool TankFirstPhaseHolds(TankFirstPhase phase)
    {
        return phase == TankFirstPhase::Wait || phase == TankFirstPhase::Dwell || phase == TankFirstPhase::Lost;
    }

    // D8: "no_tank", "siphon" or null. dueKnown false: no siphon override (an unknown clock frees nobody).
    inline const char* TankFirstOverride(int usableTanks, bool seekSource, bool dueKnown, int dueInMs)
    {
        if (usableTanks <= 0)
            return "no_tank";
        if (seekSource && dueKnown && dueInMs <= kTankFirstSiphonMs)
            return "siphon";
        return nullptr;
    }

    // Whether the record holds the add now: its phase holds and no override is in force.
    inline bool TankFirstHolds(const TankFirstRecord& r, const char* overrideWhy)
    {
        return TankFirstPhaseHolds(r.phase) && overrideWhy == nullptr;
    }

    // The release row an override owes: one per add and reason, on the read that starts it. An override ahead of a
    // phase that already frees the add (burn, open) writes none: the add is free either way.
    inline const char* TankFirstOverrideStarted(const char* overrideWhy, const char* previousWhy, TankFirstPhase phase)
    {
        if (!overrideWhy || !TankFirstPhaseHolds(phase))
            return nullptr;
        if (previousWhy && std::strcmp(previousWhy, overrideWhy) == 0)
            return nullptr;
        return overrideWhy;
    }

    // How a take got the add: `taunt` for a taunt by that tank within kTankFirstTauntHowMs, else `threat`.
    inline const char* TankFirstHow(const TankFirstRecord& r, std::uint64_t tank, std::uint32_t nowMs)
    {
        if (tank != 0 && r.tauntBy == tank && TankFirstAgeMs(nowMs, r.tauntMs) <= kTankFirstTauntHowMs)
            return "taunt";
        return "threat";
    }

    inline void TankFirstNoteTaunt(TankFirstRecord& r, std::uint64_t tank, std::uint32_t nowMs)
    {
        if (tank == 0)
            return;
        r.tauntBy = tank;
        r.tauntMs = nowMs;
    }

    inline const char* TankFirstPhaseWord(TankFirstPhase phase)
    {
        switch (phase)
        {
            case TankFirstPhase::Wait:
                return "wait";
            case TankFirstPhase::Dwell:
                return "dwell";
            case TankFirstPhase::Burn:
                return "burn";
            case TankFirstPhase::Lost:
                return "lost";
            case TankFirstPhase::Open:
                return "open";
        }
        return "wait";
    }

    inline TankFirstRules TankFirstRulesOf(float paramSec, float windowSec, std::uint8_t priority)
    {
        TankFirstRules rules;
        rules.dwellMs = paramSec > 0.0f ? static_cast<std::uint32_t>(paramSec * 1000.0f + 0.5f) : 0;
        rules.capMs = windowSec > 0.0f ? static_cast<std::uint32_t>(windowSec * 1000.0f + 0.5f) : 0;
        rules.maxLosses = priority;
        return rules;
    }
}
