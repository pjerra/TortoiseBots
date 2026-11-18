// The engine brain of one bot: a real PlayerbotAI (the legacy strategy engine), created, ticked and destroyed by
// TortoiseBots::BotMgr. This header includes NO legacy header, so the rest of the module never sees a legacy type.
//
// Everything here is implemented in TbEngineBrain.cpp. When the module is built without TORTOISEBOTS_SLICE1 the same
// functions exist as "not built with the slice" versions: Built() is false and Create() refuses.
//
// Threading: every function runs on the world thread (WorldScript::OnBeforeMapsUpdate, chat/console commands and the
// PlayerScript logout hook), the same thread the Turtle build ticks the engine on.
#pragma once

#include "Define.h"
#include <string>
#include <vector>

class Player;

namespace TortoiseBots
{
namespace engine
{
// Opaque: holds the PlayerbotAI of one bot.
struct Brain;

// True when this build contains the engine (TORTOISEBOTS_SLICE1).
bool Built();

// Loads the engine's own configuration (PlayerbotAIConfig) once, before the first brain (WorldScript::OnWorldInitialized
// calls it when the engine is built in and the module is enabled, so the first bot does not pay for it). Safe to call again:
// a success is remembered and later calls return at once; a failure is NOT remembered, the next call tries again (the next
// brain, `.tb engine on`, a config reload). Empty string = ready, else why the engine cannot run.
std::string EnsureConfig();

// Builds the brain for a bot that is in the world and placed next to `master`: PlayerbotAI, master, default strategies,
// registration where the legacy code looks a bot's AI up. Null (with `error` set) when it could not be built; the
// caller then keeps the spike behaviour for that bot.
Brain* Create(Player* bot, Player* master, std::string& error);

// One tick of the engine. `diff` is the world tick's elapsed milliseconds. Before the engine's UpdateAI the bot's queued cast
// results (BotSession, TbBotSession.h) are handed to the engine: SPELL_FAILURE -> PlayerbotAI::SpellInterrupted, CAST_FAILED ->
// the action log's CAST_FAILED_ASYNC line. The queue is filled only while the bot has a brain (SLICE2-R0-NOTES.md).
void Tick(Brain* brain, uint32 diff);

// R10 (S3): the bot's teleport is being acknowledged by the host session: PlayerbotAI::HandleTeleportAck (transition generation, teleport grace, stale
// queues, the post-teleport landing logic), which on the host replaces the engine-driven ack of the Turtle build (BotManager.cpp:2404). A same-map
// teleport: called before the session acknowledges it (the engine does it itself); a far one: after, with the Player in the new world.
void HandleTeleportAck(Brain* brain);

// Unregisters and deletes the brain. Must run before the bot's Player is deleted (see the logout paths in BotMgr).
void Destroy(Brain* brain);

// `.tb castfail <bot> <spell> <reason> [interrupt]`: sends the bot's session a real SMSG_CAST_FAILED (or, with `interrupt`, an
// SMSG_SPELL_FAILURE of its own cast) through Player::SendDirectMessage, so it travels the path of a core failure: the session's
// packet reader, the bounded queue, and the drain at the start of the next engine tick. Empty string = sent, else why not.
std::string InjectCastResult(Player* bot, uint32 spellId, uint32 reason, bool interrupted);

// What `.tb where` prints for an engine bot: one line each for state, strategies and current target, and the cast-result counts.
std::vector<std::string> Describe(Brain const* brain);

struct BrainDeleter
{
    void operator()(Brain* brain) const { Destroy(brain); }
};
}
}
