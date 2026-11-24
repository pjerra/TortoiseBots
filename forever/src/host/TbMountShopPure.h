// Core-free decisions for the mount purchase errand (forever/src/host/TbMountShop.cpp).
// Tested standalone by forever/tools/test_mount_shop_policy.cpp.
//
// Forever's riding ladder is Apprentice Riding (75) at level 40 and Journeyman
// Riding (150) at level 60 (core Trainer::TeachAvailableSpellsFree comment,
// world DB trainer_spell 33388/33391). A mount item needs riding 75/150, so the
// same levels gate the normal and the epic racial mount.
#pragma once

#include <cstdint>

namespace tbhost
{
namespace mountshop
{

inline constexpr uint8_t kMountLevel = 40;
inline constexpr uint8_t kEpicMountLevel = 60;
inline constexpr uint16_t kApprenticeRiding = 75;
inline constexpr uint16_t kJourneymanRiding = 150;

// 0 below level 40, 75 from 40, 150 from 60.
inline uint16_t RidingTargetForLevel(uint8_t level)
{
    if (level >= kEpicMountLevel)
        return kJourneymanRiding;
    if (level >= kMountLevel)
        return kApprenticeRiding;
    return 0;
}

inline bool IsEpicTier(uint8_t level)
{
    return level >= kEpicMountLevel;
}

enum class Need : uint8_t
{
    None = 0,
    TrainRiding = 1,
    BuyMount = 2
};

// A bot below 40 needs nothing. From 40 it needs riding 75 and a mount it can
// use; from 60, riding 150 and the epic mount. A normal mount is not replaced
// at 60: only the epic tier is still wanted.
inline Need PickNeed(uint8_t level, uint16_t ridingSkill, bool hasNormalMount, bool hasEpicMount)
{
    uint16_t const target = RidingTargetForLevel(level);
    if (!target)
        return Need::None;
    if (ridingSkill < target)
        return Need::TrainRiding;
    bool const hasTierMount = IsEpicTier(level) ? hasEpicMount : hasNormalMount;
    return hasTierMount ? Need::None : Need::BuyMount;
}

} // namespace mountshop
} // namespace tbhost
