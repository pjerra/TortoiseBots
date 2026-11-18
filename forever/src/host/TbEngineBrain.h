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

// Loads the engine's own configuration (PlayerbotAIConfig) once, before the first brain. Safe to call again: only the
// first call does the work and later calls return its result. Empty string = ready, else why the engine cannot run.
std::string EnsureConfig();

// Builds the brain for a bot that is in the world and placed next to `master`: PlayerbotAI, master, default strategies,
// registration where the legacy code looks a bot's AI up. Null (with `error` set) when it could not be built; the
// caller then keeps the spike behaviour for that bot.
Brain* Create(Player* bot, Player* master, std::string& error);

// One tick of the engine. `diff` is the world tick's elapsed milliseconds.
void Tick(Brain* brain, uint32 diff);

// Unregisters and deletes the brain. Must run before the bot's Player is deleted (see the logout paths in BotMgr).
void Destroy(Brain* brain);

// What `.tb where` prints for an engine bot: one line each for state, strategies and current target.
std::vector<std::string> Describe(Brain const* brain);

struct BrainDeleter
{
    void operator()(Brain* brain) const { Destroy(brain); }
};
}
}
