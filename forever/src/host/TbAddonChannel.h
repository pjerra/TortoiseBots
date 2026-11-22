// The addon channel (TBF #130 piece f-chan): the TBM addon prefix over the
// core's addon-message hooks, a world-thread FIFO and the Send/Ack/Err layer
// the other pieces answer through. Verbs are owned by the other pieces: each
// exposes one RegisterAddonVerbs<Piece>() called from TbScripts.cpp right
// after AddScripts() (f-roster, f-actions, f-verbs, f-info). Unknown verbs
// answer TBM:ACTION_ERR|verb|unsupported|..., so the addon can fail closed.
//
// Threading: the hook runs wherever the core processes the addon packet; the
// queue is drained by a WorldScript OnUpdate on the world thread, before the
// next tick's work. Entries hold the requester's guid halves and a copy of
// the text, never a Player*: the drain looks the player up again and drops
// the command when the player logged out in between.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace addon
{
// A verb implementation: the requester live on the world thread, args the
// text after the verb word, trimmed. Runs inside the queue drain, in arrival
// order; answers with Send/Ack/Err.
using VerbHandler = void (*)(Player* requester, std::string_view args);
// Startup only; the verb is lowercased. Registering also advertises the bare
// verb itself (see Advertise).
void RegisterVerb(std::string_view verb, VerbHandler handler);
// Adds a TBM:VERBS token ("roster", or "action:follow" for an action intent).
void Advertise(std::string_view token);
// The exact token list this server answers natively: sorted, unique. The
// roster handshake emits it right before TBM:CAPS; the addon uses it to pick
// native vs bridge per command.
std::vector<std::string> AdvertisedVerbs();
// One addon message per non-empty line ('\n' splits), prefix TBM, to the
// requester itself (Player::WhisperAddon to self, not logged). Lines over 255
// bytes are truncated (logged once). Silently drops null or logged-out
// requesters.
void Send(Player* to, std::string_view line);
// TBM:ACTION_ACK|intent|scope|count|detail (TB's SendActionAck shape).
void Ack(Player* to, std::string_view intent, std::string_view scope, std::uint32_t count, std::string_view detail);
// TBM:ACTION_ERR|intent|code|message (TB's SendActionError shape).
void Err(Player* to, std::string_view intent, std::string_view code, std::string_view message);
// TB's ProtocolSafe: '|' CR LF become blanks, empty becomes "-".
std::string Safe(std::string_view field);
// Registers the hook and the drain. Nothing with the engine off.
void AddScripts();
} // namespace addon
} // namespace TortoiseBots
