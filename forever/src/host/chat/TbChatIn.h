// S1-R2 (chat commands in): the way a chat line reaches a bot.
//
// PlayerScript::OnChat (a whisper, or a party/party-leader line) copies only the sender's guid, the receiver's (or the group's) guid,
// the kind, the language and the text into an Arrival (host/chat/TbChatInPure.h). The core calls the hook on the world thread for its
// chat handlers, but Player::Whisper is a public script entry too; the hook therefore touches no Player, Group, BotMgr or binding
// state. `DrainArrivals()` resolves the guids on the world thread (from BotMgr::Update), judges the line once per receiving bot
// (host/chat/TbChatInPure.h) and, when accepted, puts it in that bot's inbox. The engine drains the inbox at the start of its next
// tick (engine::DeliverChat, host/TbEngineBrain.cpp) into PlayerbotAI::HandleCommand, which keeps its own security level and command
// separator/prefix rules, and checks once more that the sender may still command that bot. Nothing here writes the chat text, records
// heard messages, sees a `.tb` line (the core parses those first) or validates a link (the core does that; the engine's parsers read
// the link text with the S1-R1 rules).
//
// The hook exists in both builds; without the engine no bot has a brain, so every line is refused with `no-engine` and the hook is inert.
#pragma once

#include "host/chat/TbChatInPure.h"
#include "ObjectGuid.h"

#include <cstdint>
#include <string>

class Player;

namespace TortoiseBots
{
namespace h3 { class Rec; }
namespace chatin
{
// One line of `AddSC_tortoisebots`: registers the PlayerScript whose OnChat hooks queue arrivals.
void AddScripts();

// World thread (the top of BotMgr::Update): resolve the queued arrivals, judge each one for each receiving bot and fill the inboxes.
// At most IngressCapacity lines per call; an arrival whose sender or receiver is gone is dropped.
void DrainArrivals();

// The inbox of the bot whose guid counter is `botKey`: the next queued line, oldest first. Called by engine::DeliverChat on the world
// thread. False when the box is empty.
bool Pop(std::uint32_t botKey, Envelope& out);

// A line of this bot's inbox that will not run (its sender has gone, the sender may no longer command the bot, or the bot may not act
// this tick): counted as dropped, so `.tb state` never shows `accepted` for a command that never ran.
void Drop(std::uint32_t botKey);

// Whether the sender may still command the bot NOW: the same rule Judge used at admission (master, commander binding, own group),
// read again on the world thread right before the line executes.
bool StillAuthorized(Player* bot, Player* sender);

// The bot is leaving the world: its box (and its counters) goes with it, so a later bot reusing the guid starts clean.
void Forget(ObjectGuid bot);

// `.tb hear <bot> <sender> whisper|party <text>`: the line a real sender would produce, judged by the same rule with the `injected` marker
// (only the `not-real` test is skipped) and queued like one from the world. Returns the verdict, for the command's reply.
Verdict Hear(Player* bot, Player* sender, unsigned kind, std::string const& text);

// The `.tb state` block this round appends to a bot's record: `chatin=<accepted>:<refused>:<dropped>` and `chatlast=<word>` (empty
// until a line was judged). Defined with the slice only (TbChatIn.cpp).
void AppendState(h3::Rec& rec, ObjectGuid bot);
}
}
