#pragma once

// The run leader (`.bot run`, docs/guides/dungeon-run-leader.md): an opt-in
// driver that walks a bound party through a dungeon plan
// (ai/playerbot/DungeonPlanPolicy.h) by itself, behind
// AiPlayerbot.DungeonSelfClear (default 0).
//
// It gives the party ONLY the remote commander's own orders (`cmd move`,
// `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`, `ritual`), so every
// commander fix also fixes the run leader and the two cannot drift apart. The
// fights themselves are the bots' own combat AI (with the boss tactics table).
//
// This header is the whole decision: one Run per commander, advanced once a
// second by Tick() with a Facts snapshot that the world glue in
// commands/BotCommands.cpp reads from the party and the plan step's
// surroundings. Tick() answers with at most one order (the text after `.bot
// cmd `); the glue sends it as the commander and reports a refusal back with
// OrderRefused(). No game types here: tools/test_dungeon_run_policy.cpp runs it
// on its own.
//
// Safety rules, carried over from turtle-agent's remote driver (its plan
// stepper is the owner's MIT code; see docs/PROVENANCE.md):
//   - gather before a pull: every living bot within kGatherYd of the lead bot;
//   - the plan's cc units get one mage's `cc moon` before a pull (never the
//     pull target), its kill_first units are pulled first and focused in a
//     fight (a summoned healing ward too);
//   - walk in the plan's hops; before each hop, pull what stands near it, but
//     never what a later pull or boss step owns (OwnedByLaterStep);
//   - a pack or boss counts as done only when the lead bot stands within
//     kSeeYd of it and no living, hostile unit of it is left;
//   - kWipeDead dead (or all dead) regroups by itself once the fight is over:
//     revive, gather, then walk back over the plan's own move points (at most
//     kAutoRegroupsPerStep times a step, then it pauses); the lead bot dead
//     alone pauses the run;
//   - a refused gather is sent once more, then the run goes on without it;
//     at a gather's ceiling a stalled straggler (kSummonYd) is summoned once;
//   - every wait has a ceiling (gather, walk, pull, fight, rest, task, object
//     wait, and kStepIdleCapMs per step); a ceiling pauses the run with the
//     reason, it never loops;
//   - any order from a player to the party pauses the run until `resume`.

#include "../ai/playerbot/DungeonPlanPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace DungeonRun {

using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

constexpr std::uint32_t kTickMs = 1000;           // the glue ticks every run this often
constexpr std::uint32_t kWipeDead = 3;            // this many dead bots stop the step for a regroup
constexpr std::uint32_t kAutoRegroupsPerStep = 2; // regroups the run does by itself per step, then it pauses
constexpr std::uint32_t kGatherTries = 2;         // a gather refused this often: the run goes on without it
constexpr std::uint32_t kWalkBackRetries = 3;     // a walk-back point refused for a passing reason is sent again this often
constexpr float kArriveYd = 3.0f;                 // a walk is done this close to its point
constexpr float kStalledArriveYd = 6.0f;          // ... or this close once it stopped gaining ground (a point
                                                  // just off the mesh or on a ledge the bot cannot reach)
constexpr float kSeeYd = 60.0f;                   // a pack, boss or spot is judged only from this close
constexpr float kApproachYd = 30.0f;              // an unseen pack is approached to this far from its centre
constexpr float kApproachHopYd = 40.0f;           // ... in navmesh path hops of at most this length
constexpr std::uint32_t kMaxApproachHops = 8;     // ... and at most this many, then pause
constexpr std::uint32_t kMaxWalkBackHops = 40;    // a walk back straight to its last point: hops before the replay
constexpr float kDetourMaxYd = 15.0f;             // an unreachable move point may be stood in for by a
                                                  // reachable one at most this far from it
constexpr float kGatherYd = 20.0f;                // every living bot this close to the lead before a pull
constexpr float kHopPullYd = 15.0f;               // units this close to the next hop are pulled first
constexpr std::uint32_t kGatherCapMs = 30000;
constexpr float kSummonYd = 40.0f;                // at a gather's ceiling a bot this far that ...
constexpr float kStragglerDzYd = 8.0f;            // ... or this far up or down (another floor) ...
constexpr float kStragglerGainYd = 5.0f;          // ... came less than this closer since the last sample is summoned
constexpr std::uint32_t kSummonWaitMs = 6000;     // a direct step's summoned follower is waited for this long (the summon takes 3 s)
constexpr std::uint32_t kWalkRetryMs = 12000;     // no progress for this long: send the move again
constexpr std::uint32_t kWalkTries = 3;           // ... at most this often, then pause
constexpr float kWalkProgressYd = 2.0f;
constexpr std::uint32_t kPullNoFightMs = 25000;   // a pull that started no fight in this time failed
constexpr std::uint32_t kPullTriesPerUnit = 2;    // a unit two pulls could not start a fight with is passed
constexpr std::uint32_t kMaxPullsPerTarget = 3;  // a unit three fought pulls did not kill is out of reach
constexpr std::uint32_t kFruitlessPulls = 3;      // three pulls in a row that kill nothing: the rest of the pack is out of reach
constexpr std::uint32_t kMaxPullsPerStep = 20;
constexpr std::uint32_t kMaxWalkBackPulls = 8;    // pulls on the way of a walk back (their own count), then pause
constexpr std::uint32_t kRegroupReviveMs = 240000; // after a resume: the dead must be up within this
constexpr std::uint32_t kRegroupGatherMs = 60000;  // ... and the party gathered within this after that
constexpr std::uint32_t kFightCapMs = 180000;
constexpr std::uint32_t kBossFightCapMs = 420000;
constexpr std::uint32_t kRestCapMs = 60000;
constexpr std::uint32_t kRestRepeatMs = 20000;
constexpr std::uint32_t kTaskCapMs = 90000;       // loot, open, use: the commander task's own budget is shorter
constexpr std::uint32_t kStepIdleCapMs = 300000;  // out-of-combat time one step may take
constexpr std::uint32_t kBossSearchMs = 60000;    // a boss not at his spot (a patrol) is waited for this long, then pause
// Scripted events (ritual, hold; docs/guides/dungeon-run-leader.md "Events").
constexpr float kHoldReturnYd = 8.0f;             // out of a fight, a lead bot farther than this from the hold spot walks back
constexpr std::uint32_t kHoldIdlePullMs = 10000;  // an awake event unit that has not come for this long is pulled
constexpr std::uint32_t kEventStartMs = 30000;    // an encounter hold whose data does not leave "not started" in this time failed
constexpr std::uint32_t kRitualNoReplyMs = 5000;  // a ritual whose bots are free again with no answer this long: pause
constexpr std::uint32_t kEventResetWaitMs = 60000; // before a ritual: an encounter still in progress or failed (a boss
                                                   // walking home) is waited for this long, then the ritual goes out

// Rest gate: it starts when someone is below a "start" line after a fight or
// before a step, and ends when everybody is at the "end" line (or at kRestCapMs).
struct RestLines
{
    std::uint32_t startHealth = 80, startHealerMana = 50, startMana = 35;
    std::uint32_t endHealth = 90, endHealerMana = 80, endMana = 60;
};

enum class State : std::uint8_t { Running, Paused, Done };
enum class Phase : std::uint8_t { Begin, Gather, Walk, AwaitPull, AwaitTask, Rest, Wait, Hold };

inline const char* StateName(State s)
{
    switch (s)
    {
        case State::Running: return "running";
        case State::Paused: return "paused";
        default: return "done";
    }
}

inline const char* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Begin: return "begin";
        case Phase::Gather: return "gather";
        case Phase::Walk: return "walk";
        case Phase::AwaitPull: return "pull";
        case Phase::AwaitTask: return "task";
        case Phase::Rest: return "rest";
        case Phase::Hold: return "hold";
        default: return "wait";
    }
}

// A creature near the step, as the glue saw it this tick.
struct UnitFact
{
    std::uint64_t guid = 0;
    std::uint32_t entry = 0;
    float distLead = 0.0f;
    bool hostile = true;     // not friendly to the party (hostile or neutral): it may be pulled
    bool stealthed = false;
    bool evading = false;    // running home: not pulled this tick
    bool polymorphed = false; // a mage's Polymorph aura is on it (kPolymorphSpells)
    // Kill-first candidates (Facts::killFirst): in combat with a party member,
    // or summoned / owned (a healing ward) by a unit that is.
    bool engaged = false;
    std::uint64_t owner = 0;
    bool ownerEngaged = false;
};

// The mage's Polymorph (all ranks and forms): the aura the run waits for after
// a `cc moon`, and the spell a mage must know to be the run's crowd control.
constexpr std::uint32_t kPolymorphSpells[] = {118, 12824, 12825, 12826, 28270, 28271, 28272};
// In the fight a `cc moon` must show its aura within this much of the mage's
// own fight time (19z5: the mage waits for the tank's threat, walks into range
// and casts 1.5 s; Polymorphs landed 7-8 s into its fight), else once more,
// then noted.
constexpr std::uint32_t kCcLandMs = 8000;

// A living bot of the party other than the lead bot.
struct MemberFact
{
    std::string name;
    float yd = 0.0f;        // from the lead bot (1000 on another map)
    bool polymorph = false; // a mage that knows Polymorph (kPolymorphSpells): the run's crowd control
    bool inCombat = false;
    float dz = 0.0f;        // height difference to the lead bot (another floor)
};

// What the glue reads once a tick for the run's current step.
struct Facts
{
    // The party (the commander's bound bots, the lead bot included).
    std::uint32_t total = 0, alive = 0, dead = 0;
    bool leadAlive = true;
    bool anyInCombat = false;
    float leadX = 0.0f, leadY = 0.0f, leadZ = 0.0f;
    float maxMemberYd = 0.0f;           // the living bot farthest from the lead bot
    std::vector<MemberFact> members;    // the living bots, the lead bot too (at 0 yd)
    // The lead bot has a navmesh path back to the run's anchor (Run::anchor*).
    // Read lazily: only on a tick after the policy asked for it
    // (Run::anchorPathWanted, set when a gather needs the answer).
    bool anchorPathRead = false;
    bool anchorReachable = true;
    // A walk back straight to its last point: the navmesh path the glue read
    // for the next hop passes an uncleared pull or boss group
    // (PathCrossesUnclearedPack); the shortcut is dropped for the replay.
    bool directPathCrossesPack = false;
    // In a fight, when the plan has kill_first: its living, not friendly units
    // from the bots' own attacker lists (a summoned healing ward included).
    std::vector<UnitFact> killFirst;
    std::uint32_t minHealthPct = 100;   // living bots
    std::uint32_t minHealerManaPct = 100;
    std::uint32_t minManaPct = 100;     // mana users that are not healers
    // The step.
    std::uint32_t team = 0;             // the party's team: ai::kPlanFlagAlliance or kPlanFlagHorde (0 unknown)
    bool seen = false;                  // the lead bot stands within kSeeYd of the step's point
    // pull/boss not seen yet: the next approach hop, a point of the lead bot's
    // navmesh path toward the step's point (kApproachHopYd along it at most,
    // kApproachYd short of the point). approachKnown false: no path.
    bool approachKnown = false;
    float approachX = 0.0f, approachY = 0.0f, approachZ = 0.0f;
    std::vector<UnitFact> units;        // pull/boss: living units of the step's entries within its radius
    std::vector<UnitFact> hopUnits;     // move: living hostile units within kHopPullYd of the point, not owned by a later step
    // boss: a dead unit (corpse) of the boss's entry within the step's radius
    // plus kSeeYd of its point (a pullback drags him toward the party), or
    // within kSeeYd of the lead bot; bossAlive: he stands in the step's units.
    bool bossDeadNearSpot = false;
    bool bossDeadNearLead = false;
    int objectState = -1;               // wait_object, hold done by object: 0 ready, 1 active, 2 active_alt; -1 not found
    // hold (units: the living units of its `entries` within its radius, the
    // friendly, still dormant ones too):
    std::uint32_t eventDown = 0;        // dead units of its `entries` within its radius
    long long eventData = -1;           // done by instance data: the value now; -1 the map has no instance data
    std::uint32_t hostilesNear = 0;     // wait_clear: living hostiles within the radius
    std::string carrier;                // the bot that does object and item steps (the lead bot)
    bool carrierHasItem = false;        // the step's `item` is in the carrier's bags
    bool carrierHasEntryItem = false;   // use_item: the item to use is in the bags
    bool carrierBusy = false;           // a commander task (loot/open/use) runs for the carrier
    int taskReply = 0;                  // the carrier's task answer since the run's order: 0 none, 1 ACK, 2 ERR
    std::string taskText;
};

