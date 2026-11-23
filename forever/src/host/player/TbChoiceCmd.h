// `.bot totem` and `.bot behavior` (#133 round 1): the class-choice writes a player commands on its own bots. The rules without
// the core (the toggle table, the scope word, the tokenizer, the tier) are host/player/TbChoiceCmdPure.h; the live objects and
// the reply channel are the `.bot` entry's (TbPlayerCmd.cpp: FactsOf, ResolveBot, Say, Refuse), shared through TbPlayerCmd.h.
//
// Threading: world thread with the maps idle (a chat opcode is PROCESS_THREADUNSAFE), so a handler sets AI values and strategies
// directly, the way `.tb strategy` does. A value lives until logout: persistence is #135.
#pragma once

#include <string_view>

class Player;

namespace TortoiseBots
{
namespace player
{
struct Caller;
struct OwnerScope;
}
namespace choice
{
// `.bot totem <bot|party> <earth|fire|water|air> <totem|auto|none> | set <earth>,<fire>,<water>,<air> | mode <auto|prepull|manual> | status`
bool HandleTotem(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail);
// `.bot behavior <bot|party> <toggle> <on|off> | status`
bool HandleBehavior(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail);
}
}
