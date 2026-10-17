#pragma once

#include <string>

// Pull refusal codes (`.bot action pull|pullback`, `.bot cmd pull|pullback`).
//
// The pull needs a tank that is not fighting something (a tank mid-fight
// cannot walk up to the next mob). That refusal used to answer `no-tank`, the
// same code as "there is no tank at all", so a client could not tell "retry in
// a moment" from "give up". When no executor is free but a tank-capable bot is
// alive and really fighting, the answer is `tank-busy`. When the pull is
// refused is unchanged: only the code and text differ. The detail names the
// most common reason the scope's bots were excluded (issue #908), so a
// refusal says which gate applied.
//
// Pure, core-free, so tools/test_pull_refusal_policy.cpp includes it directly.

namespace ai
{
    inline char const* PullNoExecutorCode(bool tankCandidateInCombat)
    {
        return tankCandidateInCombat ? "tank-busy" : "no-tank";
    }

    inline std::string PullNoExecutorText(bool tankCandidateInCombat, std::string const& detail = {})
    {
        std::string text = tankCandidateInCombat
            ? "The tank is in combat. Wait for the fight to end (or send an attack order instead) and pull again."
            : "No live tank-capable owned party bot can pull this target.";
        if (!detail.empty())
        {
            text += " (";
            text += detail;
            text += ")";
        }
        return text;
    }
}
