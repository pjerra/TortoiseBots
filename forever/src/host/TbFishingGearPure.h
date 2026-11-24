// Which carried weapon a bot picks up when a fishing pole has to leave its
// hand (a fishing errand ended, or the bot logged in still holding one).
//
// A pole is a tool, not a fighting weapon: it must never win the choice, even
// when its item level is the highest. Ties break on the lower item id so two
// bots with the same bags restore the same weapon.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace fishinggear
{

struct WeaponCandidate
{
    std::uint32_t itemId = 0;
    std::int32_t itemLevel = 0;
    bool fishingPole = false;
    bool equippable = false;   // the core accepts it in the main hand
};

// Index into `candidates`, -1 when nothing qualifies.
inline std::int32_t BestWeaponIndex(std::vector<WeaponCandidate> const& candidates)
{
    std::int32_t best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        WeaponCandidate const& candidate = candidates[i];
        if (!candidate.equippable || candidate.fishingPole)
            continue;
        if (best < 0)
        {
            best = std::int32_t(i);
            continue;
        }
        WeaponCandidate const& kept = candidates[std::size_t(best)];
        if (candidate.itemLevel > kept.itemLevel ||
            (candidate.itemLevel == kept.itemLevel && candidate.itemId < kept.itemId))
            best = std::int32_t(i);
    }
    return best;
}

} // namespace fishinggear
} // namespace TortoiseBots
