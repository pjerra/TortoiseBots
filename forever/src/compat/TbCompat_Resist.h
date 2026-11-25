// ForeverCore side of the both-core helpers for the resistance-gear swap (issue #20): the hold, the swap and the boss
// entry's school, as the engine-side files ask them. Turtle side: ai/cmangos-compat-resist.h (every answer inert).
//
// The host module is the one owner of the state (host/TbResistGear.h); these inline functions are only the names the
// engine's side uses, so a Turtle build reads the same call sites as no-ops instead of needing a second implementation.
#pragma once

#include "host/TbResistGear.h"

// The bot is wearing a resist set this module put on it (the auto-equip gate in TbCompat_W6.h asks this).
inline bool ResistGearHoldingCompat(Player* bot)
{
    return TortoiseBots::resistgear::Holding(bot);
}

// Puts the school's set on (the action "resist gear on").
inline bool ResistGearApplyCompat(Player* bot, int school)
{
    return TortoiseBots::resistgear::Apply(bot, school);
}

// Puts the bot's own pieces back (the action "resist gear off", and a logout).
inline bool ResistGearRestoreCompat(Player* bot, char const* why)
{
    return TortoiseBots::resistgear::Restore(bot, why);
}

// The school a boss entry asks for (2 fire, 3 nature, 4 frost, 5 shadow, ...); 0 when the boss is not on the list.
inline int ResistGearSchoolForCompat(uint32 entry)
{
    return TortoiseBots::resistgear::SchoolFor(entry);
}
