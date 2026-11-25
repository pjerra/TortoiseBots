// The two hire settings the lifecycle reads, in one place so the legacy config header (and the compat shim it needs)
// stays out of the ledger's translation unit.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace hire
{

// AiPlayerbot.HireDisconnectGracePeriod, clamped to an hour (0 = dismiss at once).
std::uint32_t DisconnectGraceSeconds();

// AiPlayerbot.HireAllowPvP: hires may enter battlegrounds.
bool AllowPvP();

// AiPlayerbot.HireMaxBotsPerPlayer (the ordinary companion limit).
std::uint32_t MaxCompanions();

} // namespace hire
} // namespace TortoiseBots
