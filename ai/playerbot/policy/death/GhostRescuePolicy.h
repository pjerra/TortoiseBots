#pragma once

// When a free bot's ghost may be rescued by the core (spec D2): the ONLY teleport besides the core's own release. The rule is
// time without headway toward the corpse AND toward the spirit healer, measured on the ground the bot covers, not on its motion
// state: a spline that is dispatched and never ridden moves the bot nowhere and must still trip the rescue.
//
// Pure and core-free, so forever/tools/test_ghost_rescue_policy.cpp includes it directly. The host owns the two distances (corpse,
// spirit healer) and the tick length; this struct owns the decision.

#include <cstdint>

namespace TortoiseBots
{

inline constexpr std::uint32_t GHOST_RESCUE_STALL_MS = 5 * 60 * 1000;
inline constexpr float GHOST_RESCUE_PROGRESS_YD = 5.0f;

class GhostRescueProgress
{
public:
    // One look at the ghost. corpseYd: distance to the corpse (same map only; a ghost without corpse passes its distance to the
    // spirit healer here); healerYd: distance to the graveyard the spirit healer stands at. Returns true when this look made
    // headway toward either target (the stall clock restarts).
    bool Observe(float corpseYd, float healerYd, std::uint32_t diffMs)
    {
        if (!_armed)
        {
            _armed = true;
            _bestCorpse = corpseYd;
            _bestHealer = healerYd;
            _stalledMs = 0;
            return false;
        }

        bool const progress = corpseYd < _bestCorpse - GHOST_RESCUE_PROGRESS_YD ||
                              healerYd < _bestHealer - GHOST_RESCUE_PROGRESS_YD;
        if (progress)
        {
            _bestCorpse = corpseYd < _bestCorpse ? corpseYd : _bestCorpse;
            _bestHealer = healerYd < _bestHealer ? healerYd : _bestHealer;
            _stalledMs = 0;
            return true;
        }

        // A ghost that wanders away is not making progress: the best marks never move back, so the stall keeps counting.
        _stalledMs += diffMs;
        return false;
    }

    bool Due() const { return _armed && _stalledMs >= GHOST_RESCUE_STALL_MS; }

    void Reset()
    {
        _armed = false;
        _bestCorpse = 0.0f;
        _bestHealer = 0.0f;
        _stalledMs = 0;
    }

private:
    bool _armed = false;
    float _bestCorpse = 0.0f;
    float _bestHealer = 0.0f;
    std::uint32_t _stalledMs = 0;
};

} // namespace TortoiseBots