struct Run
{
    const DungeonPlan* plan = nullptr;
    std::size_t step = 0;
    State state = State::Running;
    Phase phase = Phase::Begin;
    std::string note;        // what it does now, or why it paused

    std::uint32_t phaseMs = 0;
    std::uint32_t stepIdleMs = 0;
    bool restDue = true;     // check the rest lines before the step
    bool stepRested = false; // a `rest` step has had its gate
    bool orderSent = false;  // task steps: the order went out
    bool walkedThrough = false;
    std::uint32_t approachHops = 0;
    // A move whose point the navmesh cannot reach walks to the nearest
    // reachable point near it instead (UseDetour).
    bool detour = false;
    float detourX = 0.0f, detourY = 0.0f, detourZ = 0.0f;
    bool fighting = false;
    std::uint32_t fightMs = 0;
    std::uint32_t restRepeatMs = 0;
    // The last point the run itself walked the party to (a move, an approach
    // hop, a stand point): a gather goes back there when a fight left the lead
    // bot off it (another floor, no path back), not where the lead bot stands.
    bool anchorKnown = false;
    float anchorX = 0.0f, anchorY = 0.0f, anchorZ = 0.0f;
    bool anchorPathWanted = false; // a gather needs the lead bot's path back read (next tick)
    // Pull and boss steps left without being cleared (skipped, out of reach):
    // a direct walk back keeps clear of them too.
    std::unordered_set<std::size_t> notCleared;
    // Walk.
    float walkX = 0.0f, walkY = 0.0f, walkZ = 0.0f;
    std::uint32_t walkTries = 0;
    std::uint32_t walkStillMs = 0;
    float walkBestYd = 0.0f;
    // Pulls.
    std::uint64_t pullGuid = 0;
    std::uint32_t pulls = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> pullTries;
    std::unordered_set<std::uint64_t> passed;
    std::size_t openAtLastPull = 0;
    std::uint32_t fruitlessPulls = 0;
    std::uint32_t bossWaitMs = 0;   // a boss step: time at the spot with the boss neither alive nor dead in sight
    bool bossSeenAlive = false;     // a boss step: the boss was among the step's living units
    std::uint32_t walkBackPulls = 0; // pulls on the way of the walk back (not counted in `pulls`)
    // Run-wide (kept across steps).
    std::unordered_set<std::uint64_t> unreachable;   // units given up on: never pulled again this run
    std::unordered_set<std::uint32_t> missing;       // items the run did not get (a skipped loot or use)
    std::uint32_t pendingMissing = 0;                 // the item a paused step failed to get (missing on skip)
    std::size_t skippedOpenStep = static_cast<std::size_t>(-1); // an `open` left out: the wait after it goes too
    // Regroup after a resume (revive the dead, gather), before the step.
    bool regroup = false;
    bool reviveSent = false;
    bool regroupGatherSent = false;
    std::uint32_t regroupMs = 0;
    // Gathers (a move to the lead bot's own spot): the one sent this tick, so a
    // refusal of it is known; refused kGatherTries times, it is left out.
    std::string gatherOrder;
    std::uint32_t gatherRefusals = 0;
    bool gatherWaived = false;
    std::unordered_set<std::string> summoned; // stragglers this step summoned already (once each)
    std::string summonDue; // a follower a direct step left behind (refused too-far): summoned on the next tick
    std::string summonWait; // ... the summon sent: the step waits for that bot at the lead bot (kSummonWaitMs at most)
    std::uint32_t summonWaitMs = 0;
    std::unordered_map<std::string, float> memberYd; // each bot's distance to the lead at the gather's last sample
    // Crowd control and kill-first (the plan's cc_priority / cc_always / kill_first).
    std::unordered_set<std::uint64_t> ccSent; // units this step put a `cc moon` on (once each)
    std::uint64_t focusGuid = 0;              // the kill-first unit focused in this fight
    std::uint64_t ccPending = 0;              // the `cc moon` sent, its Polymorph not seen yet
    bool ccResent = false;                    // ... sent a second time
    std::uint32_t ccWaitMs = 0;               // fight time since the mark (or its second sending)
    // Regroups the run started by itself at this step (a wipe or kWipeDead
    // dead), and the walk back over the plan's move points after one.
    std::uint32_t autoRegroups = 0;
    bool walkBackDue = false;    // walk back once the regroup is over
    bool walkBack = false;
    bool walkBackDirect = false; // straight to the last point in hops (before a replay of every point)
    std::size_t walkBackIdx = 0; // the plan step whose point is walked now
    std::uint32_t walkBackRefusals = 0; // passing refusals of that point
    // A scripted event's hold (Hold steps): its clock runs from the first look,
    // in fights too; a regroup or resume starts it over.
    bool eventOn = false;
    std::uint32_t eventResetWaitMs = 0; // a ritual waiting for its encounter to reset (kEventResetWaitMs)
    std::uint32_t eventMs = 0;
    bool eventStarted = false;   // the done data left "not started", or an event unit was seen awake
    bool holdArrived = false;    // the party stood on the hold spot once
    std::uint32_t eventIdleMs = 0; // out of a fight with an awake event unit standing
    std::uint32_t eventUp = 0, eventDown = 0, eventTotal = 0; // the event's units now: awake, dead, all seen
    // Run-wide counts for the report.
    std::uint32_t wipes = 0;     // the whole party dead, counted once each time
    bool wiped = false;
    std::uint32_t nearWipes = 0; // kWipeDead or more dead with someone alive, counted once each time
    bool nearWiped = false;
    bool eventRetry = false;     // an event failed this run: a ritual sent now is its retry
};

inline const PlanStep* Current(const Run& r)
{
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// The hold a start step leads into: the current step is a ritual, open,
// use_item or move, and the next steps up to a hold are moves only. Its event
// may already be fighting (the Stone Keepers wake with the ritual's cast), so
// such a fight is bounded as the hold's. Null for none.
inline const PlanStep* EventHoldAhead(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Ritual && s->kind != PlanStepKind::Open && s->kind != PlanStepKind::UseItem &&
            s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect))
        return nullptr;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& next = r.plan->steps[i];
        if (next.kind == PlanStepKind::Hold)
            return &next;
        if (next.kind != PlanStepKind::Move && next.kind != PlanStepKind::MoveDirect)
            return nullptr;
    }
    return nullptr;
}

// A ritual refused because its object is still active ("already in
// progress") after the event failed: the core leaves a summoning ritual active
// after its last use, so the retry cannot be started in this instance.
inline bool SpentRitual(const Run& r, const std::string& order, const std::string& answer)
{
    return r.eventRetry && order.compare(0, 7, "ritual ") == 0 && answer.compare(0, 4, "busy") == 0 &&
        answer.find("already in progress") != std::string::npos;
}

inline std::string SpentRitualNote(const std::string& what)
{
    return what + " refused: the altar is still active from the failed try, so it is spent; an instance reset is "
        "needed to retry the event (.bot run skip goes on without it)";
}

inline std::string Num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

inline std::string MoveOrder(float x, float y, float z, bool direct)
{
    return "move " + Num(x) + " " + Num(y) + " " + Num(z) + (direct ? " direct" : "");
}

inline std::string EntryArg(std::uint32_t entry, float x, float y, float z)
{
    return "entry:" + std::to_string(entry) + "@" + Num(x) + "," + Num(y) + "," + Num(z);
}

inline void EnterPhase(Run& r, Phase p)
{
    r.phase = p;
    r.phaseMs = 0;
}

inline void Pause(Run& r, const std::string& why)
{
    r.state = State::Paused;
    r.note = why;
}

// Clears what belongs to one step (also on resume, so a resumed step gets
// fresh tries and ceilings).
inline void ResetStep(Run& r)
{
    EnterPhase(r, Phase::Begin);
    r.stepIdleMs = 0;
    r.restDue = true;
    r.stepRested = false;
    r.orderSent = false;
    r.walkedThrough = false;
    r.approachHops = 0;
    r.detour = false;
    r.walkTries = 0;
    r.walkStillMs = 0;
    r.walkBestYd = 0.0f;
    r.walkX = r.walkY = r.walkZ = 0.0f;
    r.pullGuid = 0;
    r.pulls = 0;
    r.pullTries.clear();
    r.passed.clear();
    r.openAtLastPull = 0;
    r.fruitlessPulls = 0;
    r.bossWaitMs = 0;
    r.bossSeenAlive = false;
    r.walkBackPulls = 0;
    r.gatherOrder.clear();
    r.gatherRefusals = 0;
    r.gatherWaived = false;
    r.summoned.clear();
    r.summonDue.clear();
    r.summonWait.clear();
    r.summonWaitMs = 0;
    r.memberYd.clear();
    r.ccSent.clear();
    r.focusGuid = 0;
    r.ccPending = 0;
    r.ccResent = false;
    r.ccWaitMs = 0;
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    r.walkBackIdx = 0;
    r.walkBackRefusals = 0;
    r.eventOn = false;
    r.eventResetWaitMs = 0;
    r.eventMs = 0;
    r.eventStarted = false;
    r.holdArrived = false;
    r.eventIdleMs = 0;
    r.eventUp = r.eventDown = r.eventTotal = 0;
}

// "N wipe(s), M near wipe(s)", or "" for none.
inline std::string WipeCounts(const Run& r)
{
    std::string out;
    if (r.wipes)
        out = std::to_string(r.wipes) + (r.wipes == 1 ? " wipe" : " wipes");
    if (r.nearWipes)
        out += (out.empty() ? "" : ", ") + std::to_string(r.nearWipes) + (r.nearWipes == 1 ? " near wipe" : " near wipes");
    return out;
}

inline std::string DoneNote(const Run& r)
{
    std::string const counts = WipeCounts(r);
    return counts.empty() ? std::string("plan done") : "plan done (" + counts + ")";
}

// Each bot's distance to the lead bot now: the sample a gather's ceiling
// compares with (taken when the gather goes out and at each ceiling).
inline void SampleMembers(Run& r, const Facts& f)
{
    for (const MemberFact& m : f.members)
        r.memberYd[m.name] = m.yd;
}

