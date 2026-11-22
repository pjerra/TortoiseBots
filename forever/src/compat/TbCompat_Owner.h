// ForeverCore side of the owner gate of the chat words (R5 fix of PLAYER-COMMANDS-SPEC, ADR 0005). Turtle side: ai/cmangos-compat-owner.h.
// Reached through ai/playerbot/TbCompatOwner.h.
#pragma once

#include "host/player/TbPlayerCmd.h"

// May this sender give the bot an owner-tier chat word? T the sender's game account is the bot character's account. F ownership is the
// Battle.net account, decided exactly as the `.bot` commands decide it (TbPlayerCmd.cpp OwnerScope: the sender's Battle.net account against
// the bot's game account, read fresh now, never kept), so the owner's other game account owns the bot too. `senderAccount` is unused here.
inline bool BotOwnedBySenderCompat(Player* bot, Player const& sender, uint32 /*senderAccount*/)
{
    return bot && TortoiseBots::player::CallerOwnsBotForChat(sender, *bot);
}
