// The Turtle side of the both-core helpers of the combat monitor (package P5 of the commander port). Same names and signatures as
// forever/src/compat/TbCompat_C5.h; each is the call the monitor always made.
#pragma once

#include "cmangos-compat-s5.h"   // WorldQueryCompat: the core's Query, held the way the loader holds it

#include <memory>
#include <string>

// The combat timeline's aura watch rows: this core logs a missing table itself and hands back null, which the caller reads as no rows.
inline std::unique_ptr<QueryResult> AuraWatchRowsCompat()
{
    return WorldQueryCompat("SELECT `spell_id` FROM `ai_combat_aura_watch`");
}

// One monitor line, already formatted (the manager's printf log).
inline void CombatMonitorLogCompat(bool error, std::string const& message)
{
    if (error)
        sLog.outError("%s", message.c_str());
    else
        sLog.outString("%s", message.c_str());
}
