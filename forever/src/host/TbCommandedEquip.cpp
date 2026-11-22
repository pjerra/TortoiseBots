// Config bridge for the commanded-bot auto-equip gate (declared in
// compat/TbCompat_W6.h): TB parity reads AiPlayerbot.ClaimedBotSelfEquip.
// PlayerbotAIConfig.h needs PlayerbotAI declared first, so the compat header
// cannot include it; the read lives here. Off-slice this answers the shipped
// default and never runs (no engine is built in).
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#endif

namespace tbs_w6
{
bool CommandedBotSelfEquipEnabled()
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    return sPlayerbotAIConfig.claimedBotSelfEquip;
#else
    return true;
#endif
}
}
