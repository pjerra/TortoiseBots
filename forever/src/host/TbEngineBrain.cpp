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
#include "TbBotSession.h"
#include "host/TbH3.h"
#include "host/TbCc.h"
#include "host/chat/TbChatIn.h"
#include "host/TbSpellDelayPure.h"
#include "host/TbThrownPure.h"
#include "host/TbHostTaxi.h"
#include "host/TbHostGuildJoin.h"
#include "host/TbHostWorldPvp.h"
#include "host/TbHostTransport.h"
#include "host/TbOnyxia.h"
#include "host/TbOnyxiaGround.h"
#include "slice/SliceDbStore.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

// The ForeverCore counterpart of ai/botpch.h, first, as for every legacy source (idempotent: #pragma once, and the
// build may already have pre-included it). It also defines the legacy `sLog`, which replaces the core's: TC_LOG_* does
// not compile after it, so this file logs with sLog.outString/outError, as the legacy code does.
#include "compat/botpch_forever.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include "ByteBuffer.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include "playerbot/BotActionLog.h"
#include "playerbot/BotDiagnostics.h"
#include "playerbot/BotState.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/LastSpellCastValue.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace engine
{
struct Brain
{
    Player* bot = nullptr;       // only compared and passed back to the registry after the Player is gone, never dereferenced then
    ObjectGuid guid;
    uint32 guidLow = 0;          // the bot's low guid: the action log is closed by it (the Player may be gone)
    PlayerbotAI* ai = nullptr;
    BotSession* session = nullptr;   // the bot's session (owned by BotMgr, which drops the brain before the session); its cast-result queue is on while the brain lives
    uint32 teleportAcks = 0;     // R10 (S3): how many teleports the host handed to PlayerbotAI::HandleTeleportAck (`.tb where`)
    uint32 castStartMs = 0;      // S2: world time (getMSTime) the engine's current "last spell cast" was first seen, stamped after each engine step
    uint32 castSeenId = 0;       // the "last spell cast" as of the last step (id, target, whole-second time): a change means a new cast started
    ObjectGuid castSeenTarget;
    time_t castSeenTime = 0;
    uint32 pendingMs = 0;        // R4: world time the engine has not been given yet (Tick hands it over in steps of kMinEngineStepMs)
};

namespace
{
bool g_configReady = false;

bool Usable(Brain const* brain)
{
    // PlayerbotAIAdapter::IsUsable (runtime/PlayerbotAIAdapter.cpp:79-85), without the adapter's own flag.
    return brain && brain->ai && brain->bot && brain->bot->IsInWorld() && brain->ai->GetBot() == brain->bot &&
        brain->ai->GetAiObjectContext() && brain->ai->GetCurrentEngine() &&
        PlayerbotAIStorage::Instance().GetAI(brain->bot) == brain->ai;
}

BotSession* BotSessionOf(Player* bot)
{
    WorldSession* session = bot ? bot->GetSession() : nullptr;
    return session && session->IsBot() ? dynamic_cast<BotSession*>(session) : nullptr;
}

// S2: the engine's "last spell cast" carries a whole-second clock (time(0)), so it cannot say how long ago a cast started. After each engine step the
// host looks at it: a new (id, target, time) means a cast began during this step, and the world time (milliseconds) is kept beside it.
void NoteCastStart(Brain* brain)
{
    ai::LastSpellCast const& last = brain->ai->GetAiObjectContext()->GetValue<ai::LastSpellCast&>("last spell cast")->Get();
    if (last.id == brain->castSeenId && last.target == brain->castSeenTarget && last.time == brain->castSeenTime)
        return;
    brain->castSeenId = last.id;
    brain->castSeenTarget = last.target;
    brain->castSeenTime = last.time;
    brain->castStartMs = WorldTimer::getMSTime();
}

// S2: a cast of the bot's own ended without the spell (SMSG_SPELL_FAILURE or the async SMSG_CAST_FAILED). The legacy SpellInterrupted does the
// work when the failure comes in a later wall second than the cast start; when it left the cast in place (the same second: its clock is time(0),
// PlayerbotAI.cpp:3184, left unchanged for Turtle) the host clears it and cuts the engine's wait on a millisecond clock, so a failed heal does not
// keep the bot waiting out the whole bar.
void RearmAfterCastFailure(Brain* brain, uint32 spellId)
{
    PlayerbotAI* pai = brain->ai;
    pai->SpellInterrupted(spellId);
    ai::LastSpellCast& last = pai->GetAiObjectContext()->GetValue<ai::LastSpellCast&>("last spell cast")->Get();
    if (!spellId || last.id != spellId)
        return;
    uint32 const wait = TortoiseBots::sd::FailureWaitMs(brain->castStartMs, WorldTimer::getMSTime(), pai->CalculateGlobalCooldown(spellId), sPlayerbotAIConfig.reactDelay);
    pai->NoteCastFailureWait(wait);
    last.id = 0;
    ai::botdiag::BotActionLog::Write(pai, "CAST_FAILURE_REARM", "spell=%u wait=%u", spellId, wait);
}

// One cast result, handed to the engine the way the Turtle packet handlers do (PlayerbotAIPackets.cpp:187-199, :202-217 and :554-591).
void DeliverCastResult(Brain* brain, CastResult const& result)
{
    PlayerbotAI* pai = brain->ai;
    if (result.kind == CastResult::Kind::SpellDelayed)
    {
        // SMSG_SPELL_DELAYED of the bot's own cast -> the engine's wait grows by the pushback (PlayerbotAIPackets.cpp:202-217): the cast bar
        // keeps the length the core reports, so the next action cannot start before it ends. The legacy case ignored a delay above 1000 ms.
        uint32 const extra = TortoiseBots::sd::PushbackExtraMs(int64(result.delayMs));
        ai::botdiag::BotActionLog::Write(pai, "SPELL_DELAYED", "delay=%u extra=%u%s", result.delayMs, extra, result.injected ? " injected=1" : "");
        pai->NoteSpellDelayed(extra);
        return;
    }
    if (result.kind == CastResult::Kind::SpellFailure)
    {
        // SMSG_SPELL_FAILURE of the bot's own cast -> PlayerbotAI::SpellInterrupted (PlayerbotAIPackets.cpp:187-199): the engine
        // clears "last spell cast" and shortens its wait to the remaining global cooldown (PlayerbotAI.cpp:3172-3194).
        ai::botdiag::BotActionLog::Write(pai, "SPELL_FAILURE", "spell=%u reason=%u%s", result.spellId, result.reason, result.injected ? " injected=1" : "");
        RearmAfterCastFailure(brain, result.spellId);
        return;
    }

    // SMSG_CAST_FAILED that the engine did not get as the return value of its own call: the cast bar ended badly (out of range,
    // line of sight, target dead, interrupted by movement or damage). Turtle's core sends none of these for a bot, and the Turtle
    // handler (:554-591) only logs crowd-control casts and feeds the "cast failed" trigger, which no strategy maps; both are kept:
    // the log line (shape of CC_CAST_FAILED: result, target, distance, moving) and the new CAST_FAILED_ASYNC line.
    ai::botdiag::BotActionLog::Write(pai, "CAST_FAILED_ASYNC", "spell=%u reason=%u%s", result.spellId, result.reason, result.injected ? " injected=1" : "");
    if (ai::botdiag::BotActionLog::GetHandle(pai))
    {
        SpellEntry const* info = sServerFacade.LookupSpellInfo(result.spellId);
        if (info && ai::FindNonCcTargetStrategy::IsCcSpell(info))
        {
            ai::LastSpellCast const& last = pai->GetAiObjectContext()->GetValue<ai::LastSpellCast&>("last spell cast")->Get();
            Unit* ccTarget = last.id == result.spellId ? pai->GetUnit(last.target) : nullptr;
            ai::botdiag::BotActionLog::Write(pai, "CC_CAST_FAILED", "spell=%u result=%u target=%s dist=%.1f moving=%u",
                result.spellId, result.reason, ccTarget ? ccTarget->GetGUID().ToString().c_str() : "none",
                ccTarget ? pai->GetBot()->GetDistance(ccTarget) : -1.0f, pai->GetBot()->isMoving() ? 1u : 0u);
        }
    }
    // D4: the bar ended without the cast, so the engine must not keep waiting for it. SpellInterrupted only acts when this spell is the
    // "last spell cast" (PlayerbotAI.cpp:3172-3194), which is exactly this case: the wait is cut to the rest of the global cooldown (or the
    // react delay) and the next tick may plan at once. No "cast failed" trigger is fed and nothing is whispered, as on Turtle's own handler.
    RearmAfterCastFailure(brain, result.spellId);
}

// S1-R2: the chat lines the hook (host/chat/TbChatIn.cpp) queued for this bot since its last step. Each one goes to the engine the way the
// Turtle packet case hands a whisper to it (PlayerbotAIPackets.cpp:376-385): the sender is looked up again NOW (a pointer is never kept
// across ticks), the sender must still be allowed to command this bot (a party removal or a lost binding since admission drops the
// order), and a bot that may not act this tick drops the line rather than holding it. Every drop is counted (`dropped` in `.tb state`),
// so a line that never ran is never left reading `accepted`.
void DeliverChat(Brain* brain)
{
    if (!brain->bot || !brain->ai)
        return;
    chatin::Envelope line;
    std::size_t taken = 0;
    while (taken < chatin::InboxCapacity && chatin::Pop(brain->guidLow, line))
    {
        ++taken;
        ObjectGuid senderGuid;
        senderGuid.SetRawValue(line.senderHigh, line.senderLow);
        Player* sender = ObjectAccessor::FindConnectedPlayer(senderGuid);
        if (!sender || !chatin::StillAuthorized(brain->bot, sender) || !brain->ai->AllowActivity())
        {
            chatin::Drop(brain->guidLow);
            continue;
        }
        // The owner's guild management words (guild invite/join/leave/promote/demote/remove/leader) are
        // answered by the guild host driver; every other line keeps going to the legacy chat commands.
        if (tbhost::guildjoin::HandleWhisper(brain->bot, sender, line.text))
            continue;
        brain->ai->HandleCommand(line.kind, line.text, *sender, line.lang);
    }
}

// World thread, before UpdateAI: the results the core queued since the last tick (any thread put them there).
void DeliverCastResults(Brain* brain)
{
    if (!brain->session)
        return;
    std::vector<CastResult> results;
    if (brain->session->DrainCastResults(results) == 0)
        return;
    for (CastResult const& result : results)
        DeliverCastResult(brain, result);
}

void Dispose(Brain* brain)
{
    onyxia::Forget(brain->guid);
    // The session's queue is only filled while the brain lives (no consumer afterwards).
    if (brain->session)
        brain->session->SetCastResultSink(false);
    // PlayerbotAIAdapter::Shutdown (:149-158): RemoveAI first (hooks resolve the AI through the registry, so none can
    // reach it afterwards), then the action log, then the AI itself.
    // Piece c-store: the saved values go to characters.ai_playerbot_db_store before the AI is gone (by guidLow: the
    // Player may be mid-logout, so no Player is touched). A brain that never entered the world saved nothing anyway.
    if (brain->ai && brain->guidLow)
        ai::slice::aistore::SaveValues(brain->ai, brain->guidLow, std::string());
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

TickBudgetConfig ReadTickBudgetConfig()
{
    TickBudgetConfig config;
    config.poolUs = sPlayerbotAIConfig.poolTickBudgetUs;
    config.combatUs = sPlayerbotAIConfig.combatTickBudgetUs;
    config.targetMs = sPlayerbotAIConfig.targetWorldTickMs;
    config.gateMs = sPlayerbotAIConfig.poolBudgetWhenTickOverMs;
    return config;
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
    //
    // Only a success is remembered (R9): a missing conf file or a failed Initialize is tried again on the next call (the next
    // brain, `.tb engine on`, a config reload), so fixing the file needs no restart. A failure never reaches the engine's half
    // state: Initialize itself runs again from the top.
    if (g_configReady)
        return std::string();
    std::string result;

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
    g_configReady = result.empty();
    return result;
}

bool ConfigReady()
{
    return g_configReady;
}

float BotXpRate()
{
    return g_configReady ? sPlayerbotAIConfig.playerbotsXPrate : 1.0f;
}

uint32 DuelMinimumLevel()
{
    // The member carries no in-class default; before the config is read the key's own default applies.
    return g_configReady ? sPlayerbotAIConfig.botAcceptDuelMinimumLevel : 10;
}

std::string ServerBotsJson()
{
    if (!g_configReady)
        return {};
    auto flag = [](bool on) -> char const* { return on ? "1" : "0"; };
    std::ostringstream ss;
    ss << "\"group_nearby\":\"" << flag(sPlayerbotAIConfig.randomBotGroupNearby) << "\""
       << ",\"raid_nearby\":\"" << flag(sPlayerbotAIConfig.randomBotRaidNearby) << "\""
       << ",\"invite_player\":\"" << flag(sPlayerbotAIConfig.randomBotInvitePlayer) << "\""
       << ",\"timed_logout\":\"" << flag(sPlayerbotAIConfig.randomBotTimedLogout) << "\""
       << ",\"level_ladder\":\"" << flag(sPlayerbotAIConfig.levelLadder) << "\""
       << ",\"auto_do_quests\":\"" << flag(sPlayerbotAIConfig.autoDoQuests) << "\""
       << ",\"disable_activity\":\"" << flag(sPlayerbotAIConfig.disableActivityPriorities) << "\""
       << ",\"active_alone\":" << sPlayerbotAIConfig.botActiveAlone
       << ",\"force_active_near\":\"" << flag(sPlayerbotAIConfig.forceActiveWhenNearPlayer) << "\""
       << ",\"limit_combat\":\"" << flag(sPlayerbotAIConfig.limitCombatActivity) << "\""
       << ",\"pool_budget_us\":" << sPlayerbotAIConfig.poolTickBudgetUs
       << ",\"pool_budget_gate_ms\":" << sPlayerbotAIConfig.poolBudgetWhenTickOverMs
       << ",\"ah_buyer\":\"" << flag(sPlayerbotAIConfig.ahMarketBuyer) << "\""
       << ",\"lft\":\"" << flag(sPlayerbotAIConfig.randomBotLftEnabled) << "\""
       << ",\"bg\":\"" << flag(sPlayerbotAIConfig.randomBotBgEnabled) << "\""
       << ",\"avoid_towns\":\"" << flag(sPlayerbotAIConfig.avoidHostileTowns) << "\""
       << ",\"leave_zones\":\"" << flag(sPlayerbotAIConfig.leaveOutgrownZones) << "\""
       << ",\"bot_loot_uncommon\":" << sPlayerbotAIConfig.botLootRateUncommon
       << ",\"bot_loot_rare\":" << sPlayerbotAIConfig.botLootRateRare
       << ",\"ah_market\":\"" << flag(sPlayerbotAIConfig.ahMarketEnabled) << "\""
       << ",\"auto_learn_trainer_spells\":\"" << flag(sPlayerbotAIConfig.autoLearnTrainerSpells) << "\""
       << ",\"auto_learn_quest_spells\":\"" << flag(sPlayerbotAIConfig.autoLearnQuestSpells) << "\""
       << ",\"auto_learn_quest_spells_alts\":\"" << flag(sPlayerbotAIConfig.autoLearnQuestSpellsAlts) << "\""
       << ",\"level_up_mounts\":\"" << flag(sPlayerbotAIConfig.levelUpMounts) << "\""
       << ",\"turtle_mount_at_level\":" << sPlayerbotAIConfig.turtleMountAtLevel
       << ",\"remote_commander\":\"" << flag(sPlayerbotAIConfig.remoteCommander) << "\""
       << ",\"camps_enabled\":\"" << flag(sPlayerbotAIConfig.campEnabled) << "\"";
    return ss.str();
}

std::string ServerDiagnosticsJson()
{
    if (!g_configReady)
        return {};
    auto flag = [](bool on) -> char const* { return on ? "1" : "0"; };
    std::ostringstream ss;
    ss << "\"perf_mon\":\"" << flag(sPlayerbotAIConfig.perfMonEnabled) << "\""
       << ",\"bot_events\":\"" << flag(sPlayerbotAIConfig.hasLog("bot_events.csv")) << "\""
       << ",\"unreachable\":\"" << flag(sPlayerbotAIConfig.hasLog("unreachable_targets.csv")) << "\""
       << ",\"deaths\":\"" << flag(sPlayerbotAIConfig.hasLog("deaths.csv")) << "\"";
    return ss.str();
}

Brain* Create(Player* bot, Player* master, std::string& error, bool free)
{
    if (!bot || !bot->IsInWorld())
    {
        error = "the bot is not in the world";
        return nullptr;
    }
    if (!master && !free)
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
        auto const started = std::chrono::steady_clock::now();
        auto const lap = [&started]() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(); };
        pai = new PlayerbotAI(bot);
        auto const constructedMs = lap();
        onyxia::InstallGround(pai);
        pai->SetMaster(master);
        // The crowd-control permissions the commander ordered (or the fear-denied default for a fresh bot): the cast
        // check reads the mask, so push the owner's setting before the first tick runs.
        TortoiseBots::h8::CcSyncBot(bot);
        // The engines were built before the master was known, so saved strategies and values are applied now (:41-44).
        pai->ResetStrategies();
        auto const resetMs = lap();
        // The movement mode. The adapter re-applies the default (:45-49): "follow" for a master with a network session,
        // "wander" otherwise (GetDefaultMovementStrategy). A host master is whoever the console or the hire flow named; it is
        // a bot session when no client drives it, and the default would then pick "wander" (a free-roaming mode the slice does
        // not register). The host bound this master on purpose, so the bot follows it: the same strategy change the chat
        // order "follow" makes (SetMovementStrategy("follow")).
        // W1: a free bot is masterless; it owns the ground it stands on. "free" is the engine's own movement marker (not "wander",
        // which this slice does not register, and not "follow"): it drops passive and leaves the grind kit's movement alone.
        if (!pai->HasActiveMovementStrategy())
            pai->SetMovementStrategy(free && !master ? "free" : "follow");
        // How long the world thread was held (the `.tb perf` worst tick of an engine start): the constructor builds the context and the four engines,
        // ResetStrategies applies the default strategies.
        sLog.outString("TortoiseBots engine: brain of %s built in %lld ms (PlayerbotAI constructor %lld, strategies %lld, movement %lld)", bot->GetName().c_str(),
            static_cast<long long>(lap()), static_cast<long long>(constructedMs), static_cast<long long>(resetMs - constructedMs), static_cast<long long>(lap() - resetMs));
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
    brain->guid = bot->GetGUID();
    brain->guidLow = bot->GetGUID().GetCounter();
    brain->ai = pai;
    brain->session = BotSessionOf(bot);

    ai::botdiag::BotActionLog::NoteLogin(brain->guidLow);
    PlayerbotAIStorage::Instance().SetAI(bot, pai);
    tbhost::worldpvp::ApplyGrantedRole(bot);

    if (!Usable(brain.get()))
    {
        Dispose(brain.release());
        restorePlayer();
        error = "the engine is not usable after creation (no current engine or registry mismatch)";
        return nullptr;
    }

    // From here on the core's cast failures for this bot are queued (TbBotSession.cpp SendPacket) and drained by Tick.
    if (brain->session)
        brain->session->SetCastResultSink(true);

    sLog.outString("TortoiseBots engine: PlayerbotAI attached for %s (%s) master %s", bot->GetName().c_str(),
        bot->GetGUID().ToString().c_str(), master ? master->GetName().c_str() : "none (free)");
    return brain.release();
}

// R4 (slice 2): the engine is stepped at most once per kMinEngineStepMs of world time, with the time collected since its last step. Why: the
// engine was written for a world loop that runs every ~50 ms (Turtle); ForeverCore's runs every millisecond (`.tb perf`: ~870 updates a
// second on an idle server). Legacy code that answers "done" without setting a next-check delay (ReachTargetAction -> ChaseTo, "already
// chasing this target": MovementActions.cpp:3622-3628) then ran the whole engine tick every millisecond for as long as the walk lasted (seen
// live: a healer walking to a patient 35 yd away ticked 1,000 times a second for 4.6 s). A step of 50 ms is the cadence the engine had on Turtle;
// the engine keeps its own delays on top (PlayerbotAIBase.cpp:16-31) and receives the real elapsed time, so nothing it counts in
// milliseconds changes. Cast results are delivered in the same step.
constexpr uint32 kMinEngineStepMs = 50;

void Tick(Brain* brain, uint32 diff)
{
    // World thread, from BotMgr::Update (WorldScript::OnBeforeMapsUpdate): the maps are not updating at this moment.
    if (!Usable(brain))
        return;
    brain->pendingMs += diff;
    if (brain->pendingMs < kMinEngineStepMs)
        return;
    diff = brain->pendingMs;
    brain->pendingMs = 0;
    // A rebind (`.tb master`) or an order for a class that joined later lands here: the store's version is 0 until the
    // first permission order, so bots that never had one keep their creation mask for one integer compare per tick.
    if (TortoiseBots::h8::CcDeniedVersion() != 0)
        TortoiseBots::h8::CcSyncBot(brain->bot);
    try
    {
        // A CAST_FAILED the core sends during this call is the refusal of the engine's own cast (the call returns the same
        // result): the session counts it and does not queue it (BotSession::EngineTickScope).
        BotSession::EngineTickScope tickScope(brain->session);
        DeliverCastResults(brain);
        DeliverChat(brain);                                        // S1-R2: the chat lines the hook queued since the last step
        TortoiseBots::h3::BeforeEngineTick(brain->bot, brain->ai);   // H3: the bot's queued orders, before its tick
        tbhost::taxi::TickLandingWatch(brain->bot);
        tbhost::ride::TickRide(brain->bot, diff);
        onyxia::Tick(brain->bot, brain->ai, diff);
        // #215: during a ride (waiting at the dock, stepping onto the deck, aboard) the ride alone moves the bot: the
        // engine's follow walked it off the dock and out of the deck in world space. A fight still gets the engine.
        if (!tbhost::ride::HoldsMovement(brain->bot) || brain->bot->IsInCombat())
            brain->ai->UpdateAI(diff);
        NoteCastStart(brain);
    }
    catch (ByteBufferException const&)
    {
        // Packet actions parse opcode-specific payloads; a bad one must drop this tick, not unwind through the world
        // update (the Turtle adapter does the same, runtime/PlayerbotAIAdapter.cpp:68-76).
        sLog.outError("TortoiseBots engine: dropped a malformed packet while updating bot %s", brain->bot->GetName().c_str());
    }
}

void NoteThrown(Brain* brain, uint32 jumpUntilMs, bool start)
{
    if (!Usable(brain))
        return;
    PlayerbotAI* pai = brain->ai;
    if (start)
    {
        // The Turtle knockback packet handler's first two steps (PlayerbotAIPackets.cpp:475-478): the cast bar ends, and a landing
        // target from an older jump must not pull the bot back to it when the flight ends.
        pai->InterruptSpell(false, "knockback");
        pai->ResetJumpDestination();
    }
    // A re-timed flight may extend the wait; a later report never shortens it. Zero means no jump time is set, and a
    // plain stamp compare would wrap (non-advancing for 24.8 days of uptime): kb::RaisesDeadline answers both.
    if (kb::RaisesDeadline(pai->GetJumpTime(), jumpUntilMs))
        pai->SetJumpTime(jumpUntilMs);
}

bool InFlight(Brain* brain)
{
    return Usable(brain) && brain->ai->IsJumping();
}

void ReapplyStrategies(Brain* brain, bool free)
{
    if (!brain || !brain->ai)
        return;
    brain->ai->ResetStrategies();
    brain->ai->SetMovementStrategy(free ? "free" : "follow");
    tbhost::worldpvp::ApplyGrantedRole(brain->bot);
}

void SetQuestGroupMember(Brain* brain, bool inGroup)
{
    if (!brain || !brain->ai)
        return;
    if (inGroup)
    {
        brain->ai->ChangeStrategy("+follow,+dps assist", BotState::BOT_STATE_NON_COMBAT);
        brain->ai->SetMovementStrategy("follow");
    }
    else
    {
        ReapplyStrategies(brain, true);
    }
}

void BindMaster(Brain* brain, Player* master)
{
    if (!brain || !brain->ai)
        return;
    brain->ai->SetMaster(master);
    // The engine's own adoption path ("remove bot masters not in our group", PlayerbotAI.cpp): a full reset purges the travel and grind
    // targets and stops the path the free kit had started, so the bot is the commanded one from this tick on, not the one it was.
    brain->ai->Reset(true);
    if (!brain->ai->IsJumping())     // a flight in the air is not cut by a rebind: it lands where it was thrown
        brain->ai->StopMoving();
    ReapplyStrategies(brain, false);
}

void HandleTeleportAck(Brain* brain)
{
    // The host session hands over every teleport it acknowledges for the bot (TbBotSession.cpp AcknowledgeTeleports). World thread.
    if (!Usable(brain))
        return;
    try
    {
        brain->ai->HandleTeleportAck();
        ++brain->teleportAcks;
    }
    catch (ByteBufferException const&)
    {
        sLog.outError("TortoiseBots engine: dropped a malformed packet in the teleport acknowledgement of bot %s", brain->bot->GetName().c_str());
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

std::string InjectCastResult(Player* bot, uint32 spellId, uint32 reason, bool interrupted)
{
    BotSession* session = BotSessionOf(bot);
    if (!session)
        return "not a bot session";
    return session->InjectCastResult(spellId, reason, interrupted);
}

std::string InjectSpellDelay(Player* bot, uint32 delayMs)
{
    BotSession* session = BotSessionOf(bot);
    if (!session)
        return "not a bot session";
    return session->InjectSpellDelay(delayMs);
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
    lines.push_back("teleport acks handled by the engine: " + std::to_string(brain->teleportAcks) + ", transition generation: " + std::to_string(pai->GetTransitionGeneration()));
    lines.push_back("combat strategies: " + JoinStrategies(pai, BotState::BOT_STATE_COMBAT));
    lines.push_back("non-combat strategies: " + JoinStrategies(pai, BotState::BOT_STATE_NON_COMBAT));

    // The engine's own "current target" value (AI_VALUE(Unit*, "current target"), e.g. AttackAction.cpp:313); the
    // value resolves its unit by guid, so a despawned target reads as none.
    Unit* target = nullptr;
    if (ai::Value<Unit*>* value = pai->GetAiObjectContext()->GetValue<Unit*>("current target"))
        target = value->Get();
    lines.push_back(std::string("target: ") + (target ? target->GetName() : "none"));
    {
        // Rage (the spell kit's currency): the engine's own "rage" value (what the heroic strike trigger reads, 40 or more) and the core's power.
        uint32 engineRage = 0;
        if (ai::Value<uint8>* value = pai->GetAiObjectContext()->GetValue<uint8>("rage", "self target"))
            engineRage = value->Get();
        lines.push_back("rage: engine value " + std::to_string(engineRage) + ", core power " +
            std::to_string(pai->GetBot()->GetPower(POWER_RAGE)) + "/" + std::to_string(pai->GetBot()->GetMaxPower(POWER_RAGE)));
    }
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

    // R4 (observations f and j): the engine's "last spell cast" (cleared by SpellInterrupted when the core reports a failed cast, so a
    // bot that heard it shows id 0 here) and the core's stand state of the bot (a drinking bot sits: 1 = sit, 0 = stand).
    if (ai::Value<ai::LastSpellCast&>* value = context->GetValue<ai::LastSpellCast&>("last spell cast"))
    {
        ai::LastSpellCast const& last = value->Get();
        lines.push_back("last spell cast: id " + std::to_string(last.id) + ", seconds since " + (last.id ? std::to_string(long(time(nullptr) - last.time)) : std::string("n/a")));
    }
    lines.push_back("stand state: " + std::to_string(int(pai->GetBot()->GetStandState())));

    // The cast-result queue (TbBotSession.h): what the core told the bot about its own casts since the engine started.
    if (brain->session)
    {
        CastResultCounts const counts = brain->session->GetCastResultCounts();
        lines.push_back("cast results: queued " + std::to_string(counts.queued) + ", delivered " + std::to_string(counts.delivered) +
            ", dropped " + std::to_string(counts.dropped) + " (queue holds " + std::to_string(BotSession::CastResultCapacity) + "), pending " +
            std::to_string(counts.pending) + ", delayed " + std::to_string(counts.delayed) +
            "; refused during the engine's own tick (not queued) " + std::to_string(counts.refusedInTick) +
            ", unreadable " + std::to_string(counts.malformed));
    }
    return lines;
}

bool RunAction(Player* bot, char const* action, char const* param)
{
    if (!bot || !action)
        return false;
    PlayerbotAI* pai = PlayerbotAIStorage::Instance().GetAI(bot->GetGUID());
    if (!pai)
        return false;
    return pai->DoSpecificAction(action, ai::Event("host", param ? std::string(param) : std::string(), bot), true);
}

bool ReadTelemetry(Player* bot, Telemetry& out, bool withStrategies)
{
    out = Telemetry{};
    if (!bot || !bot->IsInWorld())
        return false;
    PlayerbotAI* pai = PlayerbotAIStorage::Instance().GetAI(bot->GetGUID());
    if (!pai || pai->GetBot() != bot || !pai->GetAiObjectContext())
        return false;

    out.engine = true;
    out.combatState = pai->GetState() == BotState::BOT_STATE_COMBAT;
    out.deadState = pai->GetState() == BotState::BOT_STATE_DEAD;
    if (Action const* last = pai->GetLastExecutedAction(pai->GetState()))
        out.lastAction = const_cast<Action*>(last)->getName();
    out.lastTrigger = pai->GetLastExecutedTrigger(pai->GetState());
    if (withStrategies)
    {
        std::string joined;
        for (std::string_view strategy : pai->GetStrategies(BotState::BOT_STATE_COMBAT))
        {
            if (!joined.empty())
                joined += ", ";
            joined += strategy;
        }
        out.combatStrategies = std::move(joined);
    }

    AiObjectContext* context = pai->GetAiObjectContext();
    if (ai::Value<ai::TravelTarget*>* value = context->GetValue<ai::TravelTarget*>("travel target"))
        if (ai::TravelTarget* target = value->Get(); target && target->IsActive())
            if (ai::TravelDestination* destination = target->GetDestination())
            {
                std::string const purpose = destination->GetShortName();
                if (!purpose.empty() && purpose != "unknown" && purpose != "idle" && purpose != "none")
                {
                    // Same status names and order as TravelStatus (TravelMgr.h).
                    static char const* const statusNames[] = { "none", "prepare", "ready", "travel", "work", "cooldown", "expired" };
                    uint8 const status = uint8(target->GetStatus());
                    out.travelPurpose = purpose;
                    out.travelTo = destination->GetTitle();
                    out.travelStatus = status < 7 ? statusNames[status] : "unknown";
                    out.travelDist = int32(target->Distance(bot));
                }
            }

    if (ai::Value<Unit*>* value = context->GetValue<Unit*>("current target"))
        if (Unit* target = value->Get())
        {
            out.currentTarget = target->GetGUID();
            out.currentTargetName = target->GetName();
            out.targetHostile = bot->IsHostileTo(target);
            out.targetDistance = bot->GetDistance(target);
            // A line-of-sight check walks the VMap; only the unreachable judgement needs it, and only in combat.
            if (out.combatState && out.targetHostile)
                out.targetInLos = bot->IsWithinLOSInMap(target);
        }
    return true;
}

void DumpActionCounts()
{
    ai::botdiag::DumpActionCounts();
}
}
}

#else   // not built with the slice

namespace TortoiseBots
{
namespace engine
{
bool Built() { return false; }
TickBudgetConfig ReadTickBudgetConfig() { return TickBudgetConfig(); }
std::string EnsureConfig() { return "the engine is not built in (TORTOISEBOTS_SLICE1 is off)"; }
Brain* Create(Player*, Player*, std::string& error, bool /*free*/)
{
    error = "the engine is not built in (TORTOISEBOTS_SLICE1 is off)";
    return nullptr;
}
void ReapplyStrategies(Brain*, bool) {}
void SetQuestGroupMember(Brain*, bool) {}
void BindMaster(Brain*, Player*) {}
void Tick(Brain*, uint32) {}
void HandleTeleportAck(Brain*) {}
void Destroy(Brain*) {}
void NoteThrown(Brain*, uint32, bool) {}
bool InFlight(Brain*) { return false; }
std::string InjectCastResult(Player*, uint32, uint32, bool) { return "this build has no engine (TORTOISEBOTS_SLICE1 is off)"; }
std::string InjectSpellDelay(Player*, uint32) { return "this build has no engine (TORTOISEBOTS_SLICE1 is off)"; }
std::vector<std::string> Describe(Brain const*) { return { "engine: not built with the slice" }; }
bool ReadTelemetry(Player*, Telemetry& out, bool /*withStrategies*/) { out = Telemetry{}; return false; }
bool ConfigReady() { return false; }
std::string ServerBotsJson() { return {}; }
std::string ServerDiagnosticsJson() { return {}; }
float BotXpRate() { return 1.0f; }
uint32 DuelMinimumLevel() { return 10; }
bool RunAction(Player*, char const*, char const*) { return false; }
void DumpActionCounts() {}
}
}

#endif
