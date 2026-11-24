#pragma once

// Host thievery (TBF #153): rogue pickpocketing, lockbox opening from the
// bot's own bags, and the lockbox-by-mail service between guild bots. No
// legacy action exists for any of the three (TB has no pickpocket action and
// only a trade/bag unlock action, and ForeverCore carries neither), so the
// work rides the core's own handlers from the per-bot host tick: Pick Lock
// 1804 on a bag item, Stealth 1784 + Pick Pocket 921 on a creature, the
// item's loot window through CMSG_OPEN_ITEM / CMSG_LOOT_ITEM /
// CMSG_LOOT_MONEY, and the guild mail seam for the mailed box and its
// return. World thread, memory only: the mailbox owns the queued box until
// the rogue takes it out.
#include "ObjectGuid.h"

#include <cstdint>

class Player;
class Mail;

namespace tbhost
{
namespace thievery
{

// TortoiseBots.World.Pickpocket, .Lockboxes, .LockboxMail (default 1 each),
// read once with the other world keys.
void LoadConfig();
bool PickpocketEnabled();
bool LockboxesEnabled();
bool LockboxMailEnabled();

// One per-bot pass: advance the running errand else look for work. Free bots
// only (the caller decides), beside the other host services.
void TickThievery(Player* bot, uint32_t diffMs);

// True while the bot is stealthing, walking to a pocket, opening a box or
// walking to a mailbox for the service: the engine pauses.
bool HasActiveErrand(Player const* bot);

// A guild member bot's `lockbox` mail with an attached box: the service owns
// it, the generic mail take must leave it in the mailbox.
bool IsLockboxServiceMail(Player* bot, Mail* mail);

// The bot is leaving the world: its errand state goes.
void ForgetBot(uint32_t botGuidLow);

} // namespace thievery
} // namespace tbhost
