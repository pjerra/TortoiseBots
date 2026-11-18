// The only translation unit that touches the legacy engine (PlayerbotAI) from the ForeverCore host. Everything
// engine-facing is behind TORTOISEBOTS_SLICE1; without it this file compiles the "not built with the slice" versions
// of the functions in TbEngineBrain.h and includes no legacy header.
//
// The lifecycle mirrors the Turtle build, step by step (file:line of the Turtle tree in the comments):
//   Create  = PlayerbotAIAdapter::Initialize       (runtime/PlayerbotAIAdapter.cpp:30-62) plus IsUsable (:79-85)
//   Tick    = PlayerbotAIAdapter::Update           (runtime/PlayerbotAIAdapter.cpp:64-77), called as BotManager.cpp:2431
//   Destroy = PlayerbotAIAdapter::Shutdown         (runtime/PlayerbotAIAdapter.cpp:149-158)
// See forever/docs/SLICE1-R5-NOTES.md.
#include "TbEngineBrain.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

// The ForeverCore counterpart of ai/botpch.h, first, as for every legacy source (idempotent: #pragma once, and the
// build may already have pre-included it).
#include "compat/botpch_forever.h"

#include <memory>
#include "ByteBuffer.h"
#include "Log.h"
#include "Player.h"
#include "Unit.h"

