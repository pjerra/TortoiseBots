// Slice 8, package G8 (item 5): the run's gather that tolerates a revive (forever/docs/SLICE8-G-NOTES.md). Plain data in, plain data out: no core type, so
// forever/tools/test_run_gather.cpp runs it standalone.
//
// The problem (M8, two pauses of the same kind in 80 minutes): after a death the revive puts the dead at the entrance and the others walk back to the lead (a
// revived priest drinks first, a far member walks the cave's path); the run leader's regroup gives the gather `kRegroupGatherMs` (60 s, longer for a far bot) and
// then PAUSES: "regroup: the party did not gather within 60 s (a bot is 72.5 yd from the lead bot)". The policy's way out is a `summon` (a teleport of the
// straggler to the lead bot), which this host does not carry out (decision D3: no teleport of a bot to the lead bot, a GM power).
//
// The choice (the one that does LESS): no summon. The glue waits a BOUNDED time more, by the policy's own checked resume (`DungeonRun::Resume`: a fresh regroup,
// a fresh gather window sized for the farthest bot), instead of leaving the run paused. Nothing is teleported, no policy rule changes (the Turtle header is
// untouched), and a gather that does not progress still ends in the pause the policy gives it:
//   * only the regroup's gather pause is taken (`regroup: the party did not gather within`); every other pause is left alone;
//   * at most kMaxGatherExtensions per plan step (the counter starts over on a new step);
//   * the first extension is free (a bot that drinks or walks a long path has not moved much yet), the next ones need progress: the farthest member must be at least
//     kGatherClosingYd closer than at the previous pause of this step;
//   * never when a member is far on another map or not in the world (kGatherFarYd): that is no walk-back, that is a lost bot, and it pauses as before.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace TortoiseBots
{
namespace run
{
namespace gather
{
constexpr uint32_t kMaxGatherExtensions = 3;        // per plan step: at most this many extra gather windows (each one as long as the policy's regroup gather)
constexpr float kGatherClosingYd = 8.0f;            // the farthest member must be this much closer than at the previous pause for the second and later extension
constexpr float kGatherFarYd = 400.0f;              // a member this far (the facts say 1000 for another map or absent) is not a walker: no extension

// The pause note of the regroup's gather ceiling (DungeonRunPolicy.h: Pause(r, "regroup: the party did not gather within N s (a bot is X yd ...)")).
inline bool IsRegroupGatherPause(std::string const& note)
{
    static char const prefix[] = "regroup: the party did not gather within";
    return note.compare(0, sizeof prefix - 1, prefix) == 0;
}

struct Extension
{
    uint64_t step = ~uint64_t(0);   // the plan step the count is for
    uint32_t count = 0;             // extensions granted on that step
    float lastYd = -1.0f;           // the farthest member's distance at the previous pause of the step (-1: none yet)
};

struct Decision
{
    bool extend = false;
    std::string why;                // the event text: what was decided and from what (set for both answers when the pause is the gather's)
};

// Decides on one regroup-gather pause. `farthestYd` is the facts' maxMemberYd, `leadAlive` the lead bot's state. Updates `s`. A pause that is not the gather's
// answers extend=false with an empty `why` (the caller says nothing).
inline Decision Decide(Extension& s, uint64_t step, std::string const& note, float farthestYd, bool leadAlive)
{
    Decision d;
    if (!IsRegroupGatherPause(note))
        return d;
    if (s.step != step)
    {
        s.step = step;
        s.count = 0;
        s.lastYd = -1.0f;
    }
    auto const yd = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", double(v)); return std::string(b); };
    if (!leadAlive)
    {
        d.why = "the lead bot is dead: the pause stands";
        return d;
    }
    if (farthestYd >= kGatherFarYd)
    {
        d.why = "a member is " + yd(farthestYd) + " yd away (another map or not in the world): no walk-back, the pause stands";
        return d;
    }
    if (s.count >= kMaxGatherExtensions)
    {
        d.why = "the gather was extended " + std::to_string(s.count) + " times at this step already: the pause stands";
        return d;
    }
    if (s.count > 0 && !(s.lastYd - farthestYd >= kGatherClosingYd))
    {
        d.why = "the farthest member is " + yd(farthestYd) + " yd out (" + (s.lastYd < 0 ? std::string("not read") : yd(s.lastYd)) + " yd at the last pause), not closing in: the pause stands";
        return d;
    }
    ++s.count;
    d.extend = true;
    d.why = "the farthest member is " + yd(farthestYd) + " yd out" + (s.lastYd >= 0 ? " (" + yd(s.lastYd) + " yd at the last pause)" : std::string()) +
        ": waiting again (" + std::to_string(s.count) + " of " + std::to_string(kMaxGatherExtensions) + " at this step)";
    s.lastYd = farthestYd;
    return d;
}
}
}
}
