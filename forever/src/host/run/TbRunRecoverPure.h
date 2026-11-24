// proof-1 B (forever/docs/PROOF-1-NOTES.md): the run's self-recover after the policy stopped regrouping by itself at a step. Plain data in, plain data
// out: no core type, so forever/tools/test_run_recover.cpp runs it standalone.
//
// The problem (RFC problem 1, second half): after kAutoRegroupsPerStep (2) of its own regroups at a step, the policy PAUSES with a note that asks a line
// from outside the run ("... N regroups by itself at this step did not hold): .bot run resume" or "N dead: regroup (cmd revive party), then .bot run
// resume (... did not hold)"). The note is on the run in DungeonRunPolicy.h, LeadRevivedAway and the wipe.
//
// The choice (what a player party does, nothing the host does not have): the dead bots release their spirits (the core revives them at the instance
// entrance, decision D2), and the policy's own Resume runs BeginRegroup: the revive wait, the gather, the rest (the bots' out-of-combat buff strategies
// run there), and the wc-r9 walk back to the last cleared step that pulls the respawned packs one at a time. No teleport, no GM revive, no summon.
//   * once per plan step (kMaxRecoversPerStep) and at most kMaxRecoversPerRun times per run;
//   * never when a member is 400+ yd away or on another map (kRecoverFarYd): that is a lost bot, not a regroup that failed to hold;
//   * the option TortoiseBots.Run.SelfRecover (default on); off refuses with its reason and the pause stands;
//   * every other pause is left alone (an empty `why`: the caller says nothing).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace TortoiseBots
{
namespace run
{
namespace recover
{
constexpr uint32_t kMaxRecoversPerStep = 1;          // a step whose own regroups did not hold is recovered once
constexpr uint32_t kMaxRecoversPerRun = 3;           // a run recovers at most this many pauses by itself
constexpr float kRecoverFarYd = 400.0f;              // a member this far (the facts say 1000 for another map or absent) is not a failed regroup: no recover

// The pause note the policy writes when its own regroups at a step did not hold (both sites: the lead revived away and the wipe). The phrase is in
// the middle of different prefixes, so it is matched anywhere in the note.
inline bool IsRegroupsDidNotHoldPause(std::string const& note)
{
    static char const phrase[] = " regroups by itself at this step did not hold";
    return note.find(phrase) != std::string::npos;
}

struct State
{
    uint64_t step = ~uint64_t(0);   // the plan step the per-step count is for
    uint32_t stepCount = 0;         // recoveries granted on that step
    uint32_t runCount = 0;          // recoveries granted in the whole run (a step starts the other count over, never this one)
};

struct Decision
{
    bool recover = false;
    std::string why;                // the event text: what was decided and from what (set for both answers when the pause is this one)
};

// Decides on one "regroups did not hold" pause. `farthestYd` is the facts' maxMemberYd, `optionOn` is TortoiseBots.Run.SelfRecover. Updates `s`.
// A pause that is not this one answers recover=false with an empty `why` (the caller says nothing).
inline Decision Decide(State& s, uint64_t step, std::string const& note, float farthestYd, bool optionOn)
{
    Decision d;
    if (!IsRegroupsDidNotHoldPause(note))
        return d;
    if (s.step != step)
    {
        s.step = step;
        s.stepCount = 0;
    }
    auto const yd = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", double(v)); return std::string(b); };
    if (!optionOn)
    {
        d.why = "the self-recover is off (TortoiseBots.Run.SelfRecover): .bot run resume by hand, or turn it on";
        return d;
    }
    if (farthestYd >= kRecoverFarYd)
    {
        d.why = "a member is " + yd(farthestYd) + " yd away (another map or not in the world): no walk back holds the regroup, the pause stands";
        return d;
    }
    if (s.stepCount >= kMaxRecoversPerStep)
    {
        d.why = "the run recovered this step already (" + std::to_string(s.stepCount) + " of " + std::to_string(kMaxRecoversPerStep) + "): the pause stands";
        return d;
    }
    if (s.runCount >= kMaxRecoversPerRun)
    {
        d.why = "the run recovered by itself " + std::to_string(s.runCount) + " times (of " + std::to_string(kMaxRecoversPerRun) + "): the pause stands";
        return d;
    }
    ++s.stepCount;
    ++s.runCount;
    d.recover = true;
    d.why = "the regroups at this step did not hold: releasing the dead and regrouping again by itself (" + std::to_string(s.runCount) + " of " +
        std::to_string(kMaxRecoversPerRun) + " for the run, farthest member " + yd(farthestYd) + " yd out)";
    return d;
}
}
}
}
