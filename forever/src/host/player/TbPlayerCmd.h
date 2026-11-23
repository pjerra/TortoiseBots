// The `.bot` entry a player types in game: the words, the caller's right to each, and the replies (D-1, D-4, D-6 of the player-command
// spec). The rule without the core is host/player/TbPlayerAuthPure.h; the live objects and the reply channel are TbPlayerCmd.cpp.
//
// Threading: world thread (a chat opcode is PROCESS_THREADUNSAFE; the hook runs inside ChatHandler::ParseCommands).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace player
{
struct OwnerScope;
struct BotFacts;
bool OwnsGameAccount(OwnerScope const& scope, std::uint32_t gameAccount);
// The bot lookup, the live facts of the pure rule and the reply channel, shared with the round-1 choice handlers
// (TbChoiceCmd.cpp). `Say` writes one "TortoiseBots: ..." line; `Refuse` the "TortoiseBots: <verb>: refused <code>: <text>" one.
bool ResolveBot(std::string_view name, Player*& bot, std::string& code);
BotFacts FactsOf(Player* requester, Player* bot, OwnerScope const& scope);
void Say(Player* player, std::string const& line);
void Refuse(Player* player, std::string const& verb, char const* code, std::string const& text);
// TortoiseBots.Player.Commands and TortoiseBots.Player.MaxBots, read at world config load and on a reload.
void LoadConfig();
// Addon-channel capture (f-verbs): while a sink is set, Say/Refuse append the reply line to it instead of chatting; null
// restores chat. The autogear Say honors the same sink. World thread only (chat opcodes and the addon drain both run there).
void SetReplySink(std::vector<std::string>* sink);
// Appends one line to the sink when one is set (true); false means chat as usual. The autogear Say routes through here so
// the addon channel captures the verbs answered outside TbPlayerCmd.cpp without changing their chat text.
bool CaptureReply(std::string const& line);
// Registers the `tortoisebots_player_cmd` PlayerScript whose PlayerScript::OnCommand claims the word `bot`. Nothing with the engine off.
void AddScripts();
// The text after `.bot` (or after the `bot` word of the command text). False only while the module or the engine is off: the core
// then answers the command as it does for any other.
bool Dispatch(Player* player, std::string_view rest);
// The owner tier of a chat word whispered to a bot (the legacy whisper gate, PlayerbotAI::IsBotOwnerOrGm): is the bot's game account one of the
// sender's Battle.net account's game accounts, read fresh now, the way `.bot` decides ownership? False for a stranger, for bnet 0 and with the
// engine off. GMs are decided by the caller.
bool CallerOwnsBotForChat(Player const& sender, Player const& bot);
}
}
