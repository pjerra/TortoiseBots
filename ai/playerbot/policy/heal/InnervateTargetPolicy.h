#pragma once

#include <cstdint>
#include <vector>

// Pure selection rules for Druid Innervate and group/raid mana balancing.
// No core includes: callers translate group/raid members into plain inputs.

namespace ai
{
    struct InnervateCandidate
    {
        std::uint64_t id = 0;  // opaque caller identifier (0 = none)
        bool isSelf = false;
        bool isHealer = false;
        bool hasMana = false;
        std::uint8_t manaPct = 100;
        bool inRange = false;
        bool hasInnervate = false;
        bool claimedByOther = false;
    };

    // Priority buckets (lower wins):
    //   0 = unclaimed healer below threshold
    //   1 = unclaimed non-healer mana user below threshold
    //   2 = claimed healer below threshold
    //   3 = claimed non-healer below threshold
    // Within a bucket, lowest manaPct wins (self breaks ties in favor of others
    // only if manaPct is equal, so a dying healer gets it first).
    inline std::uint64_t PickInnervateTarget(std::vector<InnervateCandidate> const& candidates,
                                             std::uint8_t manaThresholdPct)
    {
        std::uint64_t bestId = 0;
        int bestBucket = 99;
        std::uint8_t bestMana = 101;
        bool bestIsSelf = true;

        for (InnervateCandidate const& c : candidates)
        {
            if (c.id == 0 || !c.hasMana || !c.inRange || c.hasInnervate)
                continue;
            if (c.manaPct >= manaThresholdPct)
                continue;

            int const bucket = (c.claimedByOther ? 2 : 0) + (c.isHealer ? 0 : 1);
            bool better = false;
            if (bucket < bestBucket)
                better = true;
            else if (bucket == bestBucket)
            {
                if (c.manaPct < bestMana)
                    better = true;
                else if (c.manaPct == bestMana && bestIsSelf && !c.isSelf)
                    better = true;
            }

            if (better)
            {
                bestId = c.id;
                bestBucket = bucket;
                bestMana = c.manaPct;
                bestIsSelf = c.isSelf;
            }
        }

        return bestId;
    }
} // namespace ai
