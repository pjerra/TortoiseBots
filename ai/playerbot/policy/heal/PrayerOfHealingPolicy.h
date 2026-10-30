#pragma once

// Pure gate for Prayer of Healing. The spell (10961/25316) targets 37 - the
// caster's party - so it heals the priest's own subgroup only, while the
// shared "medium aoe heal" value counts the whole raid. Queue it only when
// enough subgroup members are hurt at once.
//
// Callers count members of the bot's own subgroup (Group::SameSubGroup,
// the ImbueAction pattern), alive, in world, within 30 yd 2D, at or below
// medium health; the rule stays testable in
// tools/test_prayer_of_healing_policy.cpp.

namespace ai
{
    constexpr unsigned kPrayerOfHealingMinHurt = 3;

    inline bool PrayerOfHealingWorth(unsigned hurtInSubgroup) { return hurtInSubgroup >= kPrayerOfHealingMinHurt; }
}
