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
#include "ObjectGuid.h"
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

// The per-tick bot-update budget the host applies to the pool, in the engine's
// own keys so the operator keeps TB's names and file: AiPlayerbot.PoolTickBudgetUs,
// CombatTickBudgetUs, TargetWorldTickMs and PoolBudgetWhenTickOverMs. The values
// are the engine config's after its first load; a build without the engine (or
// before that load) answers the TB defaults.
struct TickBudgetConfig
{
    uint32 poolUs = 22000;
    uint32 combatUs = 15000;
    uint32 targetMs = 100;
    uint32 gateMs = 0;
};
TickBudgetConfig ReadTickBudgetConfig();

// Loads the engine's own configuration (PlayerbotAIConfig) once, before the first brain (WorldScript::OnWorldInitialized
// calls it when the engine is built in and the module is enabled, so the first bot does not pay for it). Safe to call again:
// a success is remembered and later calls return at once; a failure is NOT remembered, the next call tries again (the next
// brain, `.tb engine on`, a config reload). Empty string = ready, else why the engine cannot run.
std::string EnsureConfig();

// Builds the brain for a bot that is in the world and placed next to `master`: PlayerbotAI, master, default strategies,
// registration where the legacy code looks a bot's AI up. Null (with `error` set) when it could not be built; the
// caller then keeps the spike behaviour for that bot.
Brain* Create(Player* bot, Player* master, std::string& error, bool free = false);

// W1: `.tb free <bot> on|off` on a live brain: rebuild the strategy kit for the new mode and set the movement mode ("free" for a
// free bot, "follow" otherwise). No-op without a brain.
void ReapplyStrategies(Brain* brain, bool free);

// Enable or restore free-bot non-combat group follow/assist when grouped for a group/elite quest.
void SetQuestGroupMember(Brain* brain, bool inGroup);

// W1 (review): a master was bound to a live brain (the host ends free mode first). The engine takes the master pointer, then the full
// reset the adoption path does (travel and grind targets purged, the current path stopped) and the commanded kit. No-op without a brain.
void BindMaster(Brain* brain, Player* master);

// One tick of the engine. `diff` is the world tick's elapsed milliseconds. Before the engine's UpdateAI the bot's queued cast
// results (BotSession, TbBotSession.h) are handed to the engine: SPELL_FAILURE and an asynchronous CAST_FAILED -> PlayerbotAI::SpellInterrupted
// (the wait for the bar is cut), SPELL_DELAYED -> the wait grows by the pushback (PlayerbotAI::NoteSpellDelayed), CAST_FAILED -> the action
// log's CAST_FAILED_ASYNC line. The queue is filled only while the bot has a brain (SLICE2-R0-NOTES.md, S2-NOTES.md).
void Tick(Brain* brain, uint32 diff);

// R10 (S3): the bot's teleport is being acknowledged by the host session: PlayerbotAI::HandleTeleportAck (transition generation, teleport grace, stale
// queues, the post-teleport landing logic), which on the host replaces the engine-driven ack of the Turtle build (BotManager.cpp:2404). A same-map
// teleport: called before the session acknowledges it (the engine does it itself); a far one: after, with the Player in the new world.
void HandleTeleportAck(Brain* brain);

// Unregisters and deletes the brain. Must run before the bot's Player is deleted (see the logout paths in BotMgr).
void Destroy(Brain* brain);

// S3: a core-started flight (a knockback or another EFFECT_MOTION) began (`start`) or is still going. On a start the engine drops
// its cast and its old jump target; both calls raise (never lower) the bot's jump time to `jumpUntilMs` (WorldTimer ms), so
// PlayerbotAI::IsJumping holds its own StopMoving and its casts off the flight. No-op without a brain.
void NoteThrown(Brain* brain, uint32 jumpUntilMs, bool start);

// S3: a core-started flight is still in the air, so the engine holds its own moves and casts off it. The host's stops
// are held the same way (Unit::StopMoving and MotionMaster::Clear cut the spline and leave the bot hovering). False
// without a brain.
bool InFlight(Brain* brain);

// `.tb castfail <bot> <spell> <reason> [interrupt]`: sends the bot's session a real SMSG_CAST_FAILED (or, with `interrupt`, an
// SMSG_SPELL_FAILURE of its own cast) through Player::SendDirectMessage, so it travels the path of a core failure: the session's
// packet reader, the bounded queue, and the drain at the start of the next engine tick. Empty string = sent, else why not.
std::string InjectCastResult(Player* bot, uint32 spellId, uint32 reason, bool interrupted);

// `.tb pushback <bot> <ms>`: sends the bot's session a real SMSG_SPELL_DELAYED of its own cast through Player::SendDirectMessage, the packet
// Spell::Delayed sends to the set. The session reads it, the bounded queue holds it, and the next engine tick lengthens the wait. Empty
// string = sent, else why not.
std::string InjectSpellDelay(Player* bot, uint32 delayMs);

// What `.tb where` prints for an engine bot: one line each for state, strategies and current target, and the cast-result counts.
std::vector<std::string> Describe(Brain const* brain);

// The engine's live view of one bot for the dashboard feed (world thread, member reads only): its state, the
// last executed action and its trigger, the travel destination it is working, and the combat engine's current target.
// False when the bot has no usable brain; the struct is left default in that case.
struct Telemetry
{
    bool engine = false;
    bool combatState = false;       // the engine is in BOT_STATE_COMBAT
    bool deadState = false;         // the engine is in BOT_STATE_DEAD
    std::string lastAction;
    std::string lastTrigger;
    std::string combatStrategies;
    std::string travelPurpose;      // destination short name ("grind", "vendor", ...)
    std::string travelTo;           // destination title
    std::string travelStatus;       // none, prepare, ready, travel, work, cooldown, expired
    int32 travelDist = -1;          // yards to the destination point (-1 = no active destination)
    ObjectGuid currentTarget;       // the engine's "current target" value (empty = none)
    std::string currentTargetName;
    bool targetHostile = false;
    float targetDistance = 0.0f;
    bool targetInLos = true;
};

bool ReadTelemetry(Player* bot, Telemetry& out, bool withStrategies = false);

// True once the engine's own config file has been read (EnsureConfig succeeded). The Server panel only reports live
// getters, so a build or a failed config load reports no engine settings.
bool ConfigReady();

// The engine settings for the Server panel, as JSON object members without braces. Empty when the engine config is
// not loaded. `ServerBotsJson` feeds SERVER_INFO's `bots` object, `ServerDiagnosticsJson` its `diagnostics` object.
std::string ServerBotsJson();
std::string ServerDiagnosticsJson();

// The engine's XP multiplier for bots (rates.bot_xp_mult). 1.0 before the engine config is loaded.
float BotXpRate();

// Executes a legacy action by key for `bot` (used by host world-life errands).
bool RunAction(Player* bot, char const* action, char const* param);

// The periodic action-count snapshot (AiPlayerbot.ActionCountsLog): the engine counts every executed action and
// this writes the cumulative action_counts.csv rows. No-op without the engine or with the key off.
void DumpActionCounts();

struct BrainDeleter
{
    void operator()(Brain* brain) const { Destroy(brain); }
};
}
}
