// Automatic starter hunter pet on reaching level 10 and on login catch-up.
// The pure decision lives in host/TbAutoHunterPetPure.h; this host half reads the bot's stable and calls PlayerbotFactory.
#pragma once

class Player;

namespace TortoiseBots
{
namespace autohunterpet
{

// TortoiseBots.AutoHunterPet (default 1), read by the script's OnConfigLoad.
void LoadConfig();
bool Enabled();

// Gives a level-10+ hunter bot its race's starter pet and trains its pet spells when it has no pet out or in its stable.
// Returns true if a pet was granted. `why` names the trigger (`login` or `level`) in the log line.
bool Sync(Player* bot, char const* why);

} // namespace autohunterpet
} // namespace TortoiseBots
