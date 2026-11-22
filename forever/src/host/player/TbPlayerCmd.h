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
bool OwnsGameAccount(OwnerScope const& scope, std::uint32_t gameAccount);
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
}
}
