// Turtle side of the both-core helpers for W8 (auction house).
// ForeverCore side: forever/src/compat/TbCompat_WL8.h.
#pragma once

inline Creature* AuctioneerIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return static_cast<Creature*>(bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER));
}
