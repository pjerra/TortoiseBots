#pragma once

#include <cstdint>
#include <string_view>

class Player;

namespace TortoiseBots
{
namespace player { struct OwnerScope; }
namespace autogear
{

bool HandleBotAutogear(Player* caller, bool callerGm, player::OwnerScope const& scope, std::string_view tail);
void AddScripts();

} // namespace autogear
} // namespace TortoiseBots
