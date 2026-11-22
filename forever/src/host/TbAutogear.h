#pragma once

#include <cstdint>
#include <string_view>

class Player;

namespace TortoiseBots
{
namespace autogear
{

bool HandleBotAutogear(Player* caller, bool callerGm, std::uint32_t callerBnet, std::string_view tail);
void AddScripts();

} // namespace autogear
} // namespace TortoiseBots
