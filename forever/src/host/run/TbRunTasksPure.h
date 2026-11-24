// Slice 9, package O9 (object, loot and use tasks): the PURE half (forever/docs/SLICE9-DOORS-NOTES.md). Plain data in, plain data out: no core and no legacy
// type, so forever/tools/test_run_tasks.cpp runs it standalone, next to the run leader's own header (runtime/DungeonRunPolicy.h), which proves that every answer
// text is the one the policy parses (LootRefusalOpen, TaskResendable, GoneIsDone).
//
// What is here: the order grammar of `open`, `loot`, `lootmethod` and `use` (the policy's own strings), the verdicts of the lock and of the loot window, the
// task clock (the Turtle machine of runtime/RemoteCommanderTasks.h: walk, act, result, one task per bot) as ONE function over an observation of the world,
// and the loot-method rule. The core half (TbRunTasks.cpp) fills the observation from the core's public API and carries out what this half decides.
//
// Everything the module cannot do on ForeverCore is refused with a code, never answered "ok" (findings rule of slice 1): `use <item>` (no plan of the two
// dungeons has a use_item row, and the item-use path is not ported), a lock whose key the bot does not carry (the core's `Use` would not look: the host
// does). Every loot method is carried out; the run itself goes on only under group loot and need-before-greed (TbRollMethodsPure.h).
#pragma once

