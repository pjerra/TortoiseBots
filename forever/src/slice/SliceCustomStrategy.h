// Piece P135-C (TBF #135): guarded `ai_playerbot_custom_strategy` store helpers.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class PlayerbotAI;

namespace ai
{
namespace slice
{
namespace aipreset
{

std::vector<std::string> ListCustomStrategies(uint32_t owner);
std::vector<std::pair<uint32_t, std::string>> ReadCustomStrategyLines(uint32_t owner, std::string const& name);
bool EditCustomStrategyLine(PlayerbotAI* ai, uint32_t owner, std::string const& name, uint32_t idx, std::string const& command);

} // namespace aipreset
} // namespace slice
} // namespace ai
