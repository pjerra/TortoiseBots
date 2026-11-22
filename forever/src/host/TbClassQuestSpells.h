// Teaches a bot the class spells a real player gets from its class quests (Summon Imp first). The allow-list and the rules
// are core-free (host/TbClassQuestSpellsPure.h); this half reads the quest store once and applies the answer to a bot.
//
// Nothing here casts anything or writes a quest status: a bot learns the spell the way Spell::EffectLearnSpell does for a player
// (Player::LearnSpell), so the real quest reward later finds the spell known.
#pragma once

#include <cstdint>

class Player;

namespace TortoiseBots
{
namespace questspells
{

// TortoiseBots.ClassQuestSpells (default 0), read by the script's OnConfigLoad.
void LoadConfig();
bool Enabled();

// World thread only: reads sObjectMgr->GetQuestTemplates() and the spell store, checks every row of the allow-list and swaps the
// index in. A row is disabled (one WARN line) when the running data does not support it.
void BuildIndex();

// Teaches what the bot's class and level have earned and it does not know yet; returns how many spells were taught. `why` is the
// trigger (`login` or `level`) and only names the log line.
uint32_t Sync(Player* bot, char const* why);

} // namespace questspells
} // namespace TortoiseBots
