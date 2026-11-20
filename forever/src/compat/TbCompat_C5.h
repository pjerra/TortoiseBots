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

// One monitor line, already formatted. The TU is compiled with the legacy preinclude (it reads the engine's config), so `sLog` is that core's
// printf proxy on both sides; nothing here may use a format string the caller did not build.
inline void CombatMonitorLogCompat(bool error, std::string const& message)
{
    if (error)
        sLog.outError("%s", message.c_str());
    else
        sLog.outString("%s", message.c_str());
}
