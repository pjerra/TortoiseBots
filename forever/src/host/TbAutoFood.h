// Seeds pool and free bots with level-fitting vendor food (and water for mana users) on login and level-up.
#pragma once

class Player;

namespace TortoiseBots
{
namespace autofood
{

void LoadConfig();
bool Enabled();
void Sync(Player* bot, char const* why);
void TickTopUp(Player* bot);

} // namespace autofood
} // namespace TortoiseBots
