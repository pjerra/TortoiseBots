// Turtle side of the both-core helpers for the resistance-gear swap (issue #20). ForeverCore side:
// forever/src/compat/TbCompat_Resist.h.
//
// The swap needs the ForeverCore host module (host/TbResistGear.cpp), which the Turtle core does not build, so every
// helper is the inert answer here: no hold, no swap, no boss school. The Turtle core's own auto-equip path is untouched.
#pragma once

#include "Define.h"

class Player;

inline bool ResistGearHoldingCompat(Player*)
{
    return false;
}

inline bool ResistGearApplyCompat(Player*, int)
{
    return false;
}

inline bool ResistGearRestoreCompat(Player*, char const*)
{
    return false;
}

inline int ResistGearSchoolForCompat(uint32)
{
    return 0;
}
