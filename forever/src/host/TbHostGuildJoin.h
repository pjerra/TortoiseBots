#pragma once

// The free-bot guild driver: the tick side of guild participation and the owner's guild management
// whispers. It is the caller the guild host seams (host/TbHostGuild.h) were missing.
//
// World thread only: TickFreeBotGuild runs from BotMgr::Update (and only for free bots), HandleWhisper from the
// engine's DeliverChat before the legacy chat commands see the line. No DB and no world scan on the accept path;
// founding is off by default and its NPC search is throttled per bot.

#include "ObjectGuid.h"

#include <string>

class Player;

namespace tbhost
{
namespace guildjoin
{

// One free bot, once per visit: accept or decline an outstanding invite, leave a guild that reached the member
// limit, and (when `formingEnabled`) run the founding legs.
void TickFreeBotGuild(Player* bot, bool formingEnabled);

// The bot left the world: its founding timers go with it.
void ForgetBot(ObjectGuid const& botGuid);

// An owner's `guild invite <name>` / `join` / `leave` / `promote <name>` / `demote <name>` / `remove <name>` /
// `leader <name>` whisper. True when the line was one of those and is answered here, so the engine's chat
// commands never see it; false for every other line.
bool HandleWhisper(Player* bot, Player* sender, std::string const& text);

// The other direction (TBF #30): a real player's whisper asking a bot of a bot-run guild for an invite ("ginv",
// "invite me to your guild", ...). True when the line is a request and is answered here - the invite goes through the
// guild host seam and the bot answers in one whisper - so the line never joins the engine's inbox; false for every
// other line. The rules are in host/TbHostGuildRequestPure.h.
bool HandleJoinRequest(Player* bot, Player* asker, std::string const& text);

} // namespace guildjoin
} // namespace tbhost
