#pragma once

// Per-guild bot rank for guild bank access (TBF #155): the rank every module-owned
// bot in a guild with a real player member is moved to, so the bots share one rank
// the master can open bank tabs to. Backed by `tortoise_bots_guild_bot_rank` (one
// row per guild, written when the player picks a rank with `.tb guildorder rank`);
// guilds without a row use the lowest rank. The on/off switch is
// TortoiseBots.World.GuildOrderBotRank.
//
// Also the order-permission gate (a real player of the bot's guild whose rank may
// edit officer notes) and the bank-access report `.tb guildorder access` prints.
//
// World thread only: every entry point is a `.tb guildorder` command or a guild
// join, never the bot tick.
#include <cstdint>
#include <optional>
#include <string>

class Player;

namespace tbhost
{
namespace guild_order_access
{

// True when TortoiseBots.World.GuildOrderBotRank is on. Read on demand, so
// `.reload config` is enough to turn the rank rule off.
bool BotRankEnabled();

// Characters the module owns: a pool character, online or offline. Never a
// player's own character running as a bot.
bool IsOrderBot(uint32_t guidLow);

// Stored choice for the guild, or nullopt when nobody picked one (or the store
// is unavailable).
std::optional<uint32_t> StoredBotRank(uint32_t guildId);

// Rank every bot in the guild should hold: the stored choice, or the guild's
// lowest rank when nothing was stored. 0 when the switch is off.
uint32_t WantedBotRank(uint32_t guildId, uint32_t rankCount);

// What `.tb guildorder access` reports for one guild.
struct AccessReport
{
    bool enabled = false;       // the switch is on and the guild has a rank
    uint32_t guildId = 0;
    uint32_t botRank = 0;       // rank order (0 = master, which is never the bot rank)
    std::string rankName;
    std::string tabsOk = "-";   // 1-based bank tabs the bot rank may deposit into
    std::string tabsDenied = "-";
    bool canSetRank = false;    // the asker may run `.tb guildorder rank`
    bool canGiveOrders = false; // the asker may place, cancel or move orders
};

// Fills `out` for `asker` (null: the console). Always succeeds for an existing
// guild; `enabled` says whether the rank rule is active.
bool BuildAccessReport(uint32_t guildId, Player* asker, AccessReport& out);

// Store `wanted` (a rank order 1..lowest) and move every bot of the guild to it.
// Empty `outCode` = stored and applied (moved may be 0 when the guild has no real
// player member); otherwise `outCode`/`outDetail` are the refusal.
bool SetBotRank(uint32_t guildId, uint32_t wanted,
    std::string* outCode = nullptr, std::string* outDetail = nullptr, uint32_t* moved = nullptr);

// A bot joined the guild: give it the bot rank when the rule applies there. Called
// from the join path after the core accepted the member.
void ApplyBotRankOnJoin(Player* joiner, uint32_t guildId);

// The order-permission gate: a real player in the order's guild whose rank may edit
// officer notes. Console sends have no Player and skip it upstream.
bool MayGiveOrders(uint32_t guildId, Player* asker, std::string& outCode, std::string& outDetail);

// The rank-change gate: a real player whose rank may promote and demote.
bool MayChangeBotRank(uint32_t guildId, Player* asker, std::string& outCode, std::string& outDetail);

// Loads the stored ranks once at startup and runs the module's own CREATE when the
// table is missing. Called from TbScripts.cpp with the order store's load.
void LoadBotRankAtStartup();

} // namespace guild_order_access
} // namespace tbhost
