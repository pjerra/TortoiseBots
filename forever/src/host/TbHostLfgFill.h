// the dungeon-finder fill, its `.tb lfg` command and, for the player surface, `.bot lfg`.
//
// The queue is the module's own: ForeverCore's dungeon finder cannot be reached from the 1.60 client (LFGHandler.cpp
// answers the dungeon list empty) and its role data admits no damage spec, so a port on sLFGMgr is a core project. The
// fill observes nothing and owns the party instead: it takes free host bots one at a time and groups them with the
// player who queued (BotMgr::GroupWith), sets the group's LFG role bits and forces the bot's kit role, then lets them
// go when the player leaves. AiPlayerbot.RandomBotLft* are TB's keys and defaults.
//
// The decisions are pure (host/TbLfgFillPure.h); this header is the live side. Everything runs on the world thread.
#pragma once

#include "ChatCommand.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace lfg
{
// `.tb lfg join <dungeon id> <tank|healer|dps> [#token]`, `status [<player>]`, `leave`. GM or console; join needs the
// sender's own character as the leader. Without the engine build the same command answers NotBuilt (TbH3Off.cpp).
bool HandleLfg(ChatHandler* handler, Trinity::ChatCommands::Tail args);

// `.bot lfg join|status|leave`: the same service for the caller's own character (host/player/TbPlayerCmd.cpp). Replies are
// chat lines; false only when there is no player.
bool HandlePlayerLfg(Player* who, std::string_view tail);

// The WorldScript that runs one fill pass per AiPlayerbot.RandomBotLftUpdateInterval.
void RegisterScripts();

// The queued players, for the two command entries.
struct JoinResult
{
    bool ok = false;
    std::string code;           // empty when ok
    std::string detail;
    std::string dungeonName;
    std::vector<std::string> need;   // roles still to fill, tank first
};

struct LeaveResult
{
    bool queued = false;
    std::vector<std::string> released;   // bot names
};

struct StatusRow
{
    std::string player;
    std::uint32_t dungeon = 0;
    std::string dungeonName;
    std::uint8_t role = 0;               // engine role bit
    bool complete = false;
    std::vector<std::string> need;       // role names, tank first
    std::vector<std::string> filled;     // "<role>:<bot name>"
};

JoinResult Join(Player* player, std::uint32_t dungeonId, std::uint8_t role);
LeaveResult Leave(Player* player);
std::vector<StatusRow> Rows();
bool IsQueued(Player* player);
} // namespace lfg
} // namespace TortoiseBots