#include "playerbot/BotActionLog.h"
#include "playerbot/BotState.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace engine
{
struct Brain
{
    Player* bot = nullptr;       // only compared and passed back to the registry after the Player is gone, never dereferenced then
    uint32 guidLow = 0;          // the bot's low guid: the action log is closed by it (the Player may be gone)
    PlayerbotAI* ai = nullptr;
};

namespace
{
bool Usable(Brain const* brain)
{
    // PlayerbotAIAdapter::IsUsable (runtime/PlayerbotAIAdapter.cpp:79-85), without the adapter's own flag.
    return brain && brain->ai && brain->bot && brain->bot->IsInWorld() && brain->ai->GetBot() == brain->bot &&
        brain->ai->GetAiObjectContext() && brain->ai->GetCurrentEngine() &&
        PlayerbotAIStorage::Instance().GetAI(brain->bot) == brain->ai;
}

void Dispose(Brain* brain)
{
    // PlayerbotAIAdapter::Shutdown (:149-158): RemoveAI first (hooks resolve the AI through the registry, so none can
    // reach it afterwards), then the action log, then the AI itself.
    if (brain->bot)
        PlayerbotAIStorage::Instance().RemoveAI(brain->bot);
    if (brain->guidLow)
        ai::botdiag::BotActionLog::Close(brain->guidLow);
    delete brain->ai;
    brain->ai = nullptr;
    delete brain;
}
}

bool Built()
{
    return true;
}

std::string EnsureConfig()
{
    // The Turtle host calls sPlayerbotAIConfig.Initialize() once at startup (host/BotHostAdapter.cpp:116). Here it is
    // done lazily, on the first brain, which is after World::SetInitialWorldSettings (Initialize reads the object
    // manager and, for the random-bot keys, the databases).
    //
    // Initialize() reads aiplayerbot.conf through the legacy `Config::SetSource` and the host's `sConfig`
    // (ai/playerbot/PlayerbotAIConfig.cpp:650-676), and returns false when there is no such file. ForeverCore's
    // ConfigMgr has neither: the adaptor belongs to package R4C (see SLICE1-R5-NOTES.md).
    static bool attempted = false;
    static std::string result;
    if (attempted)
        return result;
    attempted = true;

    if (!sPlayerbotAIConfig.Initialize())
        result = "PlayerbotAIConfig::Initialize failed (no aiplayerbot.conf or AiPlayerbot.Enabled = 0; see the server log)";
    else if (!sPlayerbotAIConfig.enabled)
        result = "AiPlayerbot.Enabled is 0";
    return result;
}

Brain* Create(Player* bot, Player* master, std::string& error)
{
    if (!bot || !bot->IsInWorld())
    {
        error = "the bot is not in the world";
        return nullptr;
    }
    if (!master)
    {
        error = "the bot has no master";
        return nullptr;
    }

    error = EnsureConfig();
    if (!error.empty())
        return nullptr;

    // PlayerbotAIAdapter::Initialize (:36-52). The legacy constructor builds the context with
    // AiFactory::createAiObjectContext and the four engines (PlayerbotAI.cpp:232-239).
    PlayerbotAI* pai = new PlayerbotAI(bot);
    pai->SetMaster(master);
    // The engines were built before the master was known, so saved strategies and values are applied now (:41-44).
    pai->ResetStrategies();
    // Re-applies the movement default ("follow" for a player master) (:45-49).
    pai->EnsureDefaultMovementStrategy();

    if (!pai->GetAiObjectContext())
    {
        delete pai;
        error = "the engine has no object context";
        return nullptr;
    }

    std::unique_ptr<Brain> brain = std::make_unique<Brain>();
    brain->bot = bot;
    brain->guidLow = bot->GetGUID().GetCounter();
    brain->ai = pai;

    ai::botdiag::BotActionLog::NoteLogin(brain->guidLow);
    PlayerbotAIStorage::Instance().SetAI(bot, pai);

    if (!Usable(brain.get()))
    {
        Dispose(brain.release());
        error = "the engine is not usable after creation (no current engine or registry mismatch)";
        return nullptr;
    }

    TC_LOG_INFO("tortoisebots", "engine: PlayerbotAI attached for {} ({}) master {}", bot->GetName(), bot->GetGUID().ToString(),
        master->GetName());
    return brain.release();
}

void Tick(Brain* brain, uint32 diff)
{
    // World thread, from BotMgr::Update (WorldScript::OnBeforeMapsUpdate): the maps are not updating at this moment.
    // PlayerbotAIBase::UpdateAI keeps its own delay (PlayerbotAIBase.cpp:16-31), so calling every tick is right.
    if (!Usable(brain))
        return;
    try
    {
        brain->ai->UpdateAI(diff);
    }
    catch (ByteBufferException const&)
    {
        // Packet actions parse opcode-specific payloads; a bad one must drop this tick, not unwind through the world
        // update (the Turtle adapter does the same, runtime/PlayerbotAIAdapter.cpp:68-76).
        TC_LOG_ERROR("tortoisebots", "engine: dropped a malformed packet while updating bot {}", brain->bot->GetName());
    }
}

void Destroy(Brain* brain)
{
    if (brain)
        Dispose(brain);
}

namespace
{
std::string JoinStrategies(PlayerbotAI* pai, BotState state)
{
    std::string text;
    for (std::string_view name : pai->GetStrategies(state))
    {
        if (!text.empty())
            text += ", ";
        text.append(name);
    }
    return text.empty() ? "(none)" : text;
}
}

std::vector<std::string> Describe(Brain const* brain)
{
    std::vector<std::string> lines;
    if (!Usable(brain))
    {
        lines.push_back("engine: not usable");
        return lines;
    }

    PlayerbotAI* pai = brain->ai;
    lines.push_back("state: " + PlayerbotAI::BotStateToString(pai->GetState()));
    lines.push_back("combat strategies: " + JoinStrategies(pai, BotState::BOT_STATE_COMBAT));
    lines.push_back("non-combat strategies: " + JoinStrategies(pai, BotState::BOT_STATE_NON_COMBAT));

    // The engine's own "current target" value (AI_VALUE(Unit*, "current target"), e.g. AttackAction.cpp:313); the
    // value resolves its unit by guid, so a despawned target reads as none.
    Unit* target = nullptr;
    if (ai::Value<Unit*>* value = pai->GetAiObjectContext()->GetValue<Unit*>("current target"))
        target = value->Get();
    lines.push_back(std::string("target: ") + (target ? target->GetName() : "none"));
    return lines;
}
}
}

#else   // not built with the slice

namespace TortoiseBots
{
namespace engine
{
bool Built() { return false; }
std::string EnsureConfig() { return "this build has no engine (TORTOISEBOTS_SLICE1 is off)"; }
Brain* Create(Player*, Player*, std::string& error)
{
    error = "this build has no engine (TORTOISEBOTS_SLICE1 is off)";
    return nullptr;
}
void Tick(Brain*, uint32) {}
void Destroy(Brain*) {}
std::vector<std::string> Describe(Brain const*) { return { "engine: not built with the slice" }; }
}
}

#endif
