// Issue #15: `place` (a shape at a chosen spot, held after every fight), `rally here` (the fall-back point) and `fall back` (the whole group drops its
// target and re-forms at the rally point). These are `.tb cmd` verbs (host/cmd/TbCmdOrders.cpp routes them here before the lifted order judge) and the
// same words work as a whisper or party chat to the commander's bots (PlayerScript::OnChat/OnChatSent, TbScripts.cpp).
//
// The geometry, the words and the minimap-ping layout are pure (host/cmd/TbPlacePure.h, forever/tools/test_place_pure.cpp); this header is the world half:
// it resolves the commander's own bots, snaps each slot to the ground with the module's height helper, skips a slot with no path (the same PathGenerator
// the move judge reads), and queues the engine's own `come` order per bot. The fall back ends on `attack`, `pull`, `pullback`, `follow` or `resume`.
//
// The command surface (forever/docs/TB-COMMANDS.md section 17):
//   .tb cmd place <close|circle|arc|spread|line|column|spear|arrow> [tight|normal|wide] [here|target|cursor] [who...]
//   .tb cmd place follow [who...]
//   .tb cmd rally here
//   .tb cmd fallback | .tb cmd fall back | .tb cmd back
//   .tb cmd resume
//
// Threading: world thread only (commands, the packet hook of ProcessSessionUpdate, the 1 Hz tick of OnBeforeMapsUpdate): plain data, no lock.
#pragma once

#include "host/TbH3.h"

#include <string>
#include <vector>

class ChatHandler;
class Player;
class WorldPacket;
class WorldSession;

namespace TortoiseBots
{
namespace place
{
// `.tb cmd place|rally|fallback|fall back|back|resume ...`: `args.w[0]` is the verb, the echo token is kept for the reply. False when the words are not
// one of these verbs (the caller then goes on to the lifted order judge).
bool Handle(ChatHandler* handler, h3::Words const& args);

// The chat form of the same words (a whisper to one of the commander's bots, or party/raid chat of a group that holds them). `text` is the chat text as
// typed; the reply lines go to the sender's session. False when the text is not a place command.
bool HandleChat(Player* sender, std::string const& text);

// True when `text` starts with one of the place words (the chat hooks ask this before touching anything). Cheap: no world access.
bool IsPlaceText(std::string const& text);

// ServerScript::OnPacketReceive: the commander's minimap ping (CMSG_MINIMAP_PING) completes a pending `place ... cursor`. The packet is only read.
void OnPacket(WorldSession* session, WorldPacket& packet);

// An order that ends a fall back (`attack`, `pull`, `pullback`) or releases a hold (`follow`) went out for this commander's bots: the hold is dropped,
// nothing is queued here (the caller's order is what the bots do).
void NoteOrdered(ObjectGuid commander, std::string const& verb);

// A later command from the commander: a pending `place ... cursor` is answered with `error code=cancelled` and forgotten. Called by the `.tb cmd` and
// `.tb order` entries for every command, and by Handle for the chat form, so a ping can never complete a command the commander replaced.
void CancelPending(ObjectGuid commander, ChatHandler* handler);

// PlayerScript::OnLogout: the commander's rallies, holds and pending cursor request are dropped.
void NoteLogout(Player* player);

// Once a second (the same tick as cmd::Maintain): the cursor timeouts and the walk back of a placed bot that lost its slot.
void Update();
}
}
