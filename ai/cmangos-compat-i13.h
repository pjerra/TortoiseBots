// The Turtle side of the both-core helper the ForeverCore port added for
// `.tb ignore` (the leave-it-alone list): whether the bot's commander marked
// the unit. T: nothing marks a mob, so never (the behaviour is today's).
// Forever side: forever/src/compat/TbCompat_I13.h, which asks the host store.
// Owned by the ignore-mob worker: no other package edits this file.
#pragma once

// A unit the commander's ignore list names is no target, no splash and no
// area spell for this bot. T: the list does not exist, so no unit is ignored.
inline bool IsIgnoredMobCompat(Player* /*bot*/, ObjectGuid const& /*guid*/) { return false; }
