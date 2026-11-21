// S3 (throws and falls): the end of a thrown flight. A bot flies on a spline, so its movement info still holds the
// spot it was thrown from; Player::HandleFall reads exactly that position and the last fall z, which the core only
// fills from a client's CMSG_MOVE_FALL_LAND. Put both where the core's own handler would leave them, then let it
// calculate the damage: same thresholds, safe-fall and rate modifiers as a real player's fall.
#pragma once

#include "Player.h"

namespace tbcompat
{
// Fall damage the core applied for a flight from apexZ to the bot's current position.
inline uint32 FallLandCompat(Player* bot, float apexZ)
{
    if (!bot)
        return 0;
    bot->m_movementInfo.pos.Relocate(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation());
    uint32 const before = bot->GetHealth();
    bot->SetFallInformation(0, apexZ);
    bot->HandleFall();
    // The next fall starts where this one ended (Player::UpdateFallInformationIfNeed's own reset).
    bot->SetFallInformation(0, bot->GetPositionZ());
    return before > bot->GetHealth() ? before - bot->GetHealth() : 0;
}
}
