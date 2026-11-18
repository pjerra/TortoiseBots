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
// build may already have pre-included it). It also defines the legacy `sLog`, which replaces the core's: TC_LOG_* does
// not compile after it, so this file logs with sLog.outString/outError, as the legacy code does.
#include "compat/botpch_forever.h"

#include <exception>
#include <filesystem>
#include <memory>
#include "ByteBuffer.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Unit.h"

#include "playerbot/BotActionLog.h"
#include "playerbot/BotState.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/WorldPosition.h"
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

    // The engine reads aiplayerbot.conf itself, from the directory of the worldserver.conf the server was started with
    // (PlayerbotAIConfig.cpp:650-658; findings item 19). The CMake CONFIGS list puts the .dist file in worldserver.conf.d, where
    // the core's own parser would read it as part of the main config; the deployment copies it one directory up. Say so when it
    // is missing, instead of leaving only the engine's "No configuration file" line.
    std::string const mainConfig = sConfig.GetFilename();
    size_t const slash = mainConfig.find_last_of("/\\");
    std::string const engineConfig = (slash == std::string::npos ? std::string() : mainConfig.substr(0, slash + 1)) + "aiplayerbot.conf";
    if (std::string(sConfig.GetStringDefault("AiPlayerbot.ConfigFile", "")).empty() && !std::filesystem::exists(engineConfig))
    {
        result = "no " + engineConfig + " (copy aiplayerbot.conf.dist from worldserver.conf.d to that path)";
        return result;
    }

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

    // The one thing the PlayerbotAI constructor writes to the Player is the taxi-cheat flag (PlayerbotAI.cpp:212, when the
    // `taxi` cheat is configured); the AI's other state is its own. A failed Create must leave the Player as it found it
    // (findings item 10), so the flag is read first and put back on every failure path below.
    bool const taxiCheatBefore = bot->IsTaxiCheater();
    auto const restorePlayer = [&]()
    {
        if (!taxiCheatBefore && bot->IsTaxiCheater())
            bot->SetTaxiCheater(false);
    };

    // The bot must already count as OWNED here (findings items 1 and 10): `PlayerbotAI::IsOwnedBot` asks
    // BotManager::IsBot / IsRandomBot, which the host answers from BotMgr's entry (host/TbHostBotManager.cpp), and that entry
    // exists from `BotMgr::Login` on, so the constructor's "auto talents" branch (PlayerbotAI.cpp:257-260) is closed.
    //
    // PlayerbotAIAdapter::Initialize (:36-52). The legacy constructor builds the context with
    // AiFactory::createAiObjectContext and the four engines (PlayerbotAI.cpp:232-239).
    PlayerbotAI* pai = nullptr;
    try
    {
        pai = new PlayerbotAI(bot);
        pai->SetMaster(master);
        // The engines were built before the master was known, so saved strategies and values are applied now (:41-44).
        pai->ResetStrategies();
        // The movement mode. The adapter re-applies the default (:45-49): "follow" for a master with a network session,
        // "wander" otherwise (GetDefaultMovementStrategy). A host master is whoever the console or the hire flow named; it is
        // a bot session when no client drives it, and the default would then pick "wander" (a free-roaming mode the slice does
        // not register). The host bound this master on purpose, so the bot follows it: the same strategy change the chat
        // order "follow" makes (SetMovementStrategy("follow")).
        pai->SetMovementStrategy("follow");
    }
    catch (std::exception const& e)
    {
        delete pai;
        restorePlayer();
        error = std::string("the engine threw while it was built: ") + e.what();
        return nullptr;
    }

    if (!pai->GetAiObjectContext())
    {
        delete pai;
        restorePlayer();
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
        restorePlayer();
        error = "the engine is not usable after creation (no current engine or registry mismatch)";
        return nullptr;
    }

    sLog.outString("TortoiseBots engine: PlayerbotAI attached for %s (%s) master %s", bot->GetName().c_str(),
        bot->GetGUID().ToString().c_str(), master->GetName().c_str());
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
        sLog.outError("TortoiseBots engine: dropped a malformed packet while updating bot %s", brain->bot->GetName().c_str());
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
    if (target)
    {
        // Reach (findings items 12 and 16): the distance between the two units, the core's melee range for the pair and whether
        // the core calls it in melee range, so a bot that stops short or runs through shows here.
        Player* self = pai->GetBot();
        lines.push_back("target reach: distance " + std::to_string(self->GetDistance(target)) + " yd, melee range " +
            std::to_string(self->GetMeleeRange(target)) + " yd, in melee range: " + (self->IsWithinMeleeRange(target) ? "yes" : "no") +
            ", victim: " + (self->GetVictim() ? self->GetVictim()->GetName() : "none"));
        // What gates a chase (MovementAction::ChaseTo: CanMove, IsSafe; PlayerbotAI::CanMove's jump/fall/control reads), for a bot that
        // stands off its target.
        {
            WorldPosition const here(self);
            WorldPosition there(target->GetMapId(), self->GetPositionX() + 1.0f, self->GetPositionY(), self->GetPositionZ());
            float const hereHeight = here.GetHeight();
            float const thereHeight = there.GetHeight();
            lines.push_back("ground: height at the bot " + std::to_string(hereHeight) + " (bot z " + std::to_string(self->GetPositionZ()) +
                "), 1 yd east " + std::to_string(thereHeight) + ", map " + (here.GetMap(here.GetFirstInstanceId()) ? "found" : "NOT found") +
                ", first instance id " + std::to_string(here.GetFirstInstanceId()) + ", valid " + (here.isValid() ? "yes" : "no"));
        }
        lines.push_back(std::string("chase gates: can move ") + (pai->CanMove() ? "yes" : "no") + ", safe(target) " +
            (pai->IsSafe(target) ? "yes" : "no") + ", jumping " + (pai->IsJumping() ? "yes" : "no") + ", falling " +
            (self->IsFalling() ? "yes" : "no") + ", lost control " + (self->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) ? "yes" : "no") +
            ", rooted " + (self->IsRooted() ? "yes" : "no") + ", motion " + std::to_string(int(self->GetMotionMaster()->GetCurrentMovementGeneratorType())));
    }

    // What the follow rule reads (strategy/triggers/RangeTriggers.h UpdateFollowTrigger): the engine's master, whether it counts
    // as a real player (a network session: a master that is itself a bot session does not), the "follow target" value, the
    // distance to the master and the motion the bot is in. A bot that does not follow shows here which of them is wrong.
    AiObjectContext* context = pai->GetAiObjectContext();
    Player* master = pai->GetMaster();
    lines.push_back(std::string("master: ") + (master ? master->GetName() : "none") + (pai->HasRealPlayerMaster() ? " (real player)" : " (no network session)"));
    Unit* followTarget = nullptr;
    if (ai::Value<Unit*>* value = context->GetValue<Unit*>("follow target"))
        followTarget = value->Get();
    float masterDistance = -1.0f;
    if (ai::Value<float>* value = context->GetValue<float>("distance", "master target"))
        masterDistance = value->Get();
    lines.push_back(std::string("follow target: ") + (followTarget ? followTarget->GetName() : "none") + ", safe: " +
        (followTarget && pai->IsSafe(followTarget) ? "yes" : "no") + ", distance to master target: " + std::to_string(masterDistance) +
        ", motion type: " + std::to_string(int(pai->GetBot()->GetMotionMaster()->GetCurrentMovementGeneratorType())));
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
