// The Turtle side of the both-core helpers the ForeverCore port added in slice 10 (druid, paladin, shaman).
// Same names and signatures as forever/src/compat/TbCompat_S10.h; each is the expression the bot always made (the call is moved, not changed).
// Owned by the slice-10 K worker: no other package edits this file. Notes: forever/docs/SLICE10-K-NOTES.md.
#pragma once

// Stop a unit's melee swing. `targetSwitch` is the core's own flag (a target change, not a real combat stop).
// T: the call as the bot always made it (BalanceDruidStrategy.cpp OnStrategyRemoved, CombatActions.cpp).
inline bool AttackStopCompat(Unit* unit, bool targetSwitch) { return unit->AttackStop(targetSwitch); }
