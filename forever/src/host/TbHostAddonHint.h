#pragma once

// The add-on hint (TBF #46): a real player whose client never spoke the TBM
// addon prefix is told once where to get TortoiseBotsForeverManager, and the
// characters-DB row `tortoise_bots_player_flag` (`flag = 'addon-hint'`) keeps
// it once across relogs and restarts. The rule without the core is
// host/TbHostAddonHintPure.h; this is the live half.
//
// Threading: NoteAddonSeen touches only a mutex-guarded set, and the world
// tick takes that set, decides and answers one pending character under the
// same mutex: an observation that lands before the decision always wins, one
// that lands after it comes from a character already told. ForeverCore
// dispatches the addon opcode as PROCESS_THREADUNSAFE (world thread), so today
// both run there; the mutex keeps the rule if the core moves that handler.
// Every other entry point runs on the world thread; the rows that one tick
// settles go to the characters store in one statement. A session above
// SEC_PLAYER is dropped without a line or a row. A missing table degrades to
// once per server run (one warning), never to a repeat in a session.
#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <cstdint>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace addonhint
{
// The three keys, read once at startup: TortoiseBots.Player.AddonHint,
// .AddonHintDelay (seconds, clamped 10..300) and .AddonHintUrl.
void LoadConfig();

// Creates or probes the table and loads the flagged characters (one guarded
// SELECT). Startup only, from the world's initialisation hook.
void LoadAtStartup();

// A real player entered the world.
void NoteLogin(Player* player);

// A TBM addon message arrived from a character (guid halves, hook thread).
void NoteAddonSeen(uint64_t lo, uint64_t hi);

// World tick: send the hints that are due and persist what was seen.
void Tick(uint32_t nowMs);

// The character left the world.
void Forget(ObjectGuid guid);

// `.tb addonhint status|reset <character>`.
bool HandleCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args);
} // namespace addonhint
} // namespace TortoiseBots
