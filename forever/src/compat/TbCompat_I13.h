// ForeverCore side of the both-core helper the `.tb ignore` port added (the
// leave-it-alone list): whether the bot's commander marked the unit. Owned by
// the ignore-mob worker: no other package edits this file.
// Turtle side: ai/cmangos-compat-i13.h (same name and signature; always false:
// nothing marks a mob there).
#pragma once

#include "host/TbIgnore.h"

// A unit the bot's commander ignore-listed is no target, no splash and no
// area spell for this bot. Expiry and dead units the bot can see are pruned
// on the way (world thread, where the engine and the commands run).
inline bool IsIgnoredMobCompat(Player* bot, ObjectGuid const& guid)
{
    return TortoiseBots::i13::IsIgnored(bot, guid);
}