// A gather's ceiling: the farthest straggler, a bot more than kSummonYd from
// the lead bot that came less than kStragglerGainYd closer since the last
// sample (stuck behind, not on its way), is summoned to the lead bot (`cmd
// summon`), once per bot and step, and the gather waits again. False when
// there is none to summon: the caller pauses as before.
inline bool SummonStraggler(Run& r, const Facts& f, std::string& order)
{
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        if ((m.yd <= kSummonYd && m.dz <= kStragglerDzYd) || m.name.empty() || r.summoned.count(m.name))
            continue;
        auto it = r.memberYd.find(m.name);
        if (it == r.memberYd.end() || it->second - m.yd >= kStragglerGainYd)
            continue;
        if (!pick || m.yd > pick->yd)
            pick = &m;
    }
    SampleMembers(r, f);
    if (!pick)
        return false;
    r.summoned.insert(pick->name);
    order = "summon " + pick->name;
    r.note = "gather: " + pick->name + " stands " + Num(pick->yd) + " yd from the lead bot and does not come closer: summoned";
    return true;
}

// The step's walk points: what a walk back after a regroup replays.
inline bool IsMovePoint(const PlanStep& s)
{
    return s.kind == PlanStepKind::Move || s.kind == PlanStepKind::MoveDirect;
}

// Where the lead bot stands against the run's anchor: on it (or no anchor),
// off it (more than kStragglerDzYd above or below: a fight chased it up a
// ridge; or no navmesh path back), or not known yet (away on the anchor's
// floor and the path back not read: asked for, the glue reads it next tick).
enum class AnchorState : std::uint8_t { On, Off, Unknown };

inline AnchorState CheckAnchor(Run& r, const Facts& f)
{
    if (!r.anchorKnown)
        return AnchorState::On;
    float const dz = f.leadZ > r.anchorZ ? f.leadZ - r.anchorZ : r.anchorZ - f.leadZ;
    if (dz > kStragglerDzYd)
        return AnchorState::Off;
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.anchorX, r.anchorY, r.anchorZ) <= kArriveYd)
        return AnchorState::On;
    if (!f.anchorPathRead)
    {
        r.anchorPathWanted = true;
        return AnchorState::Unknown;
    }
    return f.anchorReachable ? AnchorState::On : AnchorState::Off;
}

inline bool LeadOffAnchor(Run& r, const Facts& f)
{
    return CheckAnchor(r, f) == AnchorState::Off;
}

// Sends a gather: the lead bot holds where it stands and the others form up on
// it; a lead bot off the anchor walks back to the anchor first and the party
// gathers there (19z5 step 83: a gather on the lead's own spot pinned the party
// on a hilltop with no path down).
inline void SendGather(Run& r, const Facts& f, std::string& order)
{
    order = LeadOffAnchor(r, f) ? MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false)
                                : MoveOrder(f.leadX, f.leadY, f.leadZ, false);
    r.gatherOrder = order;
    SampleMembers(r, f);
}

// Whether `order` is the gather this tick sent (the glue tells `cmd move` it
// is the run leader's gather on the lead bot's own spot).
inline bool IsGatherOrder(const Run& r, const std::string& order)
{
    return !r.gatherOrder.empty() && order == r.gatherOrder;
}

// The step the glue reads the step facts for (seen, approach, hopUnits): the
// move point a walk back replays, else the current step.
inline const PlanStep* FactStep(const Run& r)
{
    if (r.walkBack && r.plan && r.walkBackIdx < r.step && r.step < r.plan->steps.size())
        return &r.plan->steps[r.walkBackIdx];
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// OwnedByLaterStep's `from` for the hop pulls: the current step; during a walk
// back the step before it, so a pack of a step already done (back after a
// respawn) is pulled on the way, and the current step's own pack and later
// ones are left to their steps.
inline std::size_t OwnerFrom(const Run& r)
{
    return r.walkBack && r.step > 0 ? r.step - 1 : r.step;
}

// A refused move whose point cannot be walked to (the move's own verdicts:
// no path, off the mesh; a direct step's no ground or no sight line), as
// opposed to a refusal for the lead bot's state of the moment.
inline bool IsPathRefusal(const std::string& answer)
{
    for (const char* code : {"unreachable", "off-mesh", "no-ground", "no-los"})
        if (answer.compare(0, std::char_traits<char>::length(code), code) == 0)
            return true;
    return false;
}

inline void BeginRegroup(Run& r)
{
    r.anchorKnown = false; // the party comes back to life elsewhere
    r.regroup = true;
    r.reviveSent = false;
    r.regroupGatherSent = false;
    r.regroupMs = 0;
}

inline void NextStep(Run& r, const std::string& note)
{
    r.pendingMissing = 0;
    ++r.step;
    r.autoRegroups = 0;
    ResetStep(r);
    r.note = note;
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = DoneNote(r);
    }
}

inline Run Start(const DungeonPlan* plan, std::size_t fromStep = 0)
{
    Run r;
    r.plan = plan;
    r.step = fromStep;
    ResetStep(r);
    r.note = "started";
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = "plan has no steps";
    }
    return r;
}

// `.bot run resume`: the party regroups first (the dead are revived, the
// living gather on the lead bot), then the paused step starts over with fresh
// tries; a lead bot far from it walks back on the navmesh (approach hops).
inline bool Resume(Run& r)
{
    if (r.state != State::Paused || !Current(r))
        return false;
    r.state = State::Running;
    r.fighting = false;
    r.autoRegroups = 0;
    ResetStep(r);
    BeginRegroup(r);
    r.walkBackDue = true; // a party revived at the entrance walks back the plan's way
    r.note = "resumed: regrouping";
    return true;
}

// `.bot run skip`: leaves the current step out and goes on running (from a
// pause too, with the regroup of a resume). An item the step failed to get is
// noted missing: later steps that need it are left out with a note instead
// of failing one after the other.
inline bool Skip(Run& r)
{
    const PlanStep* s = Current(r);
    if (r.state == State::Done || !s)
        return false;
    bool const wasPaused = r.state == State::Paused;
    if (r.pendingMissing)
        r.missing.insert(r.pendingMissing);
    if (s->kind == PlanStepKind::Open)
        r.skippedOpenStep = r.step;
    if (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss)
        r.notCleared.insert(r.step);
    // The step's pull state goes with the step (NextStep -> ResetStep); a
    // target refused as invalid-target is already out of reach for the run.
    std::size_t const skipped = r.step + 1;
    r.state = State::Running;
    NextStep(r, "skipped step " + std::to_string(skipped));
    if (wasPaused && r.state == State::Running)
        BeginRegroup(r);
    return true;
}

// Any order from a player to the party.
inline bool PauseForPlayer(Run& r, const std::string& order)
{
    if (r.state != State::Running)
        return false;
    Pause(r, "player order (" + order + "): .bot run resume to go on");
    return true;
}

// The glue's order was refused (ERR): pause with the module's own words. A
// refused `rest` (nobody had food or drink to use) only notes it: the rest
// gate's own ceiling ends the wait. A refused gather is sent once more, then
// left out (the pull or the step goes on with the party as it stands); a
// refused point of a walk back is passed for the next one.
// The follower a refused direct step names: `cmd move ... direct` answers
// "too-far: <bot> is not with <lead> (...)" when a bot has no way onto the
// lead bot's line. Empty for any other answer.
inline std::string DirectStragglerName(const std::string& answer)
{
    static const std::string head = "too-far: ";
    static const std::string mid = " is not with ";
    if (answer.compare(0, head.size(), head) != 0)
        return std::string();
    std::size_t const at = answer.find(mid, head.size());
    if (at == std::string::npos || at == head.size())
        return std::string();
    return answer.substr(head.size(), at - head.size());
}

inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)
{
    // The summon of a direct step's follower: `busy` (a summon of that bot is
    // pending already) keeps the wait; any other refusal ends it, so the step
    // goes out again at once (and pauses if that bot is named again).
    if (order.compare(0, 7, "summon ") == 0 && !r.summonWait.empty() && order.substr(7) == r.summonWait)
    {
        if (answer.compare(0, 4, "busy") != 0)
        {
            r.summonWait.clear();
            r.summonWaitMs = 0;
        }
        r.note = order + " refused: " + answer + (r.summonWait.empty() ? "" : ": waiting for it");
        return;
    }
    if (order == "rest" || order == "revive party" || order.compare(0, 7, "summon ") == 0 ||
        order.compare(0, 3, "cc ") == 0 || order.compare(0, 6, "focus ") == 0)
    {
        // The rest gate, the regroup and the gather have their own ceilings;
        // crowd control and a focus are help, the fight goes on without them.
        r.note = order + " refused: " + answer;
        return;
    }
    if (!r.gatherOrder.empty() && order == r.gatherOrder)
    {
        r.gatherOrder.clear();
        bool const again = ++r.gatherRefusals < kGatherTries;
        r.gatherWaived = !again;
        if (r.regroup)
            r.regroupGatherSent = false;
        else
            EnterPhase(r, Phase::Begin);
        r.note = "gather refused (" + answer + "): " + (again ? "sending it once more" : "going on without it");
        return;
    }
    const PlanStep* s = Current(r);
    // A direct step (the step's own or a walk back's) refused because a
    // follower has no way onto the lead bot's line and no straight line of its
    // own (it stands over the navmesh hole the step before crossed, or was left
    // behind): that follower is summoned to the lead bot, once per bot and
    // step, and the step goes out again.
    if (order.size() > 7 && order.compare(order.size() - 7, 7, " direct") == 0)
    {
        std::string const name = DirectStragglerName(answer);
        if (!name.empty() && !r.summoned.count(name))
        {
            r.summoned.insert(name);
            r.summonDue = name;
            EnterPhase(r, Phase::Begin);
            r.note = "direct step: " + name + " is not with the lead bot: summoning it";
            return;
        }
    }
    if (SpentRitual(r, order, answer))
    {
        Pause(r, SpentRitualNote("ritual " + std::to_string(s ? s->entry : 0)));
        return;
    }
    // A pull target the module refuses as such (invalid-target: not an enemy,
    // or the group cannot attack it; Zul'Farrak's Sul'lithuz Hatchlings 8130)
    // is left out for the run; the step goes on with what is left. Other
    // refusals of a pullback (a busy tank, no tank, the lead bot unavailable)
    // pause as before.
    if (order.compare(0, 9, "pullback ") == 0 && answer.compare(0, 14, "invalid-target") == 0)
    {
        std::uint64_t const guid = std::strtoull(order.c_str() + 9, nullptr, 10);
        r.unreachable.insert(guid);
        if (r.pullGuid == guid)
            r.pullGuid = 0;
        EnterPhase(r, Phase::Begin);
        r.note = "pull refused (" + answer + "): that unit is left out";
        return;
    }
    if (r.walkBack && r.plan && r.walkBackIdx < r.step)
    {
        const PlanStep& p = r.plan->steps[r.walkBackIdx];
        if (order == MoveOrder(p.x, p.y, p.z, p.kind == PlanStepKind::MoveDirect))
        {
            std::string const point = "walk back: the point of step " + std::to_string(r.walkBackIdx + 1);
            if (IsPathRefusal(answer))
            {
                // The point itself cannot be walked to now: the next one.
                r.note = point + " refused (" + answer + "): on to the next";
                ++r.walkBackIdx;
                r.approachHops = 0;
                r.walkBackRefusals = 0;
                r.gatherWaived = false;
                r.gatherRefusals = 0;
                EnterPhase(r, Phase::Begin);
                return;
            }
            // The lead bot cannot go right now (dead, teleporting, busy ...):
            // the same point again, a few times, then pause.
            if (++r.walkBackRefusals > kWalkBackRetries)
            {
                Pause(r, point + " refused " + std::to_string(r.walkBackRefusals) + " times (" + answer + ")");
                return;
            }
            r.note = point + " refused (" + answer + "): sending it again";
            EnterPhase(r, Phase::Begin);
            return;
        }
    }
    if (s && s->kind == PlanStepKind::UseItem && s->item && !r.missing.empty())
    {
        // A use that cannot work because an earlier item is missing (a
        // reagent): a consequence, not a second failure.
        r.missing.insert(s->item);
        NextStep(r, "use_item " + std::to_string(s->entry) + " refused (" + answer + "): an earlier item is missing; " +
            "item " + std::to_string(s->item) + " is noted missing too");
        return;
    }
    if (s && (s->kind == PlanStepKind::Loot || s->kind == PlanStepKind::UseItem) && s->item)
        r.pendingMissing = s->item;
    Pause(r, "cmd " + order + " refused: " + answer +
        (r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
            " missing: .bot run skip leaves out the steps that need it)" : ""));
}

