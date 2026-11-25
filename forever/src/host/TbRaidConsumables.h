// Raid consumables for bots (issue #22 round 1, SPEC-TortoiseBotsForever-22.md). Host side: it owns the table
// (forever/data/raid/consumables.csv through the generated TbConsumableData.gen.h), the checked index of the rows that
// resolve on this server, the mode of one bot and the two ways to pay for a buff:
//   mode 1 (items)  the item already in the bot's bags (BagItemFor hands its id to round 2's UseItemIdAction);
//   mode 2 (free)   a triggered cast of a checked index row's spell (ApplyFree, by category; a weapon row casts on the
//                   main-hand item, and only on the weapon family the table names).
// Nothing here buys, mails, crafts or creates an item, and forever/ code queries no database: the rows come from the
// compiled table, the facts from the core's stores and the bot manager.
//
// The rules are the pure ones in ai/playerbot/policy/raid/RaidConsumablePolicy.h (the mode, the map gate, the categories
// due, the check of the free cast's row, the not-covered declarations); this file only answers them with the core's facts.
// Off by default (TortoiseBots.RaidConsumables = 0); any other value than 0, 1 or 2 is a config error that stays off.
//
// Compiled with and without TORTOISEBOTS_SLICE1: the config, the index and the read-outs answer in both builds, while the
// bot's role needs the engine's class/spec tables, so without the engine no bot has a role and every answer is empty
// (a built-in feature test only runs with the engine).
//
// Threading: world thread only (the config load, the engine tick and the command handlers all run there).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ai/playerbot/policy/raid/RaidConsumablePolicy.h"
#include "host/data/TbConsumableData.gen.h"

class Player;

namespace TortoiseBots
{
namespace raidcons
{
// TortoiseBots.RaidConsumables (0 off, 1 items, 2 free), .Dungeons and .FreeForOwned: OnConfigLoad (TbScripts.cpp). A
// value outside 0, 1 and 2 is logged as a config error and stays off.
void LoadConfig();
bool Enabled();

// Builds the index of the rows that resolve on this server: the item exists in the item mirror (< 25000, a classic id, or
// one of the listed custom ids), the item lists that spell, and the spell leaves that aura (a weapon row instead carries
// the temporary-enchant effect, whose enchant id is kept: that id, and no other, is what "the stone is on the weapon"
// means). A row that fails, or a weapon row with an unknown or missing family, is logged and dropped, never guessed at. Safe
// to call again; answers nothing until the spell mirror is ready (a lazy build then). The table's not-covered lines are
// logged here too.
void BuildIndex();

// The mode this bot gets now: Off when the key is 0 (or the character is not a bot), else the pure policy's EffectiveMode
// with the bot's kind — a pool bot pays nothing, a player's bot (an owned alt, a hire, anything with a master) gets free
// mode only with FreeForOwned on.
ai::raidcons::Mode ModeFor(Player* bot);

// raid map, or a dungeon with the key, never a battleground; plus mode not Off.
bool MapAllows(Player* bot);

// The bot's role, mapped from its class and spec the way the brain's UseConsumableAction::GetBotRole does it. False
// without the engine (no role to answer).
bool RoleOf(Player* bot, ai::raidcons::Role& role);

// Mode 1: the item of that category already in the bot's bags, by the table's order (0 = none). Never creates an item.
std::uint32_t BagItemFor(Player* bot, ai::raidcons::Category category);

// Mode 2: a triggered cast of one checked index row's spell, chosen for the bot's own role and the category asked for (a
// weapon row casts on the main-hand item, and only when that weapon is of the family the row names). The generated table is
// never accepted at this boundary: a row the startup check dropped is not in the index and cannot be cast. False when the
// mode is not Free for this bot, the map or the bot's state refuses (dead, in combat), the category is not due (already
// held, or a flask or elixir covers it), the index has no row for the role and category at the bot's level on a weapon the
// row fits, or the core refused the cast.
bool ApplyFree(Player* bot, ai::raidcons::Category category);

// Whether the category is held now: one of its rows' auras is on the bot (`auraId` names it), or, for `weapon`, the main
// hand carries one of the table's own weapon enchantments (`auraId` 0; a rogue poison or a shaman imbue does not count).
bool Held(Player* bot, ai::raidcons::Category category, std::uint32_t& auraId);

// The categories due on this bot, as the table's words (flask, battle, guardian, food, weapon, scroll), in that order:
// what `.tb state` prints as `consmiss=`. Empty while the feature is off, the map refuses, the bot is dead or in combat,
// or it has no role.
std::vector<std::string> MissingCategories(Player* bot);

// The slots the table declares uncovered for this bot's role, as the table's words, in the table's order: what `.tb
// state` prints as `consnotcovered=`. Empty while the feature is off or the bot has no role.
std::vector<std::string> NotCoveredCategories(Player* bot);

// One line for the boot log and the docs: "raid consumables: 15 rows, 15 kept, 4 uncovered (mode 0, dungeons no, free for
// owned no)".
std::string BootLine();
}
}
