#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Zone level of an area a travel destination sits in (W4-A2, FOREVER-W0-W4-SPEC section 2.3).
//
// The legacy chain (TravelMgr::GetAreaLevel) kept an `ai_playerbot_zone_level` cache, then walked every sub-area of the
// DBC area table and averaged their levels, then took the parent zone, then the DBC AreaLevel. The sub-area average is
// the expensive and wrong half: it reads area levels that are themselves unknown, and the spawn-average fallback it
// once had called WorldPosition::GetArea() per creature spawn, which loaded the terrain under every creature in the
// world on the world thread (issue #684, 8-24 s freezes).
//
// The rule this header carries, in order:
//   1. the shipped `ai_playerbot_zone_level` row (read before this policy is asked, so it is not an input here);
//   2. the median of the area's own spawns: hostile, non-critter, non-elite creatures, each carrying its
//      `creature_classic_level` level - at least five samples, or there is no median;
//   3. the parent zone's level, when it has one;
//   4. the DBC AreaTableEntry::ExplorationLevel;
//   5. -1: unknown, which is never a travel target.
//
// Everything is data the caller already holds: no terrain lookup, no area-table walk, no world scan. The generation
// boot (W4-B) is the one caller that feeds step 2, one area at a time, from spawns whose area it resolved once.
namespace ai
{
    namespace zone_level
    {
        // Minimum number of qualifying spawns for the median to be trusted. Four spawns of one entry say nothing
        // about the area (a single wolf pack); five is the smallest count that survives one outlier on each side.
        constexpr std::uint32_t kMinSamples = 5;

        struct Sample
        {
            std::int32_t level = 0;   // the spawn's creature_classic_level (0 = unknown, ignored)
            bool hostile = false;     // the spawn's template is hostile to players
            bool critter = false;     // CREATURE_TYPE_CRITTER: zero XP, never a travel target
            bool elite = false;       // rank above normal: a different fight, not the area's baseline
        };

        struct Inputs
        {
            std::vector<Sample> samples;      // every spawn of the area; the policy filters
            std::int32_t parentLevel = 0;     // the parent zone's resolved level, 0 = unknown
            std::int32_t explorationLevel = 0;// DBC AreaTableEntry::ExplorationLevel, 0 = none
        };

        // Levels of the samples that speak for the area: hostile, not a critter, not an elite, with a known level.
        inline std::vector<std::int32_t> QualifyingLevels(std::vector<Sample> const& samples)
        {
            std::vector<std::int32_t> levels;
            levels.reserve(samples.size());
            for (Sample const& sample : samples)
                if (sample.hostile && !sample.critter && !sample.elite && sample.level > 0)
                    levels.push_back(sample.level);
            return levels;
        }

        // Lower median of the sorted levels (even counts take the lower of the two middle values). A median that
        // rounds up would rate an area by its hardest half; the floor keeps the pick on the side a bot can fight.
        inline std::int32_t Median(std::vector<std::int32_t> levels)
        {
            if (levels.empty())
                return 0;
            std::sort(levels.begin(), levels.end());
            return levels[(levels.size() - 1) / 2];
        }

        // -1 when nothing is known: such an area is never a travel target (IsLocationLevelValid refuses it).
        inline std::int32_t Resolve(Inputs const& inputs)
        {
            std::vector<std::int32_t> const levels = QualifyingLevels(inputs.samples);
            if (levels.size() >= kMinSamples)
            {
                std::int32_t const median = Median(levels);
                if (median > 0)
                    return median;
            }

            if (inputs.parentLevel > 0)
                return inputs.parentLevel;

            if (inputs.explorationLevel > 0)
                return inputs.explorationLevel;

            return -1;
        }
    }
}