inline bool NeedsRest(const Facts& f, const RestLines& l)
{
    return f.minHealthPct < l.startHealth || f.minHealerManaPct < l.startHealerMana || f.minManaPct < l.startMana;
}

inline bool Rested(const Facts& f, const RestLines& l)
{
    return f.minHealthPct >= l.endHealth && f.minHealerManaPct >= l.endHealerMana && f.minManaPct >= l.endMana;
}

// Walks the party to a point. True once the lead bot is there; otherwise
// `order` may hold the move to send. Pauses after kWalkTries moves without
// progress.
inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct, std::uint32_t dt,
    std::string& order)
{
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    // The anchor is where the lead bot actually stands on arrival: the point,
    // or, for a stalled arrival short of it (a point off the mesh), its own spot.
    auto arrived = [&](float ax, float ay, float az)
    {
        r.anchorKnown = true;
        r.anchorX = ax;
        r.anchorY = ay;
        r.anchorZ = az;
        return true;
    };
    if (dist <= kArriveYd)
        return arrived(x, y, z);
    bool const fresh = r.phase != Phase::Walk || r.walkX != x || r.walkY != y || r.walkZ != z;
    if (fresh)
    {
        EnterPhase(r, Phase::Walk);
        r.walkX = x;
        r.walkY = y;
        r.walkZ = z;
        r.walkTries = 1;
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        order = MoveOrder(x, y, z, direct);
        return false;
    }
    r.walkStillMs += dt;
    if (dist < r.walkBestYd - kWalkProgressYd)
    {
        r.walkBestYd = dist;
        r.walkStillMs = 0;
    }
    if (r.walkStillMs >= kWalkRetryMs && dist <= kStalledArriveYd)
        return arrived(f.leadX, f.leadY, f.leadZ);
    if (r.walkStillMs >= kWalkRetryMs)
    {
        if (r.walkTries >= kWalkTries)
        {
            Pause(r, "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z));
            return false;
        }
        ++r.walkTries;
        r.walkStillMs = 0;
        order = MoveOrder(x, y, z, direct);
    }
    return false;
}

// The unit to pull now: hostile, not evading, not passed, not stealthed;
// nearest to the lead bot. When `entry` is not 0 only that entry. Null for none.
inline const UnitFact* PickPull(const Run& r, const std::vector<UnitFact>& units, std::uint32_t entry)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : units)
    {
        if (!u.hostile || u.evading || u.stealthed || r.passed.count(u.guid) || r.unreachable.count(u.guid))
            continue;
        if (entry && u.entry != entry)
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// Units the step still has to deal with: hostile, alive (the glue lists only
// living ones), not passed. Evading ones count (they come back).
inline std::size_t OpenUnits(const Run& r, const std::vector<UnitFact>& units)
{
    std::size_t n = 0;
    for (const UnitFact& u : units)
        if (u.hostile && !r.passed.count(u.guid) && !r.unreachable.count(u.guid))
            ++n;
    return n;
}

// Whether a unit may be pulled or crowd-controlled now (PickPull's rules).
inline bool Pullable(const Run& r, const UnitFact& u)
{
    return u.hostile && !u.evading && !u.stealthed && !r.passed.count(u.guid) && !r.unreachable.count(u.guid);
}

// The party's crowd control: the first living mage that knows Polymorph, "" none.
inline std::string CcBot(const Facts& f)
{
    for (const MemberFact& m : f.members)
        if (m.polymorph && !m.name.empty())
            return m.name;
    return std::string();
}

// In the fight after a `cc moon`: the Polymorph aura must show on that unit
// within kCcLandMs. Not there: the mark once more, then noted as not landed.
// Returns true when it sent an order.
inline bool CheckCc(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.ccPending)
        return false;
    const UnitFact* held = nullptr;
    for (const UnitFact& x : f.units)
        if (x.guid == r.ccPending)
            held = &x;
    if (!held || held->polymorphed)
    {
        if (held)
            r.note = "fighting: " + std::to_string(held->entry) + " is polymorphed";
        r.ccPending = 0;
        return false;
    }
    // Counted only while the crowd control itself is in the fight.
    std::string const bot = CcBot(f);
    for (const MemberFact& m : f.members)
        if (m.name == bot && m.inCombat)
            r.ccWaitMs += dt;
    if (r.ccWaitMs < kCcLandMs)
        return false;
    if (!r.ccResent && !bot.empty())
    {
        r.ccResent = true;
        r.ccWaitMs = 0;
        r.note = "fighting: the crowd control on " + std::to_string(held->entry) + " did not land: once more";
        order = "cc moon " + std::to_string(held->guid) + " " + bot;
        return true;
    }
    r.note = "fighting: cc did not land on " + std::to_string(held->entry);
    r.ccPending = 0;
    return false;
}

// Whether a unit this step crowd-controlled is still alive among the step's
// units (one mage holds one Polymorph).
inline bool CcHeld(const Run& r, const Facts& f)
{
    for (const UnitFact& u : f.units)
        if (r.ccSent.count(u.guid))
            return true;
    return false;
}

// The pack member the run crowd-controls before a pull of `pullGuid`: the
// best-ranked of the plan's cc entries (cc_priority, then cc_always) among
// the step's units, never the pull target, not one already done this step,
// and none while an earlier one is still alive. Null without a mage in the
// party or a plan list.
inline const UnitFact* PickCc(const Run& r, const Facts& f, std::uint64_t pullGuid)
{
    if (!r.plan || CcBot(f).empty() || CcHeld(r, f))
        return nullptr;
    const UnitFact* best = nullptr;
    int bestRank = 0;
    for (const UnitFact& u : f.units)
    {
        if (u.guid == pullGuid || !Pullable(r, u) || r.ccSent.count(u.guid))
            continue;
        int const rank = r.plan->CcRank(u.entry);
        if (rank < 0)
            continue;
        if (!best || rank < bestRank || (rank == bestRank && u.distLead < best->distLead))
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best;
}

// A pack's pull target (a pull step): a kill_first unit first (best rank,
// then nearest), else the nearest; never the unit the crowd control is for,
// or a crowd-controlled one, while another can be pulled. Null for none.
inline const UnitFact* PickPackPull(const Run& r, const Facts& f)
{
    const UnitFact* cc = PickCc(r, f, 0);
    const UnitFact* best = nullptr;
    const UnitFact* held = nullptr;
    int bestRank = -1;
    for (const UnitFact& u : f.units)
    {
        if (!Pullable(r, u))
            continue;
        if ((cc && u.guid == cc->guid) || r.ccSent.count(u.guid))
        {
            if (!held || u.distLead < held->distLead)
                held = &u;
            continue;
        }
        int const rank = r.plan ? r.plan->KillFirstRank(u.entry) : -1;
        bool better = !best;
        if (best)
        {
            if (rank >= 0 && (bestRank < 0 || rank < bestRank))
                better = true;
            else if (rank == bestRank && u.distLead < best->distLead)
                better = true;
        }
        if (better)
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best ? best : held;
}

// In a fight: the kill_first unit to focus (best rank, then nearest; not a
// crowd-controlled one). Null for none.
inline const UnitFact* PickKillFirst(const Run& r, const Facts& f)
{
    if (!r.plan)
        return nullptr;
    const UnitFact* best = nullptr;
    int bestRank = 0;
    for (const UnitFact& u : f.killFirst)
    {
        if (!u.hostile || u.evading || u.stealthed || r.ccSent.count(u.guid))
            continue;
        // Only this fight's: engaged with the party, or the ward of one that is.
        if (!u.engaged && !(u.owner && u.ownerEngaged))
            continue;
        int const rank = r.plan->KillFirstRank(u.entry);
        if (rank < 0)
            continue;
        if (!best || rank < bestRank || (rank == bestRank && u.distLead < best->distLead))
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best;
}

// Gathers the party, crowd-controls the plan's cc target of the pack (one
// `cc moon <guid> <mage>`, once per unit and step), then sends `pullback <guid>`.
inline void PullUnit(Run& r, const Facts& f, const UnitFact& u, std::string& order, const UnitFact* cc = nullptr,
    bool walkBack = false)
{
    AnchorState const anchor = r.gatherWaived ? AnchorState::On : CheckAnchor(r, f);
    if (anchor == AnchorState::Unknown && r.phase != Phase::Gather)
    {
        r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
        return; // the glue reads the path next tick
    }
    if ((f.maxMemberYd > kGatherYd || anchor == AnchorState::Off) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = anchor == AnchorState::Off ? "gathering back at " + Num(r.anchorX) + " " + Num(r.anchorY) + " " +
                Num(r.anchorZ) + " before a pull (the fight left the lead bot off the way)"
                                         : std::string("gathering before a pull");
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            if (SummonStraggler(r, f, order))
                EnterPhase(r, Phase::Gather);
            else
                Pause(r, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) + " s (a bot is " +
                    Num(f.maxMemberYd) + " yd from the lead bot)");
        }
        return;
    }
    if (!walkBack && r.pulls >= kMaxPullsPerStep)
    {
        Pause(r, std::to_string(kMaxPullsPerStep) + " pulls did not clear the step");
        return;
    }
    if (walkBack && r.walkBackPulls >= kMaxWalkBackPulls)
    {
        Pause(r, "walk back: " + std::to_string(kMaxWalkBackPulls) + " pulls on the way did not clear it");
        return;
    }
    if (cc && cc->guid != u.guid && !r.ccSent.count(cc->guid))
    {
        // The mark goes out before the pull; the mage's Polymorph comes with
        // the fight (its `cc` strategy is a combat one), where the fight gate
        // checks the aura (CheckCc).
        r.ccSent.insert(cc->guid);
        r.ccPending = cc->guid;
        r.ccResent = false;
        r.ccWaitMs = 0;
        // Still getting the pull ready: the next tick's pull is not counted
        // as a second pull attempt (FightStep's fruitless count).
        EnterPhase(r, Phase::Gather);
        std::string const bot = CcBot(f);
        r.note = "crowd control on " + std::to_string(cc->entry) + " (" + bot + ") before the pull";
        order = "cc moon " + std::to_string(cc->guid) + " " + bot;
        return;
    }
    // A gather left out holds for this pull only: the next one gathers again.
    r.gatherWaived = false;
    r.gatherRefusals = 0;
    std::uint32_t& tries = r.pullTries[u.guid];
    if (tries >= kMaxPullsPerTarget)
    {
        // Each pull started a fight, yet the unit lives: it evades, leashes
        // home or stands where the party cannot reach. Never loop on it.
        r.unreachable.insert(u.guid);
        r.note = "unit " + std::to_string(u.entry) + " survived " + std::to_string(kMaxPullsPerTarget) +
            " pulls: left as out of reach";
        return;
    }
    ++tries;
    ++(walkBack ? r.walkBackPulls : r.pulls);
    r.pullGuid = u.guid;
    EnterPhase(r, Phase::AwaitPull);
    r.note = "pulling " + std::to_string(u.entry);
    order = "pullback " + std::to_string(u.guid);
}

// The step's pull is out: wait for the fight (the fight gate takes over) or
// give the pull up after kPullNoFightMs.
inline void AwaitPull(Run& r)
{
    if (r.phaseMs < kPullNoFightMs)
        return;
    auto it = r.pullTries.find(r.pullGuid);
    if (it != r.pullTries.end() && it->second >= kPullTriesPerUnit)
        r.passed.insert(r.pullGuid);
    r.pullGuid = 0;
    EnterPhase(r, Phase::Begin);
}

// Walks toward a step whose point is out of sight, one navmesh hop at a time
// (the glue's approach point). A hop is fixed when its walk starts; once there
// the next tick asks for the next one. Bounded by kMaxApproachHops.
inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order,
    const std::string& what)
{
    r.note = "approaching the " + what;
    if (r.phase == Phase::Walk)
    {
        if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
            EnterPhase(r, Phase::Begin);
        return;
    }
    if (!f.approachKnown)
    {
        Pause(r, "no walkable path toward the " + what + " at " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z));
        return;
    }
    if (++r.approachHops > kMaxApproachHops)
    {
        Pause(r, std::to_string(kMaxApproachHops) + " approach hops did not bring the party within " +
            Num(kSeeYd) + " yd of the " + what);
        return;
    }
    WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
}

