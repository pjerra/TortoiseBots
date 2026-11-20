// Commander port, package P2: the opt-in persistence of commander bindings (decision D3). FOREVER-COMMANDER-PORT-SPEC.md sections 4, 5 and 10.
//
// The module's first characters-database table: one row per commander (`tb_commander_binding`), five fields and no other (TbCmdBindingLifePure.h;
// character guids only, never a group id or a group counter, decision D2). The DDL lives in forever/sql/characters/2026_11_20_00_tb_commander_binding.sql
// and, so an enabled server without that migration still works, the module runs the same CREATE TABLE IF NOT EXISTS once at enable.
//
// ForeverCore aborts the world on a query that names a missing table or column (MySQLConnection.cpp: ER_NO_SUCH_TABLE), so every read and write here
// goes through `CharacterQueryCompat`: it asks information_schema first and answers "no rows" (persistence off for this session, one log line) when the
// table is missing - the same "do less" rule as WorldQueryCompat on the world side. Nothing is ever queried on a server that has the key off.
//
// World thread only (plain data, no lock).
#pragma once

#include "host/cmd/TbCmdBindingLifePure.h"

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace store
{
// Reads TortoiseBots.Commander.KeepBindings and the two graces; called at world start and on a config reload.
void LoadConfig();
// The stored rows (empty when the key is off or the table is not there). Reads the table once, after LoadConfig.
std::vector<life::Stored> Rows();
bool Enabled();
// Writes one row (delete + insert: the module prepares no statements of its own on this core) and drops one. False when persistence is off.
bool Save(life::Stored const& stored);
bool Erase(uint64_t commander);
// The grace a commander's absence gets before the binding ends, and the grace a dead lead gets. Milliseconds; 0 = immediate.
int64_t CommanderGraceMs();
int64_t LeadDeadGraceMs();
}
}
}
