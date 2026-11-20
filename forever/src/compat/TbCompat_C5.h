// ForeverCore side of the both-core helpers the combat monitor needs (package P5 of the commander port): its one world-database read and its
// two log lines. Turtle side: ai/cmangos-compat-c5.h (same names, the calls the monitor always made); both are reached through
// ai/playerbot/TbCompatC5.h, so runtime/CombatMonitorService.cpp stays one file for both cores.
#pragma once

#include "DatabaseEnv.h"   // QueryResult, WorldDatabase (TbCompat_S5.h needs it first)
#include "Log.h"           // the core's log macros

#include "TbCompat_S5.h"   // WorldQueryCompat: every world read on this core first asks information_schema (a missing table aborts here)

#include <cstddef>
#include <string>

// The combat timeline's aura watch rows (world table ai_combat_aura_watch, read once at startup). A database without the table hands back an
// empty result after one log line: the timeline then records no aura rows (less, never an abort). The module ships no such table on this core.
inline QueryResult AuraWatchRowsCompat()
{
    return WorldQueryCompat("SELECT `spell_id` FROM `ai_combat_aura_watch`");
}

// One monitor line, already formatted: the manager's log has no printf path on this core, and the world-database loop of it is a shared file.
inline void CombatMonitorLogCompat(bool error, std::string const& message)
{
    if (error)
        TC_LOG_ERROR("tortoisebots", "{}", message);
    else
        TC_LOG_INFO("tortoisebots", "{}", message);
}