// After a regroup the run started by itself the party stands where it came
// back to life (the instance entrance after a corpse run). It walks the plan's
// own move points again, in order, from the one nearest the lead bot up to the
// current step: the way the run came, with no new path search. Nothing to walk
// when the current step's point is nearer than every earlier move point.
// A point of a navmesh path (the glue's PathFinder points).
struct PathPoint
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The margin past a pull group's radius a direct walk back keeps from it.
constexpr float kPackClearanceYd = 10.0f;

inline float SegmentDistance(const PathPoint& a, const PathPoint& b, float px, float py, float pz)
{
    float const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float const len2 = dx * dx + dy * dy + dz * dz;
    float t = len2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy + (pz - a.z) * dz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return ai::PlanDistance(a.x + t * dx, a.y + t * dy, a.z + t * dz, px, py, pz);
}

// Whether a path passes within a pull or boss group's radius plus
// kPackClearanceYd of its point, for every group not cleared yet: the step
// `from` and later ones, and earlier ones left without being cleared
// (`notCleared`: skipped, out of reach), of the party's team. The whole path
// counts, its last leg too. The shortest navmesh path back
// ignores packs: a straight walk past an uncleared one body-pulls it.
inline bool PathCrossesUnclearedPack(const DungeonPlan& plan, std::size_t from, const std::vector<PathPoint>& path,
    std::uint32_t team, const std::unordered_set<std::size_t>& notCleared = {})
{
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        if (i < from && !notCleared.count(i))
            continue; // cleared before
        const PlanStep& g = plan.steps[i];
        if ((g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, team))
            continue;
        float const reach = g.radius + kPackClearanceYd;
        if (path.size() == 1 && ai::PlanDistance(path[0].x, path[0].y, path[0].z, g.x, g.y, g.z) <= reach)
            return true;
        for (std::size_t k = 1; k < path.size(); ++k)
            if (SegmentDistance(path[k - 1], path[k], g.x, g.y, g.z) <= reach)
                return true;
    }
    return false;
}

// The walk back first goes straight to the last move point before the step
// (in navmesh hops, StartWalkBack); when that fails it replays every point
// from the one nearest the lead bot (ReplayWalkBack). True when there is one.
inline bool ReplayWalkBack(Run& r, const Facts& f)
{
    r.walkBackDirect = false;
    r.walkBack = false;
    r.approachHops = 0;
    const PlanStep* cur = Current(r);
    if (!cur)
        return false;
    float best = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    bool found = false;
    for (std::size_t i = 0; i < r.step; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsMovePoint(p))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (d < best)
        {
            best = d;
            r.walkBackIdx = i;
            found = true;
        }
    }
    if (!found)
        return false;
    r.walkBack = true;
    EnterPhase(r, Phase::Begin);
    return true;
}

// Whether the plan's points from `from` up to the current step hold a direct
// step: a navmesh hole (Uldaman's scorpid pit) the navmesh hops of a straight
// walk back cannot cross; only the plan's own points cross it.
inline bool DirectStepOnWay(const Run& r, std::size_t from)
{
    for (std::size_t i = from; i < r.step && i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].kind == PlanStepKind::MoveDirect)
            return true;
    return false;
}

// The plan's move point nearest the lead bot before the current step (npos
// for none), ReplayWalkBack's start.
inline std::size_t NearestMovePoint(const Run& r, const Facts& f)
{
    std::size_t idx = static_cast<std::size_t>(-1);
    float best = 0.0f;
    for (std::size_t i = 0; i < r.step; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsMovePoint(p))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (idx == static_cast<std::size_t>(-1) || d < best)
        {
            best = d;
            idx = i;
        }
    }
    return idx;
}

// After a regroup: the way back to the current step. The party stays only when
// it stands on the last move point before the step, or within kApproachYd of
// the step's own point and nearer to it than to that move point (a straight
// line from the entrance can be shorter to the pack than to its approach stand
// yet have no path: Galgann pull 108, 2026-10-16, "no walkable path toward the
// pack" from the entrance). Otherwise it walks the plan's way: straight to the
// last move point in navmesh hops, or, with a direct step on the way, the
// plan's own points at once (the hops cannot cross the navmesh hole).
inline void StartWalkBack(Run& r, const Facts& f)
{
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    const PlanStep* cur = Current(r);
    if (!cur)
        return;
    std::size_t last = r.step;
    for (std::size_t i = 0; i < r.step; ++i)
        if (IsMovePoint(r.plan->steps[i]))
            last = i;
    if (last == r.step)
        return;
    const PlanStep& p = r.plan->steps[last];
    float const toLast = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    float const toStep = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    if (toLast <= kArriveYd || (toStep <= kApproachYd && toStep <= toLast))
        return; // back near the step already
    std::size_t const nearest = NearestMovePoint(r, f);
    if (nearest != static_cast<std::size_t>(-1) && nearest < last && DirectStepOnWay(r, nearest))
    {
        r.walkBack = true;
        r.walkBackIdx = nearest;
        r.approachHops = 0;
        r.note = "walk back: a direct step is on the way: replaying the plan's points";
        EnterPhase(r, Phase::Begin);
        return;
    }
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = last;
    r.approachHops = 0;
    EnterPhase(r, Phase::Begin);
}

// On to the next walk-back point, with fresh tries, hops and gather waiver.
inline void NextWalkBackPoint(Run& r)
{
    ++r.walkBackIdx;
    r.walkBackRefusals = 0;
    r.approachHops = 0;
    r.gatherWaived = false;
    r.gatherRefusals = 0;
    EnterPhase(r, Phase::Begin);
}

// One second of the walk back. True while it walks (the tick ends there);
// false once it is over. Each point is replayed as its move step would be:
// the hostiles within kHopPullYd of it (the glue's hopUnits, read for this
// point: FactStep) are pulled first, a direct one gathers first, then a
// WalkTo (bounded by kWalkTries). One point a tick: the facts are read for it.
inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.walkBack)
        return false;
    while (r.walkBackIdx < r.step && !IsMovePoint(r.plan->steps[r.walkBackIdx]))
        ++r.walkBackIdx;
    if (r.walkBackIdx >= r.step)
    {
        r.walkBack = false;
        r.stepIdleMs = 0;
        r.note = "walked back to step " + std::to_string(r.step + 1);
        EnterPhase(r, Phase::Begin);
        return true; // the next tick reads the facts for the step itself
    }
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return true;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0))
    {
        PullUnit(r, f, *hopUnit, order, nullptr, true);
        if (r.state == State::Running)
            r.note = "walk back: " + r.note;
        return true;
    }
    const PlanStep& p = r.plan->steps[r.walkBackIdx];
    // A point out of sight (more than kSeeYd: beyond ReactDistance the move
    // itself is refused) is approached in navmesh hops, as a resume does;
    // no path toward it at all: it is passed for the next one.
    if (!f.seen && !(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z))
    {
        std::string const what = "the point of step " + std::to_string(r.walkBackIdx + 1);
        if (r.phase == Phase::Walk)
        {
            r.note = "walk back: approaching " + what;
            if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
                EnterPhase(r, Phase::Begin);
            return true;
        }
        if (r.walkBackDirect && f.directPathCrossesPack)
        {
            // The shortest way passes a pack not cleared yet: the plan's own
            // points instead (the way the run came, its packs cleared).
            if (ReplayWalkBack(r, f))
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack: replaying the plan's points";
            else
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack";
            return true;
        }
        std::uint32_t const cap = r.walkBackDirect ? kMaxWalkBackHops : kMaxApproachHops;
        if (!f.approachKnown || r.approachHops >= cap)
        {
            std::string const why = !f.approachKnown ? "no walkable path toward " + what
                                                     : std::to_string(cap) + " hops did not reach " + what;
            if (r.walkBackDirect)
            {
                // Straight there failed: replay the plan's points instead.
                if (ReplayWalkBack(r, f))
                    r.note = "walk back: " + why + ": replaying the plan's points";
                else
                    r.note = "walk back: " + why;
                return true;
            }
            r.note = "walk back: " + why + ": on to the next";
            NextWalkBackPoint(r);
            return true;
        }
        ++r.approachHops;
        r.note = "walk back: approaching " + what;
        WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
        return true;
    }
    bool const direct = p.kind == PlanStepKind::MoveDirect;
    if (direct && r.phase != Phase::Walk && f.maxMemberYd > kGatherYd && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "walk back: gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            if (SummonStraggler(r, f, order))
                EnterPhase(r, Phase::Gather);
            else
                Pause(r, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                    " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
        }
        return true;
    }
    r.note = "walk back: to the point of step " + std::to_string(r.walkBackIdx + 1);
    if (WalkTo(r, f, p.x, p.y, p.z, direct, dt, order))
        NextWalkBackPoint(r);
    return true;
}

