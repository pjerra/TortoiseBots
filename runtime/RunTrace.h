#pragma once

// Run trace (opt-in): records what a party of bots did during one run as a
// bt-trace v1 JSONL file plus a <run_id>.summary.json, for offline scoring by
// tools/trace_score.py. Schema: docs/guides/run-trace.md. Records and decisions:
// runtime/RunTraceRecords.h; file writer: runtime/RunTraceWriter.h.
//
// Off by default (AiPlayerbot.RunTrace.Enabled = 0). Started and stopped by
// `.bot trace start <run_id> [party|bot <Name>]` / `.bot trace stop` (a GM, or
// the owner of every bot in scope); one run at a time. While no run is active
// the only costs are one relaxed atomic load per world tick, and one empty
// listener-list walk per action in Engine::DoNextAction.
//
// Threading: the whole recorder runs on the world thread (the bots' AI, the
// command queue and Update() all do; see docs/concepts/architecture-invariants.md).
// Only the file write is off it: lines go through a bounded queue to the writer
// thread, and are dropped (counted) when it is full. Nothing here reads the
// database or scans the world; hostiles come from the bots' own attacker lists
// and are resolved by guid on the bot's map.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RunTraceRecords.h"
#include "RunTraceWriter.h"
#include "WorldThreadQueue.h"

class Player;
class PlayerbotAI;
class Unit;

namespace TortoiseBots {

class RunTraceService
{
public:
    static RunTraceService& Instance();

    // Cheap check for hook sites.
    bool Active() const { return active_.load(std::memory_order_relaxed); }

    struct StartParams
    {
        std::string runId;
        bool wholeParty = true;     // false: only the bot `anchorGuid`
        uint64_t anchorGuid = 0;    // a member of the group to trace, or the bot
        std::string scopeName;      // shown in the header and the status
        std::string requester;      // name, for the header
        uint64_t requesterGuid = 0; // who may stop it (besides a GM)
    };

    // World thread. False with `message` filled when the run cannot start.
    bool Start(StartParams const& params, std::string& message);
    // World thread. False when no run is active.
    bool Stop(char const* reason, std::string& message);
    std::string Status() const;
    std::string const& RunId() const { return header_.runId; }
    uint64_t RequesterGuid() const { return requesterGuid_; }

    // World thread, every world tick.
    void Update(uint32_t diff);
    // Module shutdown: ends a run in progress.
    void Shutdown();

    // Hooks (world thread; callers check Active() first).
    void NoteAction(uint64_t botGuid, std::string const& name, std::string const& source,
        RunTrace::ActionOutcome outcome, float relevance);
    void NoteBotEvent(PlayerbotAI* ai, std::string const& eventName, std::string const& info1,
        std::string const& info2);

    // PlayerbotAI::SpellInterrupted: one of the bot's own casts was cancelled.
    void NoteSpellInterrupted(PlayerbotAI* ai, uint32_t spellId);

    // Attaches the action listener to the bot's engines (once per engine).
    static void AttachListeners(PlayerbotAI* ai);

private:
    RunTraceService() = default;

    struct BotTrack
    {
        bool known = false;     // party record written
        bool alive = true;
        bool inCombat = false;
        std::string strategy;   // last strategy string written
        uint64_t lastAttacker = 0;  // a hostile that targeted it in the previous frame
    };

    void Sample();
    void ResolveBots(std::vector<Player*>& bots);
    void Emit(std::string line);
    void EmitEvent(RunTrace::Event const& e);
    void EmitAction(RunTrace::ActionRecord const& rec);
    void FlushActions(bool all);
    void WriteSummary(char const* status, std::string const& endReason);
    void Finish(char const* reason, char const* status);
    std::string DirPath() const;

    std::atomic<bool> active_{false};
    // Hooks that run off the world thread are ignored: the recorder state is not locked.
    WorldThreadMark worldMark_;
    std::unique_ptr<RunTrace::Builder> builder_;
    RunTrace::RunTraceWriter writer_;
    RunTrace::Header header_;
    RunTrace::Counters counters_;
    RunTrace::ActionCoalescer coalescer_;
    RunTrace::LoopDetector loops_;
    std::string tracePath_;
    std::string summaryPath_;
    bool wholeParty_ = true;
    uint64_t anchorGuid_ = 0;
    uint64_t requesterGuid_ = 0;
    uint32_t elapsedMs_ = 0;
    uint32_t frameAccumMs_ = 0;
    uint32_t checkAccumMs_ = 0;
    uint32_t emptyScopeMs_ = 0;
    uint32_t intervalMs_ = RunTrace::kDefaultIntervalMs;
    std::unordered_map<uint64_t, BotTrack> tracks_;
    std::unordered_set<uint64_t> seenMobs_;
    std::unordered_set<uint64_t> scopeGuids_;  // read by NoteAction on the world thread only
};

} // namespace TortoiseBots
