// The resistance-gear swap around the raid pull (issue #20, SPEC-TortoiseBotsForever-20.md). Host side: it owns the
// boss entry -> school index (from forever/data/raid/boss-resist.csv through the generated TbResistData.gen.h), the
// swap itself (core Player::SwapItem, in place: nothing is bought, created, sold or destroyed), the map of what each
// bot is holding (guid -> the pieces still to put back) and that map's characters-database copy
// (tortoise_bots_resist_hold), so a crash, a restart or a logout that could not finish does not leave a resist set on
// with nothing recording it.
//
// Compiled with and without TORTOISEBOTS_SLICE1: the hold is plain host state, and `Holding` is what the auto-equip gate
// (compat/TbCompat_W6.h through compat/TbCompat_Resist.h) asks, so a slice-OFF build must answer it too (there it is
// always no: nothing arms a swap without the engine). The tank question needs the engine's role rules, so it is asked
// only in the engine build; format is "the head, shoulders, chest, legs, feet and hands are the non-tank's only".
//
// Threading: world thread only (commands, the engine tick and the PlayerScript hooks all run there).
#pragma once

#include "Define.h"

class Player;

namespace TortoiseBots
{
namespace resistgear
{
// TortoiseBots.RaidResistGear.Enable (default 0), .Range (60), .ReleaseSeconds (15): OnConfigLoad (TbScripts.cpp).
// Turning Enable off while a set is held puts the set back for every bot that can take it (alive, out of combat) and
// keeps a hold it could not resolve: only the pass that brings the last piece back clears a hold.
void LoadConfig();
bool Enabled();
uint32 Range();            // yd: how near a listed boss the swap is armed (SPEC-20 round 2 uses it)
uint32 ReleaseSeconds();   // s: how long after the last listed boss left the range the set comes off (round 2)

// The characters store (tortoise_bots_resist_hold, read and written through the schema guard): what a bot is holding
// survives a crash or a restart. Each row carries the hold's state, so a restart knows whether the set is still meant to
// be on (HELD) or a restore was in flight (RELEASING). LoadAtStartup asks once whether the table exists; with the
// feature off that is all it does (no CREATE, no read, no warning) unless the table is there, in which case its rows are
// read once into a snapshot. A login takes its hold from that snapshot (never a query) and restores it. Both are
// harmless when the store is absent: the hold is then memory-only and one warning says so.
void LoadAtStartup();
void RecoverLogin(Player* bot);

// The world tick (OnBeforeMapsUpdate): recovers a stored hold once its bot is online, and retries a RELEASING hold
// (a restore that was asked for and did not finish) while the bot lives out of combat: ai::kRetrySeconds between the
// first passes, then ai::kBackoffRetrySeconds apart, for as long as the hold has pieces (a full bag delays the set,
// never abandons it). A hold that is merely HELD (the set is on for the pull) is never touched here.
void Update(uint32 diff);

// Builds entry -> school from the generated table and checks every row: the evidence spell's school (the spell mirror
// TbSpellEntry) must be the row's school, else the row is logged and dropped (a data typo must not swap the wrong set).
// Called at OnWorldInitialized, after tbcompat::InitSpellTable (the store is ready by then). Safe to call again.
void BuildIndex();

// The school a boss entry asks for as the client's SpellSchools id (2 fire, 3 nature, 4 frost, 5 shadow, 1 holy,
// 6 arcane); 0 when the boss is not on the list. Builds the index on first use.
int SchoolFor(uint32 entry);

// The school's word as the data file writes it ("fire", "shadow", ...); "" for 0.
char const* SchoolWord(int school);

// Puts the school's resistance pieces on (one per allowed slot, see ai::PlanSwaps) and records the hold as HELD: the
// world tick has nothing to retry until a release asks. Refuses (false) when the feature is off, the bot is not a
// grouped host bot on a raid map, is dead or in combat, is already holding, or the school is unknown, and also when the
// recovery record cannot be written first (the store is missing or the write did not land): nothing is swapped then, so
// no piece is ever on without a row that puts it back. Returns true when at least one piece went on.
bool Apply(Player* bot, int school);

// Puts back every piece that can move now and keeps the rest in the hold (a dead bot keeps all of it; a piece whose
// own counterpart is gone, is not in the bags (the bank is not a swap source), whose slot no longer holds the resist
// piece, or that the core refuses is skipped or dropped from the hold and logged). Asking for a restore makes the hold
// RELEASING, so the world tick retries the pieces left behind. `why` says who asked ("logout", the release action,
// "login", "retry", "disable") and goes in the log line. A logout ends the in-memory hold after the try: the remaining
// pieces stay in the characters store (and in the startup snapshot), so the next login restores them.
bool Restore(Player* bot, char const* why);

// Whether the bot is wearing a resist set this module put on it. The auto-equip gate reads this: an upgrade pass must
// not "improve" the resist piece back into the bags mid-hold.
bool Holding(Player* bot);

// The read-out halves: the school of the held set (0 when not holding) and how many pieces it put on.
int HeldSchool(Player* bot);
uint32 HeldCount(Player* bot);

#if defined(TB_RESIST_GEAR_TEST)
// The host check's between-scenario reset (forever/tools/resist_gear_host_check.cpp): forget every hold, the startup
// snapshot and what was learned about the store. Never called by the module.
void ResetForTest();
#endif
}
}
