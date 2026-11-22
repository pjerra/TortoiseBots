// Turtle side of the owner gate of the chat words (R5 fix of PLAYER-COMMANDS-SPEC). ForeverCore side: forever/src/compat/TbCompat_Owner.h.
// Reached through ai/playerbot/TbCompatOwner.h.
#pragma once

// May this sender give the bot an owner-tier chat word? Turtle has no Battle.net account: the sender's game account is the bot character's
// account (the rule PlayerbotAI::IsBotOwnerOrGm always ended on).
inline bool BotOwnedBySenderCompat(Player* bot, Player const& /*sender*/, uint32 senderAccount)
{
    return AccountIdOfCharacterCompat(bot->getObjectGuid()) == senderAccount;
}