// pull and boss steps: approach until seen, then pull in order until nothing
// hostile of the step is left. `order` lists the entries in kill order (empty:
// any entry, nearest first).
inline void FightStep(Run& r, const Facts& f, const PlanStep& s, const std::vector<std::uint32_t>& killOrder,
    std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    // A step for the other team (flags 2/4):
    //   - a pack is skipped without a look, unless the party already stands
    //     near it and a unit of it is not friendly;
    //   - a boss is always looked at first, and skipped only when every unit
    //     of its encounter there is friendly to the party. A hostile or
    //     neutral one is fought (Uldaman's Lost Dwarves carry team horde in
    //     the plan, yet were hostile to an Alliance party in a live run).
    if (!ai::StepForTeam(s, f.team))
    {
        bool notFriendly = false;
        for (const UnitFact& u : f.units)
            notFriendly = notFriendly || (u.hostile && !r.passed.count(u.guid));
        bool const skip = s.kind == PlanStepKind::Boss ? (f.seen && !notFriendly) : (!f.seen || !notFriendly);
        if (skip)
        {
            NextStep(r, s.kind != PlanStepKind::Boss ? "pack is for the other team: skipped"
                : f.units.empty() ? "boss is for the other team and not here: skipped"
                                  : "boss is for the other team and friendly to this party: skipped");
            return;
        }
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack");
        return;
    }
    if (s.kind == PlanStepKind::Boss)
        for (const UnitFact& u : f.units)
            r.bossSeenAlive = r.bossSeenAlive || u.entry == s.entry;
    const UnitFact* pick = nullptr;
    if (killOrder.empty())
        pick = PickPackPull(r, f);
    for (std::uint32_t entry : killOrder)
        if (!pick)
            pick = PickPull(r, f.units, entry);
    if (pick)
    {
        r.bossWaitMs = 0;
        // The way back to the anchor not known yet: read it first (one tick),
        // before this pull counts as one.
        if (r.phase != Phase::Gather && !r.gatherWaived && CheckAnchor(r, f) == AnchorState::Unknown)
        {
            r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
            return;
        }
        // Pulls that kill nothing, three in a row: the rest of the pack is
        // out of reach (a pit, a ledge). It is left, not counted cleared.
        std::size_t const open = OpenUnits(r, f.units);
        if (r.phase != Phase::Gather)
        {
            if (r.pulls > 0 && open >= r.openAtLastPull)
                ++r.fruitlessPulls;
            else if (r.pulls > 0)
                r.fruitlessPulls = 0;
            r.openAtLastPull = open;
        }
        if (r.fruitlessPulls >= kFruitlessPulls)
        {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid))
                    r.unreachable.insert(u.guid);
            r.notCleared.insert(r.step);
            NextStep(r, std::string(s.kind == PlanStepKind::Boss ? "boss" : "pack") + " skipped, not cleared: " +
                std::to_string(open) + " units out of reach after " + std::to_string(kFruitlessPulls) +
                " pulls that killed nothing");
            return;
        }
        PullUnit(r, f, *pick, order, PickCc(r, f, pick->guid));
        return;
    }
    if (OpenUnits(r, f.units) > 0)
    {
        // Only evading or stealthed units left: let them settle (the step's
        // idle ceiling bounds this), and walk through a stealthed pack.
        r.note = "waiting for the pack to settle";
        bool const stealthOnly = [&] {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid) && !u.stealthed)
                    return false;
            return true;
        }();
        if (stealthOnly && WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            NextStep(r, "walked through a stealthed pack");
        return;
    }
    if ((s.flags & ai::kPlanFlagAmbush) && !r.walkedThrough)
    {
        // An ambush pack shows itself only when the party walks into it.
        r.note = "walking through an ambush pack";
        if (WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            r.walkedThrough = true;
        if (!r.walkedThrough)
            return;
    }
    std::size_t outOfReach = 0;
    bool friendlyLeft = false;
    for (const UnitFact& u : f.units)
    {
        if (u.hostile && r.unreachable.count(u.guid))
            ++outOfReach;
        friendlyLeft = friendlyLeft || !u.hostile;
    }
    std::string const what = s.kind == PlanStepKind::Boss ? "boss" : "pack";
    if (outOfReach)
    {
        r.notCleared.insert(r.step);
        NextStep(r, what + " skipped, not cleared: " + std::to_string(outOfReach) + " units out of reach");
    }
    else if (friendlyLeft)
        NextStep(r, what + " is friendly to this party: left alone");
    else if (s.kind != PlanStepKind::Boss)
        NextStep(r, "pack cleared");
    else if (f.bossDeadNearSpot || (r.bossSeenAlive && f.bossDeadNearLead))
        NextStep(r, "boss down");
    else
    {
        // Not seen alive and not seen dead: he is away (a patrol, Theka in
        // Zul'Farrak), not killed. The plan rows carry no patrol path: the
        // party waits at his spot for him to come by, then pauses.
        r.bossWaitMs += dt;
        r.note = "boss " + std::to_string(s.entry) + " not at his spot: waiting for him";
        if (r.bossWaitMs >= kBossSearchMs)
            Pause(r, "boss " + std::to_string(s.entry) + " not found: not alive or dead within " + Num(s.radius) +
                " yd of " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z) + " for " +
                std::to_string(kBossSearchMs / 1000) + " s (.bot run skip if he is already dead)");
    }
}

inline void MoveStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0))
    {
        PullUnit(r, f, *hopUnit, order);
        return;
    }
    // Far away (after a wipe the lead bot comes back from the entrance; a
    // long plan hop): walk back on the navmesh in hops first.
    if (!r.detour && !f.seen)
    {
        ApproachHop(r, f, s, dt, order, "next point");
        return;
    }
    // A direct step takes the lead bot's line: the party gathers on it first
    // (`cmd move ... direct` refuses while a bot has no way to the lead's spot).
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && f.maxMemberYd > kGatherYd && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            if (SummonStraggler(r, f, order))
                EnterPhase(r, Phase::Gather);
            else
                Pause(r, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                    " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
        }
        return;
    }
    if (r.detour)
    {
        r.note = "walking around: the plan's point is off the walkable way";
        if (WalkTo(r, f, r.detourX, r.detourY, r.detourZ, false, dt, order))
            NextStep(r, "arrived at the nearest reachable point, " +
                Num(ai::PlanDistance(r.detourX, r.detourY, r.detourZ, s.x, s.y, s.z)) + " yd from the plan's");
        return;
    }
    r.note = "walking";
    if (WalkTo(r, f, s.x, s.y, s.z, s.kind == PlanStepKind::MoveDirect, dt, order))
        NextStep(r, "arrived");
}

// The glue found that the current move step's point cannot be reached (its
// `cmd move` was refused unreachable or off-mesh) and that (x,y,z), within
// kDetourMaxYd of it, can: the step walks there instead, by the navmesh path
// around whatever is in the way. False (nothing changes) when the step is not
// a move, a detour is already on, or the point is too far from the plan's.
inline bool UseDetour(Run& r, float x, float y, float z)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.detour ||
        (s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect))
        return false;
    if (ai::PlanDistance(x, y, z, s->x, s->y, s->z) > kDetourMaxYd)
        return false;
    r.detour = true;
    r.detourX = x;
    r.detourY = y;
    r.detourZ = z;
    EnterPhase(r, Phase::Begin);
    return true;
}

// loot, open, use_item: one commander task for the carrier, then its result.
inline void TaskStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    if (!r.orderSent)
    {
        if ((s.kind == PlanStepKind::Loot || s.kind == PlanStepKind::UseItem) && s.item && f.carrierHasItem)
        {
            NextStep(r, kind + ": " + f.carrier + " already carries item " + std::to_string(s.item));
            return;
        }
        if (s.kind == PlanStepKind::Open && s.item && !f.carrierHasItem)
        {
            if (r.missing.count(s.item))
            {
                std::size_t const here = r.step;
                NextStep(r, "open " + std::to_string(s.entry) + " left out: it needs item " + std::to_string(s.item) +
                    ", which the run did not get");
                r.skippedOpenStep = here;
                return;
            }
            Pause(r, "open: " + f.carrier + " does not carry item " + std::to_string(s.item) + " (the lock needs it)");
            return;
        }
        if (s.kind == PlanStepKind::UseItem && !f.carrierHasEntryItem)
        {
            if (r.missing.count(s.entry))
            {
                if (s.item)
                    r.missing.insert(s.item);
                NextStep(r, "use_item " + std::to_string(s.entry) + " left out: the run did not get that item");
                return;
            }
            r.pendingMissing = s.item;
            Pause(r, "use_item: " + f.carrier + " does not carry item " + std::to_string(s.entry));
            return;
        }
        if (f.carrierBusy)
        {
            r.note = kind + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        r.note = kind + " by " + f.carrier;
        if (s.kind == PlanStepKind::Loot)
            order = "loot " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier + (s.item ? " " + std::to_string(s.item) : "");
        else if (s.kind == PlanStepKind::Open)
            order = "open " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier;
        else
            order = "use " + std::to_string(s.entry) + " " + f.carrier;
        return;
    }
    bool const consequence = s.kind == PlanStepKind::UseItem && s.item && !r.missing.empty();
    if (f.taskReply == 2)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item failed (" + f.taskText + "): an earlier item is missing; item " +
                std::to_string(s.item) + " is noted missing too");
            return;
        }
        if (s.kind != PlanStepKind::Open && s.item)
            r.pendingMissing = s.item;
        Pause(r, kind + " failed: " + f.taskText +
            (r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
                " missing: .bot run skip leaves out the steps that need it)" : ""));
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, kind + ": " + f.carrier + " did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (s.kind != PlanStepKind::Open && s.item && !f.carrierHasItem)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item did not make item " + std::to_string(s.item) +
                " (an earlier item is missing): it is noted missing too");
            return;
        }
        r.pendingMissing = s.item;
        Pause(r, kind + " done, but item " + std::to_string(s.item) + " is not in " + f.carrier +
            "'s bags (.bot run skip leaves out the steps that need it)");
        return;
    }
    NextStep(r, kind + " done");
}

// ---------------------------------------------------------------------------
// Scripted events (docs/guides/dungeon-run-leader.md "Events"): a start step
// (`ritual` here; `open` and `use_item` start events too), then a `hold` that
// stands on a spot and fights what the event sends until its done condition.
// ---------------------------------------------------------------------------

// The bots of a ritual: the carrier (the lead bot) first, then the nearest
// living bots of the party, `want` in all (fewer when the party has fewer).
inline std::vector<std::string> RitualBots(const Facts& f, std::uint32_t want)
{
    std::vector<const MemberFact*> others;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.name != f.carrier)
            others.push_back(&m);
    std::sort(others.begin(), others.end(), [](const MemberFact* a, const MemberFact* b) { return a->yd < b->yd; });
    std::vector<std::string> names;
    if (!f.carrier.empty() && want)
        names.push_back(f.carrier);
    for (const MemberFact* m : others)
        if (names.size() < want)
            names.push_back(m->name);
    return names;
}