#include "runtime/GossipOrderPolicy.h"
#include "runtime/RemoteCommanderPolicy.h"
#include "forever/src/host/TbRollMethodsPure.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
namespace tasks
{
namespace RC = ::TortoiseBots::RemoteCommander;

// ---- the numbers ----
constexpr float kEntrySearchYd = 5.0f;          // spec 7: a plan row names an object by entry and point; a spawn farther than this from the point is not "it" (never another one of the entry)
constexpr float kCorpseRangeYd = 5.0f;          // a corpse is looted from this close (the core accepts 30: AELootCreatureCheck::LootDistance, LootHandler.cpp:42)
constexpr uint32_t kWalkReorderMs = 3000;       // a bot standing still short of its stand is sent to it again this often
constexpr uint32_t kScriptSettleMs = 1500;      // a goober's script may answer the use without changing the goober's own state (the Defias Cannon sets the Iron Clad Door instead)
constexpr uint32_t kTaskCapMs = 60000;          // the whole task, walk and act: the policy's own cap is 90 s (kTaskCapMs there)
constexpr uint32_t kFreeSlotsFloor = 2;         // the carrier keeps this many free bag slots (spec 5d); a loot order that needs a new slot below it is left alone
constexpr int kLootNone = -1;                   // the policy's "no group / not a method it knows" (Facts::lootMethod)

// ---- the words the policy matches (runtime/DungeonRunPolicy.h kLootRightsText, kLootEmptyText, TaskResendable, GoneIsDone) ----
inline char const* LootRightsText() { return "nothing this bot may loot"; }
inline char const* LootEmptyText() { return "Nothing is left to loot"; }

// ---- the grammar ----
enum class Verb : uint8_t { None, Open, Loot, LootMethod, Use, Gossip };
enum class OwnerKind : uint8_t { Run, Commander };

struct TaskOrder
{
    Verb verb = Verb::None;
    RC::TargetSpec spec;            // open, loot, use, gossip: the target as the policy typed it (a handle of ReadFacts, or entry:id@x,y,z)
    std::string bot;                // open, loot, use, gossip
    uint32_t itemId = 0;            // loot: only this item (0 = everything); use: the item
    int method = -1;                // lootmethod: RC::kLoot*
    int threshold = 0;
    bool hasThreshold = false;
    GossipOrder::Request gossip;    // gossip: the NPC's entry, the bot and the menu line (index or text; both empty = list the menu)
};

// Convert a ForeverCore GossipOptionNpc value to GossipOrder::Option. On this core a DATABASE menu item is added with sender = the menu id and action = the
// order index (Player::PrepareGossipMenu, GossipMenu::AddMenuItem(item, MenuID, OrderIndex)), so neither field is the plain/service word the policy reads;
// the option's own TYPE is (GossipOptionNpc::None = a plain gossip line, everything else a service: vendor, taxi, trainer, spirit healer, innkeeper/binder,
// banker, battlemaster, auctioneer, stablemaster, mailbox, talent ...). The mapping hands the policy the database shape it knows: sender 0, action
// GOSSIP_OPTION_GOSSIP (1) for None and 0 for every service (0 is a value the policy refuses: it is not GOSSIP_OPTION_GOSSIP).
inline ::TortoiseBots::GossipOrder::Option MakeGossipOption(std::string const& text, uint32_t optionNpc, bool coded)
{
    ::TortoiseBots::GossipOrder::Option opt;
    opt.text = text;
    opt.coded = coded;
    opt.sender = 0;
    opt.action = optionNpc == 0 ? ::TortoiseBots::GossipOrder::kOptionGossip : 0;
    return opt;
}

inline char const* VerbName(Verb v)
{
    switch (v)
    {
        case Verb::Open: return "open";
        case Verb::Loot: return "loot";
        case Verb::LootMethod: return "lootmethod";
        case Verb::Use: return "use";
        case Verb::Gossip: return "gossip";
        default: return "none";
    }
}

// `words` = the order split by RC::SplitOrderArgs, the verb word first and lower case. False with a code and a message in the legacy words.
inline bool ParseTaskOrder(std::string const& verb, std::vector<std::string> const& args, TaskOrder& out, std::string& code, std::string& message)
{
    out = TaskOrder();
    auto const invalid = [&](std::string const& usage)
    {
        code = "invalid";
        message = "Usage: " + usage;
        return false;
    };
    if (verb == "loot")
    {
        RC::LootOrder request;
        if (!RC::ParseLootOrder(args, request))
            return invalid("loot <corpse or object handle | entry:<id>@x,y,z> <bot> [itemId]");
        out.verb = Verb::Loot;
        out.spec = request.spec;
        out.bot = request.bot;
        out.itemId = request.itemId;
        return true;
    }
    if (verb == "open")
    {
        RC::OpenOrder request;
        if (!RC::ParseOpenOrder(args, request))
            return invalid("open <object handle | entry:<id>@x,y,z> <bot>");
        out.verb = Verb::Open;
        out.spec = request.spec;
        out.bot = request.bot;
        return true;
    }
    if (verb == "lootmethod")
    {
        int method = 0, threshold = 0;
        if (args.empty() || args.size() > 2 || !RC::ParseLootMethod(args[0], method) || (args.size() == 2 && !RC::ParseLootThreshold(args[1], threshold)))
            return invalid("lootmethod <ffa|group|rr|master|nbg> [threshold 2-5 or uncommon/rare/epic/legendary]");
        out.verb = Verb::LootMethod;
        out.method = method;
        out.threshold = threshold;
        out.hasThreshold = args.size() == 2;
        return true;
    }
    if (verb == "use")
    {
        RC::UseOrder request;
        if (!RC::ParseUseOrder(args, request))
            return invalid("use <itemId> <bot> [target]");
        out.verb = Verb::Use;
        out.itemId = request.itemId;
        out.bot = request.bot;
        out.spec = request.spec;
        return true;
    }
    if (verb == "gossip")
    {
        // The policy's own grammar (`gossip <entry> <bot> <index | "text">`, GossipOrder::ParseRequest): an index is 1-based.
        std::string rest;
        for (size_t i = 0; i < args.size(); ++i)
            rest += (i ? " " : "") + args[i];
        if (!GossipOrder::ParseRequest(rest, out.gossip))
            return invalid("gossip <npc entry> <bot> <option number 1-15 | \"option text\">");
        out.verb = Verb::Gossip;
        out.bot = out.gossip.bot;
        // The target is the NPC by entry (BeginTask looks it up near the bot). Without it the spec read as a handle of an earlier
        // read and every run gossip was refused `invalid-target` (Shadowfang Keep live run, 2026-11-23: "cmd gossip 3850 Dmtank 1
        // refused: invalid-target: No corpse or game object with that handle ...").
        out.spec.entry = out.gossip.entry;
        return true;
    }
    code = "invalid";
    message = "Not a task order.";
    return false;
}

// ---- verdicts that need no world ----

// `use <item>`: refused on ForeverCore. The text names why, so a run that needs it pauses with the reason (the policy shows `cmd <order> refused: <text>`).
inline char const* UseUnsupportedText() { return "using an item is not carried out by a run task on this core (no plan of this slice has a use_item row); the console `.tb use <character> <item id>` casts an item"; }

// `lootmethod`: the methods the module carries out. The core's own handler refuses a threshold below uncommon (GroupHandler.cpp HandleSetLootMethodOpcode).
struct MethodVerdict
{
    bool ok = true;
    std::string code;
    std::string text;
};

inline MethodVerdict JudgeLootMethod(TaskOrder const& order)
{
    MethodVerdict v;
    // Every method is carried out (the bots vote group loot and need-before-greed; under free-for-all, round robin and master loot they stay silent
    // and the human master distributes). Only the threshold is judged: the core's own handler refuses below uncommon.
    if (order.hasThreshold && (order.threshold < 2 || order.threshold > 5))
    {
        v.ok = false;
        v.code = "invalid";
        v.text = "The core accepts a loot threshold from uncommon (2) to legendary (5).";
    }
    return v;
}

enum class LootMasterTarget : uint8_t { Empty, BoundMaster, GroupLeader };

// Core HandleSetLootMethodOpcode rejects MASTER_LOOT when LootMasterGUID is not a group member.
inline LootMasterTarget ChooseLootMaster(int policyMethod, bool boundHumanMasterInGroup)
{
    if (policyMethod != RC::kLootMaster)
        return LootMasterTarget::Empty;
    return boundHumanMasterInGroup ? LootMasterTarget::BoundMaster : LootMasterTarget::GroupLeader;
}

// The loot method of a core group as the policy reads it (Facts::lootMethod): the five 1.12 methods keep their numbers; anything else (the core's
// PERSONAL_LOOT = 5, a method the 1.12 policy does not know) reads as "no" (-1): the loot step then pauses instead of guessing.
inline int PolicyLootMethod(uint32_t coreMethod)
{
    switch (coreMethod)
    {
        case 0: return RC::kLootFfa;
        case 1: return RC::kLootRoundRobin;
        case 2: return RC::kLootMaster;
        case 3: return RC::kLootGroup;
        case 4: return RC::kLootNeedBeforeGreed;
        default: return kLootNone;
    }
}

// The loot method a run may go on with (packages C9 and O9): group loot and need-before-greed, at the start and for the whole run (the table both gates
// read, TbRollMethodsPure.h). Others are named, so the supervisor can set it back. A method changed under a running run leaves a chest or corpse to rules
// the policy does not know (rolls it cannot vote, items to chance), so the run pauses and no loot or object act starts until it is group loot or
// need-before-greed again.
inline bool RunLootMethodOk(int policyMethod, std::string& why)
{
    if (rollmethods::RunMayUseMethod(policyMethod))
        return true;
    why = policyMethod == kLootNone ? std::string("the group's loot method is one the policy does not know (personal loot)")
                                    : std::string("the group's loot method is ") + RC::LootMethodName(policyMethod) + ", not group loot or need-before-greed";
    return false;
}

// The free bag slots a start needs (spec 5d: at least 12). `free` is the number of empty inventory slots the bot counts.
constexpr uint32_t kStartFreeSlots = 12;
inline bool StartBagsOk(uint32_t freeSlots) { return freeSlots >= kStartFreeSlots; }

// ---- the lock (RCT CheckLock on the core's lock row) ----
struct LockJudgement
{
    bool ok = true;
    std::string code;
    std::string text;
};

inline LockJudgement JudgeLock(RC::LockFacts const& facts)
{
    LockJudgement j;
    RC::LockVerdict const v = RC::CheckLock(facts);
    if (v == RC::LockVerdict::Open)
        return j;
    j.ok = false;
    j.code = RC::LockVerdictCode(v);
    j.text = RC::LockVerdictText(v);
    return j;
}

// ---- a game object's state, as the plan reads it and as a use changes it ----
// The packed state of RC's JudgeObjectEffect: the loot state in the high bits, the go state in the low byte. A use changes one or the other.
inline uint32_t PackState(uint32_t lootState, uint32_t goState) { return (lootState << 8) | (goState & 0xFFu); }

// A door or a lever that is not at rest (state != ready, GO_STATE_READY 1) is already used: the core's `Use` TOGGLES a door (a second use closes it), so a
// door that is open is never used again; the step's goal holds and the answer says so (true, not "ok").
inline bool AlreadyUsed(uint32_t goState) { return goState != 1u; }

// ---- the task clock ----
enum class Kind : uint8_t { Open, Loot, Gossip };

struct TaskRec
{
    Kind kind = Kind::Open;
    bool lootLike = false;          // loot window at the end: a corpse, or a chest
    bool corpse = false;            // the target is a creature corpse (else a game object)
    std::string gossipPicked;       // Gossip: the menu line whose option was selected (the answer's payload)
    uint32_t itemId = 0;
    RC::TaskState state;
    bool actStarted = false;
    uint32_t actTries = 0;
    uint32_t baseline = 0;          // the packed state of the object when the act began (Open on a door or lever)
    uint32_t sinceWalkOrderMs = kWalkReorderMs;   // the first look sends the bot (or finds it in range)
    uint32_t walkOrders = 0;
    uint32_t totalMs = 0;
    bool scriptUse = false;         // a goober: its script handles the use (GameObject::Use, GOOBER case: `OnGossipHello` / the linked event), so the object's own state may never change
    uint32_t actMs = 0;             // since the use was delivered
    std::string lastCode, lastMessage;
};

inline RC::TaskKind ToRcKind(Kind kind)
{
    switch (kind)
    {
        case Kind::Open: return RC::TaskKind::Open;
        case Kind::Loot: return RC::TaskKind::Loot;
        case Kind::Gossip: return RC::TaskKind::Gossip;
    }
    return RC::TaskKind::Open;
}

inline TaskRec MakeRec(Kind kind, bool lootLike, bool corpse, uint32_t itemId, float startDistance, bool scriptUse = false)
{
    TaskRec r;
    r.scriptUse = scriptUse;
    r.kind = kind;
    r.lootLike = lootLike;
    r.corpse = corpse;
    r.itemId = itemId;
    r.state = RC::MakeTask(ToRcKind(kind), startDistance, true, lootLike);
    return r;
}

// What the world shows at one look (every field is a plain read; the core half fills it).
struct Obs
{
    bool botAvailable = true;       // in the world, alive, not teleporting, not on a taxi, still the commander's
    bool teleporting = false;
    bool inCombat = false;
    bool targetPresent = true;
    bool inRange = false;
    float distance = 0.0f;
    bool moving = false;            // the bot is walking
    bool walkOrderFailed = false;   // the engine ran the walk order and it failed
    std::string walkOrderText;
    // the act (an Open on a door or lever)
    bool objectPresent = true;
    uint32_t objectState = 0;       // PackState now
    // the act (a loot window)
    bool watchFound = false;        // the ledger still holds this bot's watch
    bool windowFinished = false;
    bool windowRefused = false;
    uint32_t itemsTaken = 0;        // items the window gave (all of them, or only the ordered one)
    std::string looted;             // RC::FormatLootedItems
};

enum class Do : uint8_t { Wait, OrderWalk, BeginAct, Succeed, Fail };

struct Step
{
    Do what = Do::Wait;
    std::string code;               // Fail: the answer's code
    std::string message;            // Fail: the answer's words; Succeed: the payload ("opened", "7741:1,copper:30")
};

// The act's result is in.
inline bool ActDone(TaskRec const& r, Obs const& o)
{
    if (!r.actStarted)
        return false;
    // The core runs the choice inside the select handler (commands/Commander.cpp TaskActDone: "the act is over at once").
    if (r.kind == Kind::Gossip)
        return true;
    if (r.lootLike)
        return o.watchFound && o.windowFinished;
    return RC::JudgeObjectEffect(o.objectPresent, r.baseline, o.objectState) == RC::ObjectEffect::Reacted;
}

inline Step Failed(char const* code, std::string message)
{
    Step s;
    s.what = Do::Fail;
    s.code = code;
    s.message = std::move(message);
    return s;
}

// The success answer of a finished act.
inline Step Finished(TaskRec const& r, Obs const& o)
{
    if (r.kind == Kind::Gossip)
    {
        Step s;
        s.what = Do::Succeed;
        s.message = r.gossipPicked.empty() ? std::string("gossip") : r.gossipPicked;
        return s;
    }
    if (r.lootLike)
    {
        if (!o.watchFound)
            return Failed(RC::kCodeFailed, "The loot window was lost.");
        if (o.windowRefused)
            return Failed(RC::kCodeFailed, "The server refused the loot window (not this bot's loot under the group rules, or too far).");
        if (r.itemId && !o.itemsTaken)
            return Failed(RC::kCodeFailed, "That item was not in the loot for this bot (group rules, taken, or no room in its bags).");
        Step s;
        s.what = Do::Succeed;
        s.message = o.looted.empty() ? std::string("-") : o.looted;
        return s;
    }
    Step s;
    s.what = Do::Succeed;
    s.message = "opened";
    return s;
}

// One look at a running task, `diffMs` after the last (Commander.cpp StepCommanderTask, on plain data). A refused start of the act is told back with
// `NoteActStart`; the next look tries again while the bot may still act, up to RC::kActTries.
inline Step Advance(TaskRec& r, Obs const& o, uint32_t diffMs)
{
    r.totalMs += diffMs;
    RC::TaskFacts facts;
    facts.botAvailable = o.botAvailable;
    facts.teleporting = o.teleporting;
    facts.actStarted = r.actStarted;
    facts.inCombat = o.inCombat;
    facts.targetPresent = o.targetPresent;
    facts.inRange = o.inRange;
    facts.distance = o.distance;
    facts.actDone = r.state.stage == RC::TaskStage::Act && ActDone(r, o);

    // A used door that vanished (despawn, map change) is no "opened".
    if (o.botAvailable && r.state.stage == RC::TaskStage::Act && r.actStarted && !r.lootLike && r.kind != Kind::Gossip && !o.objectPresent)
        return Failed(RC::kCodeFailed, "The object disappeared while the bot used it.");

    // A refused attempt that may pass the next look.
    if (RC::MayRetryAct(r.state, facts) && !r.actStarted && r.actTries > 0)
    {
        if (r.actTries >= RC::kActTries)
            return Failed(r.lastCode.empty() ? RC::kCodeFailed : r.lastCode.c_str(), r.lastMessage);
        Step s;
        s.what = Do::BeginAct;
        return s;
    }

    if (r.totalMs >= kTaskCapMs && !facts.actDone)
        return Failed(RC::kCodeTimeout, "The task did not finish in " + std::to_string(kTaskCapMs / 1000) + " s.");

    // A goober whose use was delivered and whose own state did not change: the core's handler gives no answer, and the script's effect is on another object
    // (the cannon sets the Iron Clad Door). The use is reported as delivered, in those words (never "opened"); the plan's wait_object step reads the effect.
    if (r.actStarted) r.actMs += diffMs;
    if (r.scriptUse && r.actStarted && !facts.actDone && o.botAvailable && o.objectPresent && r.state.stage == RC::TaskStage::Act && r.actMs >= kScriptSettleMs)
    {
        Step s;
        s.what = Do::Succeed;
        s.message = "used: the object's script answers the use; its effect is read by the plan's next wait step";
        return s;
    }
    RC::TaskVerdict const verdict = RC::AdvanceTask(r.state, facts, diffMs);
    switch (verdict.step)
    {
        case RC::TaskStep::Wait:
        {
            Step s;
            if (r.state.stage == RC::TaskStage::Walk && o.botAvailable)
            {
                if (o.walkOrderFailed)
                    return Failed(RC::kCodeUnreachable, std::string("The bot could not walk to the object (") + o.walkOrderText + ").");
                r.sinceWalkOrderMs += diffMs;
                if (r.sinceWalkOrderMs >= kWalkReorderMs && !o.moving)
                {
                    r.sinceWalkOrderMs = 0;
                    ++r.walkOrders;
                    s.what = Do::OrderWalk;
                }
            }
            return s;
        }
        case RC::TaskStep::BeginAct:
        {
            Step s;
            s.what = Do::BeginAct;
            return s;
        }
        case RC::TaskStep::Succeed:
            return Finished(r, o);
        case RC::TaskStep::Fail:
        {
            std::string code = verdict.code;
            std::string message = verdict.message;
            if (code == RC::kCodeTimeout)
            {
                if (r.state.stage == RC::TaskStage::Act && !r.lastCode.empty() && !r.actStarted)
                {
                    code = r.lastCode;
                    message = r.lastMessage;
                }
                else if (r.kind == Kind::Open && !r.lootLike && r.state.stage == RC::TaskStage::Act)
                {
                    code = RC::kCodeFailed;
                    message = "The object showed no change after the bot used it.";
                }
            }
            return Failed(code.c_str(), message);
        }
    }
    return Step();
}

// The core half tried to start the act: `started` it did; else `final` = no retry (the lock, the loot rights), or a refusal that may pass.
inline void NoteActStart(TaskRec& r, bool started, bool final, std::string const& code, std::string const& message)
{
    ++r.actTries;
    r.actStarted = started;
    if (started)
        return;
    r.lastCode = code;
    r.lastMessage = message;
    if (final)
        r.actTries = RC::kActTries;     // the next look answers with it
}

// A cast the act started (the 5 s Opening of a locked chest) runs inside the act stage, whose budget was read before the cast: the cast time still to run
// is added on top of the result wait, so a chest is not answered "No result came back in time" while its own cast is still running.
// Same rule and numbers as the Turtle glue (commands/Commander.cpp:5112-5146, RemoteCommander::ActBudgetAfterCast).
inline void NoteActCast(TaskRec& r, uint32_t castMs)
{
    r.state.actBudgetMs = RC::ActBudgetAfterCast(r.state, r.lootLike, castMs);
}

// ---- the target search of a by-entry order ----
// A candidate spawn of the entry. The nearest to the plan's point within kEntrySearchYd wins; equal distances go to the lowest key (RC::PickNearestEntry).
struct Candidate
{
    uint64_t key = 0;               // the handle (or a stable number) of the spawn
    float distance = 0.0f;          // from the plan's point
};

inline bool PickEntryTarget(std::vector<Candidate> const& candidates, uint64_t& key)
{
    std::vector<RC::EntryCandidate> list;
    for (Candidate const& c : candidates)
        list.push_back(RC::EntryCandidate{ c.key, c.distance });
    return RC::PickNearestEntry(list, kEntrySearchYd, key);
}

// ---- the answers ----
inline std::string NotFoundText(uint32_t entry)
{
    return "No spawned game object or corpse of entry " + std::to_string(entry) + " lies within " + std::to_string(int(kEntrySearchYd)) + " yd of the plan's point.";
}
}
}
}
