#include "host/hire/TbHireConfig.h"

#include "compat/botpch_forever.h"

#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>

namespace TortoiseBots
{
namespace hire
{

std::uint32_t DisconnectGraceSeconds()
{
    return std::min<std::uint32_t>(sPlayerbotAIConfig.hireDisconnectGracePeriod, 3600);
}

bool AllowPvP()
{
    return sPlayerbotAIConfig.hireAllowPvP;
}

std::uint32_t MaxCompanions()
{
    return std::min<std::uint32_t>(sPlayerbotAIConfig.hireMaxBotsPerPlayer, 39);
}

} // namespace hire
} // namespace TortoiseBots