// ritual: `cmd ritual` for `param` bots (the commander's own order: they walk
// to the object, use it in turn and hold still until it has cast), then its
// one answer. An ERR pauses with the module's words; it never waits blind.
inline void RitualStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const what = "ritual " + std::to_string(s.entry);
    std::uint32_t const want = s.param ? s.param : ai::kPlanDefaultRitualBots;
    if (!r.orderSent)
    {
        // The event this ritual starts, when its hold reads an encounter state
        // (the glue reads that data on the ritual step too).
        const PlanStep* hold = EventHoldAhead(r);
        if (hold && (hold->flags & ai::kPlanFlagDoneData) && (hold->flags & ai::kPlanFlagEncounter) && f.eventData >= 0)
        {
            if (f.eventData == static_cast<long long>(hold->param))
            {
                NextStep(r, what + " left out: the event is done already (data " + std::to_string(hold->entry) +
                    " reads " + std::to_string(hold->param) + ")");
                return;
            }
            // Still in progress or failed (Archaedas walking home after a
            // reset): a ritual now would start a dead event. Wait for the
            // reset, bounded (the Stone Keepers stay at fail for good).
            bool const busy = f.eventData == 1 || f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (busy && r.eventResetWaitMs < kEventResetWaitMs)
            {
                r.eventResetWaitMs += kTickMs;
                r.note = what + ": waiting for the encounter to reset (data " + std::to_string(hold->entry) + " reads " +
                    std::to_string(f.eventData) + ")";
                return;
            }
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        std::vector<std::string> const bots = RitualBots(f, want);
        if (bots.size() < want)
        {
            Pause(r, what + " needs " + std::to_string(want) + " living bots; " + std::to_string(bots.size()) +
                " are up (revive the party, then .bot run resume)");
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        order = "ritual " + EntryArg(s.entry, s.x, s.y, s.z);
        std::string list;
        for (const std::string& name : bots)
        {
            order += " " + name;
            list += (list.empty() ? "" : ", ") + name;
        }
        r.note = what + ": starting the event (" + list + ")";
        return;
    }
    if (f.taskReply == 2)
    {
        Pause(r, SpentRitual(r, "ritual ", f.taskText) ? SpentRitualNote(what) : what + " failed: " + f.taskText);
        return;
    }
    if (f.taskReply == 1)
    {
        NextStep(r, what + " done: the event started");
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, what + ": the bots did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (r.phaseMs >= kRitualNoReplyMs)
        Pause(r, what + ": the bots are free again but the ritual gave no answer (.bot run resume tries it again)");
}

// The event's units now (the glue lists the living ones within the hold's
// radius, the friendly, still dormant ones too, and counts the dead).
// Whether a unit of a hold's radius counts as one of the event's own: awake
// (hostile), or of the hold's first entry (the event's named mobs: the four
// Stone Keepers, Archaedas), dormant ones too. The dormant statues of the other
// entries in the hall (Archaedas's room holds some fifty Earthen Guardians and
// Custodians, 2026-10-16 live run: "0/53 down") count only once they wake.
inline bool EventUnitCounts(const PlanStep& s, const UnitFact& u)
{
    return u.hostile || s.entries.empty() || u.entry == s.entries.front();
}

inline void CountEvent(Run& r, const Facts& f, const PlanStep& s)
{
    std::uint32_t up = 0, counted = 0;
    for (const UnitFact& u : f.units)
    {
        up += u.hostile ? 1 : 0;
        counted += EventUnitCounts(s, u) ? 1 : 0;
    }
    r.eventUp = up;
    r.eventDown = f.eventDown;
    r.eventTotal = counted + f.eventDown;
}

// "2 of 4 down, 1 awake", or "" before any unit of the event was seen.
inline std::string EventProgress(const Run& r)
{
    if (!r.eventTotal)
        return std::string();
    return std::to_string(r.eventDown) + " of " + std::to_string(r.eventTotal) + " down" +
        (r.eventUp ? ", " + std::to_string(r.eventUp) + " awake" : std::string());
}

// The step that started the event a hold waits on: the nearest ritual, open or
// use_item before it, with only moves, rests and waits between. npos for none.
inline std::size_t EventStartStep(const Run& r)
{
    if (!r.plan)
        return static_cast<std::size_t>(-1);
    for (std::size_t i = r.step; i-- > 0;)
    {
        PlanStepKind const k = r.plan->steps[i].kind;
        if (k == PlanStepKind::Ritual || k == PlanStepKind::Open || k == PlanStepKind::UseItem)
            return i;
        if (k != PlanStepKind::Move && k != PlanStepKind::MoveDirect && k != PlanStepKind::Rest &&
            k != PlanStepKind::WaitObject && k != PlanStepKind::WaitClear)
            break;
    }
    return static_cast<std::size_t>(-1);
}

// The event reset (or never started): the run goes back to the step that
// starts it and pauses, so `.bot run resume` regroups and starts it again.
inline void EventFailed(Run& r, const std::string& why)
{
    std::size_t const start = EventStartStep(r);
    std::string again = ": .bot run resume waits for it again, .bot run skip goes on without it";
    if (start != static_cast<std::size_t>(-1))
    {
        r.step = start;
        r.autoRegroups = 0;
        ResetStep(r);
        again = ": .bot run resume regroups and starts it again at step " + std::to_string(start + 1) + " (" +
            ai::PlanStepKindName(r.plan->steps[start].kind) + ")";
    }
    r.fighting = false;
    r.eventRetry = true;
    Pause(r, "event failed (" + why + ")" + again);
}

// hold: walks to the spot, then stands there and lets the bots fight what the
// event sends (no pulls of its own, but for an awake unit of the event that
// does not come within kHoldIdlePullMs), walks back to the spot after a fight
// that took it more than kHoldReturnYd away, and ends on the done condition.
// A failed event goes back to its start step (EventFailed); `timeout_s` is the
// event's ceiling, fights included.
inline void HoldStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, "event spot");
        return;
    }
    r.eventOn = true;
    CountEvent(r, f, s);
    std::string const progress = EventProgress(r);
    std::string const tail = progress.empty() ? std::string() : " (" + progress + ")";
    std::string const data = "data " + std::to_string(s.entry);
    if (s.flags & ai::kPlanFlagDoneData)
    {
        if (f.eventData < 0)
        {
            Pause(r, "event: the lead bot's map has no instance data to read (" + data + ")");
            return;
        }
        if (f.eventData == static_cast<long long>(s.param))
        {
            NextStep(r, "event done: " + data + " reads " + std::to_string(s.param) + tail);
            return;
        }
        if (s.flags & ai::kPlanFlagEncounter)
        {
            bool const notStarted = f.eventData == static_cast<long long>(ai::kPlanEncounterNotStarted);
            bool const failed = f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (!notStarted && !failed)
                r.eventStarted = true;
            if (failed)
            {
                EventFailed(r, data + " reads 2, fail: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventStarted)
            {
                EventFailed(r, data + " is back to 0, not started: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventMs >= kEventStartMs)
            {
                EventFailed(r, data + " did not leave 0, not started, in " + std::to_string(kEventStartMs / 1000) +
                    " s: the start did not take");
                return;
            }
        }
    }
    else if (s.flags & ai::kPlanFlagDoneObject)
    {
        if (f.objectState == static_cast<int>(s.param))
        {
            NextStep(r, "event done: object " + std::to_string(s.entry) + " is in state " + std::to_string(s.param) + tail);
            return;
        }
    }
    else
    {
        if (r.eventUp)
            r.eventStarted = true;
        if (r.eventStarted && f.units.empty())
        {
            NextStep(r, "event done: every unit of it is dead" + tail);
            return;
        }
    }
    if (r.eventMs >= s.timeoutS * 1000u)
    {
        Pause(r, "event not done after " + std::to_string(s.timeoutS) + " s" + tail +
            ": .bot run resume waits again, .bot run skip goes on");
        return;
    }
    // Hold the spot: walk there first, and back after a fight that took the
    // lead bot away (the bots chase in a fight; the run brings them back).
    float const fromSpot = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
    if (!r.holdArrived || fromSpot > kHoldReturnYd)
    {
        if (!WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
        {
            if (r.state == State::Running)
                r.note = (r.holdArrived ? "event: back to the hold spot" : "event: walking to the hold spot") + tail;
            return;
        }
        r.holdArrived = true;
    }
    if (r.phase != Phase::Hold)
        EnterPhase(r, Phase::Hold);
    // An awake unit of the event that does not come is pulled after a while.
    const UnitFact* idle = PickPull(r, f.units, 0);
    r.eventIdleMs = idle ? r.eventIdleMs + dt : 0;
    if (idle && r.eventIdleMs >= kHoldIdlePullMs)
    {
        r.eventIdleMs = 0;
        PullUnit(r, f, *idle, order);
        return;
    }
    r.note = "event: holding the spot" + tail;
}

// The out-of-combat ceiling of a step: kStepIdleCapMs, or a wait's own
// timeout plus a minute to get there when that is longer.
inline std::uint32_t StepIdleCapMs(const PlanStep& s)
{
    bool const wait = s.kind == PlanStepKind::WaitObject || s.kind == PlanStepKind::WaitClear ||
        s.kind == PlanStepKind::Hold;
    std::uint32_t const own = wait ? s.timeoutS * 1000 + 60000 : 0;
    return own > kStepIdleCapMs ? own : kStepIdleCapMs;
}

// One second of the run. Returns the order to send now (the text after
// `.bot cmd `), or "" for none.
inline std::string Tick(Run& r, const Facts& f, std::uint32_t dt = kTickMs, const RestLines& lines = RestLines())
{
    std::string order;
    if (r.state != State::Running)
        return order;
    const PlanStep* s = Current(r);
    if (!s)
    {
        r.state = State::Done;
        r.note = DoneNote(r);
        return order;
    }
    r.phaseMs += dt;
    if (r.eventOn)
        r.eventMs += dt; // an event's clock runs in its fights too
    r.gatherOrder.clear(); // only this tick's gather can be refused next
    if (f.anchorPathRead)
        r.anchorPathWanted = false;

    // A wipe (the whole party dead) counts once, however long it lasts.
    bool const allDead = f.total > 0 && f.alive == 0;
    if (allDead && !r.wiped)
        ++r.wipes;
    r.wiped = allDead;
    // A near wipe (kWipeDead or more dead, someone still alive) counts once
    // too; one that ends in a wipe counts as both.
    bool const nearWipe = f.dead >= kWipeDead && f.alive > 0;
    // The latch holds until fewer than kWipeDead are dead: a near wipe that
    // turns into a wipe, and the revive one by one after it, is one event.
    if (nearWipe && !r.nearWiped)
        ++r.nearWipes;
    if (nearWipe)
        r.nearWiped = true;
    else if (f.dead < kWipeDead)
        r.nearWiped = false;

    // After a resume or a skip from a pause: the dead get up, the living
    // gather on the lead bot, then the step goes on.
    if (r.regroup)
    {
        r.regroupMs += dt;
        if (f.dead > 0 || !f.leadAlive)
        {
            r.note = "regroup: " + std::to_string(f.dead) + " dead, reviving";
            if (!r.reviveSent || r.regroupMs % 60000 < dt)
            {
                r.reviveSent = true;
                order = "revive party";
            }
            if (r.regroupMs > kRegroupReviveMs)
                Pause(r, "regroup: " + std::to_string(f.dead) + " still dead after " +
                    std::to_string(kRegroupReviveMs / 1000) + " s (cmd revive <bot>, then .bot run resume)");
            return order;
        }
        if (f.maxMemberYd > kGatherYd && !f.anyInCombat && !r.gatherWaived)
        {
            r.note = "regroup: gathering on the lead bot";
            if (!r.regroupGatherSent)
            {
                r.regroupGatherSent = true;
                r.regroupMs = 0;
                SendGather(r, f, order);
            }
            else if (r.regroupMs > kRegroupGatherMs)
            {
                if (SummonStraggler(r, f, order))
                    r.regroupMs = 0;
                else
                    Pause(r, "regroup: the party did not gather within " + std::to_string(kRegroupGatherMs / 1000) +
                        " s (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
            }
            return order;
        }
        r.regroup = false;
        r.gatherWaived = false;
        r.gatherRefusals = 0;
        r.note = "regrouped";
        if (r.walkBackDue)
        {
            StartWalkBack(r, f);
            if (r.walkBack)
                return order; // the next tick reads the facts for the first point
        }
    }

    // A wipe or kWipeDead dead: once the fight is over the run regroups by
    // itself (revive, gather, walk back), kAutoRegroupsPerStep times a step;
    // then it pauses for a regroup by hand.
    bool const wipe = f.dead >= kWipeDead || allDead;
    bool const autoRegroup = wipe && r.autoRegroups < kAutoRegroupsPerStep;
    if (wipe && !autoRegroup)
    {
        Pause(r, std::to_string(f.dead) + " dead: regroup (cmd revive party), then .bot run resume (" +
            std::to_string(kAutoRegroupsPerStep) + " regroups by itself at this step did not hold)");
        return order;
    }
    if (autoRegroup && !f.anyInCombat)
    {
        ++r.autoRegroups;
        r.fighting = false;
        ResetStep(r);
        BeginRegroup(r);
        r.walkBackDue = true;
        r.note = std::to_string(f.dead) + " dead (" + WipeCounts(r) + " this run): regrouping by itself (" +
            std::to_string(r.autoRegroups) + " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)";
        return order;
    }
    // The lead bot dead with fewer than kWipeDead dead: the others fight on
    // (more may fall: then it is a wipe and the run regroups by itself);
    // once the fight is over it pauses.
    if (!f.leadAlive && !autoRegroup && !f.anyInCombat)
    {
        Pause(r, "the lead bot is dead: revive it, then .bot run resume");
        return order;
    }

    // Fights are the bots' own; the run only bounds them.
    if (f.anyInCombat)
    {
        // The ritual answered and its event is fighting already (the first
        // Stone Keeper wakes with the cast): the fight is the hold's.
        if (s->kind == PlanStepKind::Ritual && r.orderSent && f.taskReply == 1)
        {
            NextStep(r, "ritual " + std::to_string(s->entry) + " done: the event started");
            s = Current(r);
            if (!s)
                return order;
            if (s->kind == PlanStepKind::Hold)
                r.eventOn = true;
        }
        if (!r.fighting)
        {
            r.fighting = true;
            r.fightMs = 0;
        }
        r.fightMs += dt;
        // A hold's fights are the event (one wave after the other, no pause
        // between): its own timeout bounds them when that is longer.
        // A start step's fight (or a move's) before its hold is the event's too.
        const PlanStep* const event = s->kind == PlanStepKind::Hold ? s : EventHoldAhead(r);
        std::uint32_t const holdCap = event && event->timeoutS * 1000u > kBossFightCapMs ? event->timeoutS * 1000u
                                                                                       : kBossFightCapMs;
        std::uint32_t const cap = event ? holdCap
                                : s->kind == PlanStepKind::Boss ? kBossFightCapMs : kFightCapMs;
        if (r.fightMs > cap && autoRegroup)
        {
            // kWipeDead or more dead and one left fighting past the ceiling:
            // a wipe, not a long fight. The regroup starts.
            ++r.autoRegroups;
            r.fighting = false;
            ResetStep(r);
            BeginRegroup(r);
            r.walkBackDue = true;
            r.note = std::to_string(f.dead) + " dead and the fight went past " + std::to_string(cap / 1000) +
                " s (" + WipeCounts(r) + " this run): regrouping by itself (" + std::to_string(r.autoRegroups) +
                " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)";
            return order;
        }
        if (r.fightMs > cap)
        {
            Pause(r, "a fight lasted more than " + std::to_string(cap / 1000) + " s");
            return order;
        }
        r.note = autoRegroup ? std::to_string(f.dead) + " dead: fighting on, a regroup follows"
               : !f.leadAlive ? std::string("the lead bot is dead: the others fight on") : std::string("fighting");
        if (s->kind == PlanStepKind::Hold && r.eventOn && f.seen)
        {
            CountEvent(r, f, *s);
            std::string const progress = EventProgress(r);
            if (!autoRegroup && f.leadAlive)
                r.note = "event: fighting" + (progress.empty() ? std::string() : " (" + progress + ")");
        }
        if (CheckCc(r, f, dt, order))
            return order;
        // The plan's kill_first unit of the fight (a healer, its healing ward)
        // is focused: skull on it, every bot attacks it. Again only when the
        // best one changes (it died, or a ward came up).
        if (const UnitFact* k = PickKillFirst(r, f))
            if (k->guid != r.focusGuid)
            {
                r.focusGuid = k->guid;
                r.note = "fighting: kill " + std::to_string(k->entry) + " first";
                order = "focus " + std::to_string(k->guid);
            }
        return order;
    }
    if (r.fighting)
    {
        // Back to the step's start: it looks at what is left, and rests first.
        r.fighting = false;
        r.restDue = true;
        r.pullGuid = 0;
        r.focusGuid = 0;
        if (r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
            EnterPhase(r, Phase::Begin);
    }

    // A walk back is bounded point by point (WalkTo), not by the step's ceiling.
    if (!r.walkBack)
        r.stepIdleMs += dt;
    if (r.stepIdleMs > StepIdleCapMs(*s))
    {
        Pause(r, "step " + std::to_string(r.step + 1) + " (" + ai::PlanStepKindName(s->kind) + ") took more than " +
            std::to_string(StepIdleCapMs(*s) / 1000) + " s out of combat");
        return order;
    }

    // Rest gate.
    if (r.phase == Phase::Rest)
    {
        if (Rested(f, lines) || r.phaseMs >= kRestCapMs)
            EnterPhase(r, Phase::Begin);
        else
        {
            r.restRepeatMs += dt;
            if (r.restRepeatMs >= kRestRepeatMs)
            {
                r.restRepeatMs = 0;
                order = "rest";
            }
            return order;
        }
    }
    if (r.phase == Phase::Begin && (r.restDue || (s->kind == PlanStepKind::Rest && !r.stepRested)))
    {
        bool const forced = s->kind == PlanStepKind::Rest && !r.stepRested;
        r.restDue = false;
        r.stepRested = r.stepRested || s->kind == PlanStepKind::Rest;
        if (forced || NeedsRest(f, lines))
        {
            EnterPhase(r, Phase::Rest);
            r.restRepeatMs = 0;
            r.note = "resting";
            order = "rest";
            return order;
        }
    }

    // A follower a direct step left behind (OrderRefused): summoned before the
    // step goes out again.
    if (!r.summonDue.empty())
    {
        order = "summon " + r.summonDue;
        r.summonWait = r.summonDue;
        r.summonWaitMs = 0;
        r.summonDue.clear();
        return order;
    }
    // ... then waits for that bot to stand at the lead bot (the summon is the
    // 3 s `.bot summon` flow: a step sent at once still finds it away), at most
    // kSummonWaitMs; then the step goes out again.
    if (!r.summonWait.empty())
    {
        const MemberFact* member = nullptr;
        for (const MemberFact& m : f.members)
            if (m.name == r.summonWait)
                member = &m;
        r.summonWaitMs += dt;
        if (member && member->yd > kArriveYd && r.summonWaitMs < kSummonWaitMs)
        {
            r.note = "direct step: waiting for " + r.summonWait + " (summoned)";
            return order;
        }
        r.summonWait.clear();
        r.summonWaitMs = 0;
        EnterPhase(r, Phase::Begin);
    }

    if (WalkBack(r, f, dt, order))
        return order;

    switch (s->kind)
    {
        case PlanStepKind::Move:
        case PlanStepKind::MoveDirect:
            MoveStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::Pull:
            FightStep(r, f, *s, {}, dt, order);
            break;
        case PlanStepKind::Boss:
        {
            std::vector<std::uint32_t> killOrder = s->entries;
            killOrder.push_back(s->entry);
            FightStep(r, f, *s, killOrder, dt, order);
            break;
        }
        case PlanStepKind::Loot:
        case PlanStepKind::Open:
        case PlanStepKind::UseItem:
            TaskStep(r, f, *s, order);
            break;
        case PlanStepKind::WaitObject:
            if (r.skippedOpenStep != static_cast<std::size_t>(-1) && r.step == r.skippedOpenStep + 1)
            {
                NextStep(r, "wait for object " + std::to_string(s->entry) + " left out: the open before it was left out");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.objectState == static_cast<int>(s->param))
                NextStep(r, "object " + std::to_string(s->entry) + " is in the state the plan waits for");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s for object " + std::to_string(s->entry) +
                    (f.objectState < 0 ? " (not found near its point)" : " (state " + std::to_string(f.objectState) + ")"));
            else
                r.note = "waiting for object " + std::to_string(s->entry);
            break;
        case PlanStepKind::WaitClear:
            if (!f.seen)
            {
                ApproachHop(r, f, *s, dt, order, "spot to watch");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.hostilesNear == 0)
                NextStep(r, "no hostile left near the spot");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s; " + std::to_string(f.hostilesNear) +
                    " hostiles still near the spot");
            else
                r.note = "waiting for the spot to clear";
            break;
        case PlanStepKind::Rest:
            NextStep(r, "rested");
            break;
        case PlanStepKind::Ritual:
            RitualStep(r, f, *s, order);
            break;
        case PlanStepKind::Hold:
            HoldStep(r, f, *s, dt, order);
            break;
        default:
            Pause(r, std::string("step kind ") + ai::PlanStepKindName(s->kind) +
                " is not supported yet: do it by hand, then .bot run skip and .bot run resume");
            break;
    }
    return order;
}

// The run's phase as the status line and the dashboard show it: during an
// event's hold, how far the event is ("hold 1/4 down", "fight 2/4 down": units
// of the event dead of all seen), else the phase's name.
inline std::string PhaseText(const Run& r)
{
    const PlanStep* s = Current(r);
    if (s && s->kind == PlanStepKind::Hold && r.eventOn && r.eventTotal && r.state == State::Running)
        return std::string(r.fighting ? "fight " : "hold ") + std::to_string(r.eventDown) + "/" +
            std::to_string(r.eventTotal) + " down";
    return PhaseName(r.phase);
}

// The status line for the commander:
//   TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>
// <step> counts from 1 (0 before the first, steps+1 is never sent: a done run
// shows the last). '|' and line breaks in the note become spaces.
inline std::string StatusLine(const Run& r)
{
    std::size_t const total = r.plan ? r.plan->steps.size() : 0;
    std::size_t const shown = total == 0 ? 0 : (r.step < total ? r.step + 1 : total);
    const PlanStep* s = r.plan && shown ? &r.plan->steps[shown - 1] : nullptr;
    std::string note = r.note;
    for (char& c : note)
        if (c == '|' || c == '\n' || c == '\r')
            c = ' ';
    std::string leg = s ? r.plan->LegName(s->leg) : std::string();
    if (leg.empty() && s)
        leg = std::to_string(s->leg);
    return "TBM:RUN|" + (r.plan ? r.plan->name : std::string("-")) + "|" + StateName(r.state) + "|" +
        std::to_string(shown) + "/" + std::to_string(total) + "|" + (leg.empty() ? "-" : leg) + "|" +
        (s ? ai::PlanStepKindName(s->kind) : "-") + "|" + PhaseText(r) + "|" + (note.empty() ? "-" : note);
}

// Which steps the run leader does today; the rest pause the run.
inline bool Supported(PlanStepKind kind)
{
    return kind != PlanStepKind::None;
}

} // namespace DungeonRun
} // namespace TortoiseBots
