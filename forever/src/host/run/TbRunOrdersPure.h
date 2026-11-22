// Slice 6, package O6 (orders): the PURE half of the run order executor (forever/docs/RUN-S6-O-NOTES.md). Plain data in, plain data out: no core and no
// legacy type, so forever/tools/test_run_orders.cpp runs it standalone, next to the run leader's own header (runtime/DungeonRunPolicy.h) to prove that the
// refusal texts are the ones the policy parses (UnreachableAnswer, ReactTooFarAnswer, LeadNoProgressRefusal, DirectStragglerName, the invalid-target prefix).
//
// What is here: the order parser (P's strings), the refusal/ACK texts in the Turtle shape, the move and direct-step verdicts (the Turtle judge's own pure
// functions from runtime/RemoteCommanderPolicy.h, fed with the facts the core half reads), and the tracker of accepted orders whose queued work is judged
// when the bots' engines have run it.
#pragma once

#include "TbRunTasksPure.h"
#include "ai/playerbot/PullOpeningPolicy.h"
#include "runtime/CcOrderFamily.h"
#include "runtime/RemoteCommanderPolicy.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
namespace orders
{
enum class Verb : uint8_t
{
    Invalid,        // not an order of the grammar (bad syntax or an unknown word)
    Move,           // move x y z [direct]
    Walk,           // walk <bot> x y z
    Pull,           // pull <handle> [body]
    Pullback,       // pullback <handle>
    Attack,         // attack <handle> [bot]
    Focus,          // focus <handle>
    Rest,           // rest
    ReviveParty,     // revive party
    Stay,           // stay          (not emitted by the policy: the run's stop and the glue use it)
    Follow,         // follow        (same)
    Hold,           // hold          (+passive in combat and non-combat)
    Summon,         // summon <bot>: refused by decision D3
    Cc,             // cc <mark> <handle> <bot> [family]            (G8: h8::ApplyCcOrder)
    CcClear,        // cc clear <bot> [<mark> <handle>]             (G8: h8::ClearCcOrder)
    Open,           // S9 (O9): open <object> <bot>: a door, a lever, a chest (TbRunTasks)
    Loot,           // S9 (O9): loot <corpse or object> <bot> [item]
    LootMethod,     // S9 (O9): lootmethod <ffa|group|rr|master|nbg> [threshold]
    Use,            // S9 (O9): use <item> <bot>: parsed, refused `unsupported` by the executor (the item-use path is not ported)
    Gossip,         // WC: gossip <npc entry> <bot> <option>: the plan's own gossip step (TbRunTasks, GossipOrder)
    Unsupported,    // a verb of the grammar the module does not carry out on ForeverCore
};

struct Parsed
{
    Verb verb = Verb::Invalid;
    std::string word;           // the verb word, lower case
    float x = 0, y = 0, z = 0;  // move, walk
    bool direct = false;        // move ... direct
    std::string bot;            // walk, attack, summon
    uint64_t handle = 0;        // pull, pullback, attack, focus: the handle of ReadFacts' unit guid (0 when a 32-hex guid)
    std::string targetToken;    // pull, pullback, attack, focus: the raw target token (handle or 32-hex guid)
    bool body = false;          // pull <handle> body
    std::string pullOption;     // pull, pullback: the canonical option text the h3 order parses (`5 ranged`, `body`, empty)
    std::string mark;           // cc, cc clear: the raid mark word, lower case ("" for a `cc clear <bot>`)
    std::string family;         // cc: the family token the policy named after the bot ("" for the plan's own orders)
    tasks::TaskOrder task;      // S9 (O9): open, loot, lootmethod, use
};

inline bool IsGuidHex32(std::string const& token)
{
    if (token.size() != 32)
        return false;
    bool nonZero = false;
    for (char c : token)
    {
        bool const hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
        if (c != '0')
            nonZero = true;
    }
    return nonZero;
}

// The eight raid mark words of a `cc` order (skull parses and is refused by the host: it is the party's kill mark, never a crowd-control mark).
inline bool IsRaidMarkWord(std::string const& word)
{
    for (char const* known : { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" })
        if (word == known)
            return true;
    return false;
}

// The code of a refused `cc` / `cc clear` answer from the host's own code (h8::ApplyCcOrder / ClearCcOrder): the words the policy and `.tb run` know.
inline std::string CcAnswerCode(std::string const& hostCode)
{
    if (hostCode == "bad-mark" || hostCode == "bad-family")
        return "invalid";
    if (hostCode == "no-party")
        return "not-bound";
    if (hostCode == "target-not-found" || hostCode == "bad-target" || hostCode == "target-dead" || hostCode == "not-attackable")
        return "invalid-target";
    if (hostCode == "no-engine" || hostCode == "engine-not-built")
        return "no-engine";
    return "failed";
}

// The canonical option text of a parsed pull (TB PullOptionText, Action.cpp:240-249): "[seconds][ ranged|body]". The h3 order parses this
// same text on the bot's tick, so the command and the executor read one grammar.
inline std::string PullOptionText(ai::PullOrderOption const& option)
{
    std::string text = option.hasSeconds ? std::to_string(option.seconds) : std::string();
    if (option.ranged)
        text += text.empty() ? "ranged" : " ranged";
    if (option.body)
        text += text.empty() ? "body" : " body";
    return text;
}

// The text after the legacy `.bot cmd`: P's own grammar. A refused parse sets `code` (always "invalid" or "unsupported") and the reason in the legacy words.
inline bool ParseOrder(std::string const& order, Parsed& out, std::string& code, std::string& message)
{
    out = Parsed();
    std::vector<std::string> words = RemoteCommander::SplitOrderArgs(order);
    if (words.empty())
    {
        code = "invalid";
        message = "Empty order.";
        return false;
    }
    std::string verb = words[0];
    for (char& c : verb)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    out.word = verb;
    std::vector<std::string> const args(words.begin() + 1, words.end());
    auto const invalid = [&](std::string const& usage)
    {
        code = "invalid";
        message = "Usage: " + usage;
        return false;
    };
    auto const handleOf = [&](std::string const& token)
    {
        uint64_t raw = 0;
        return RemoteCommander::ParseGuid(token, raw) ? raw : uint64_t(0);
    };
    auto const targetOf = [&](std::string const& token, uint64_t& handleOut, std::string& tokenOut)
    {
        uint64_t raw = 0;
        if (RemoteCommander::ParseGuid(token, raw) && raw != 0)
        {
            handleOut = raw;
            tokenOut = token;
            return true;
        }
        if (IsGuidHex32(token))
        {
            handleOut = 0;
            tokenOut = token;
            return true;
        }
        return false;
    };
    if (verb == "move")
    {
        RemoteCommander::MoveOrder request;
        std::string rest;
        for (size_t i = 0; i < args.size(); ++i)
            rest += (i ? " " : "") + args[i];
        if (!RemoteCommander::ParseMoveOrder(rest, request))
            return invalid("move <x> <y> <z> [direct] (map coordinates on the lead bot's map)");
        out.verb = Verb::Move;
        out.x = request.point.x;
        out.y = request.point.y;
        out.z = request.point.z;
        out.direct = request.direct;
        return true;
    }
    if (verb == "walk")
    {
        std::string rest;
        for (size_t i = 1; i < args.size(); ++i)
            rest += (i > 1 ? " " : "") + args[i];
        RemoteCommander::MovePoint point;
        if (args.size() != 4 || !RemoteCommander::ParseMovePoint(rest, point))
            return invalid("walk <bot> <x> <y> <z>");
        out.verb = Verb::Walk;
        out.bot = args[0];
        out.x = point.x;
        out.y = point.y;
        out.z = point.z;
        return true;
    }
    if (verb == "pull" || verb == "pullback")
    {
        std::string const usage = "pull|pullback <guid> [seconds 0-60] [ranged|body]";
        if (args.empty() || !targetOf(args[0], out.handle, out.targetToken))
            return invalid(usage);
        out.verb = verb == "pull" ? Verb::Pull : Verb::Pullback;
        std::string optionText;
        for (size_t i = 1; i < args.size(); ++i)
            optionText += (i > 1 ? " " : "") + args[i];
        ai::PullOrderOption option;
        if (!ai::ParsePullOrderOption(optionText, option))
            return invalid(usage);
        out.body = option.body;
        out.pullOption = PullOptionText(option);
        return true;
    }
    if (verb == "attack")
    {
        if (args.empty() || args.size() > 2 || !targetOf(args[0], out.handle, out.targetToken))
            return invalid("attack <guid> [bot]");
        out.verb = Verb::Attack;
        if (args.size() == 2)
            out.bot = args[1];
        return true;
    }
    if (verb == "focus")
    {
        if (args.size() != 1 || !targetOf(args[0], out.handle, out.targetToken))
            return invalid("focus <guid>");
        out.verb = Verb::Focus;
        return true;
    }
    if (verb == "rest" || verb == "stay" || verb == "follow" || verb == "hold")
    {
        if (!args.empty())
            return invalid(verb);
        out.verb = verb == "rest" ? Verb::Rest : verb == "stay" ? Verb::Stay : verb == "follow" ? Verb::Follow : Verb::Hold;
        return true;
    }
    if (verb == "revive")
    {
        if (args.size() == 1 && (args[0] == "party" || args[0] == "Party" || args[0] == "PARTY"))
        {
            out.verb = Verb::ReviveParty;
            return true;
        }
        code = "unsupported";
        message = "Only `revive party` is carried out: a single bot's revive is not ported.";
        return false;
    }
    if (verb == "summon")
    {
        if (args.size() != 1)
            return invalid("summon <bot>");
        out.verb = Verb::Summon;
        out.bot = args[0];
        return true;
    }
    if (verb == "cc")
    {
        auto const lower = [](std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        if (!args.empty() && lower(args[0]) == "clear")
        {
            // cc clear <bot> [<mark> <handle>]: the bot's crowd-control mark goes; the icon only while it still holds that unit.
            if (args.size() != 2 && args.size() != 4)
                return invalid("cc clear <bot> [<mark> <guid>]");
            out.verb = Verb::CcClear;
            out.bot = args[1];
            if (args.size() == 4)
            {
                out.mark = lower(args[2]);
                if (!IsRaidMarkWord(out.mark) || !handleOf(args[3]))
                    return invalid("cc clear <bot> [<mark> <guid>]");
                out.handle = handleOf(args[3]);
            }
            return true;
        }
        // cc <mark> <handle> <bot> [family]: the run names the bot always (the Turtle command's selected-bot form has no run behind it).
        if (args.size() < 3 || args.size() > 4)
            return invalid("cc <mark> <guid> <bot> [family]");
        out.mark = lower(args[0]);
        if (!IsRaidMarkWord(out.mark) || !handleOf(args[1]))
            return invalid("cc <mark> <guid> <bot> [family]");
        if (args.size() == 4 && !DungeonRun::CcOrderFamilyKnown(args[3]))
            return invalid("cc <mark> <guid> <bot> [family]");
        out.verb = Verb::Cc;
        out.handle = handleOf(args[1]);
        out.bot = args[2];
        if (args.size() == 4)
            out.family = DungeonRun::CcFamilyTokenNorm(args[3]);
        return true;
    }
    // S9 (O9): the four task verbs have a grammar and an executor (TbRunTasks); `use` is parsed and then refused by the executor with its own reason.
    if (verb == "loot" || verb == "open" || verb == "lootmethod" || verb == "use" || verb == "gossip")
    {
        if (!tasks::ParseTaskOrder(verb, args, out.task, code, message))
            return false;
        out.verb = verb == "loot" ? Verb::Loot : verb == "open" ? Verb::Open : verb == "lootmethod" ? Verb::LootMethod
                 : verb == "gossip" ? Verb::Gossip : Verb::Use;
        out.bot = out.task.bot;
        return true;
    }
    for (char const* known : { "escape", "rescue", "fetch", "return", "rejoin", "ritual", "state", "enter" })
        if (verb == known)
        {
            out.verb = Verb::Unsupported;
            code = "unsupported";
            message = "The order `" + verb + "` is not carried out on ForeverCore.";
            return false;
        }
    code = "invalid";
    message = "Usage: attack <guid> [bot] / pull|pullback <guid> [seconds 0-60] [ranged|body] / focus <guid> / cc <mark> <guid> <bot> [family] / cc clear <bot> [<mark> <guid>] / move <x> <y> <z> [direct] / walk <bot> <x> <y> <z> / stay / follow / hold / rest / revive party / open <object> <bot> / loot <corpse or object> <bot> [item] / lootmethod <ffa|group|rr|master|nbg> [threshold]";
    return false;
}

// ---- the answer texts, in the Turtle shape (commands/CommandShared.cpp:75-96) ----

inline std::string ProtocolSafe(std::string value)
{
    for (char& c : value)
        if (c == '|' || c == '\r' || c == '\n')
            c = ' ';
    return value;
}

inline std::string RefusalText(std::string const& code, std::string const& message)
{
    return code + ": " + ProtocolSafe(message);
}

inline std::string AckText(std::string const& scope, uint32_t count)
{
    return ProtocolSafe(scope) + " " + std::to_string(count);
}

// ---- verdicts ----

struct Judged
{
    bool ok = true;
    std::string code;
    std::string text;           // RefusalText(...) when !ok
};

inline Judged Refused(std::string const& code, std::string const& message)
{
    Judged j;
    j.ok = false;
    j.code = code;
    j.text = RefusalText(code, message);
    return j;
}

// CommanderMove's verdict and the three texts (Commander.cpp:3636-3657): the point is refused when it is too far for the stay strategy (ReactDistance), has
// no navmesh path, or is not on walkable ground. The no-progress mark rides on an unreachable answer whose path ends where the mover stands.
inline Judged JudgeMove(RemoteCommander::MoveFacts const& facts)
{
    RemoteCommander::MoveVerdict const verdict = RemoteCommander::CheckMove(facts);
    if (verdict == RemoteCommander::MoveVerdict::Ok)
        return Judged();
    std::string message;
    if (verdict == RemoteCommander::MoveVerdict::TooFar)
        message = "The point is more than " + std::to_string(static_cast<uint32_t>(facts.maxDistance)) +
            " yd (AiPlayerbot.ReactDistance) from the lead bot. Move in shorter legs.";
    else if (verdict == RemoteCommander::MoveVerdict::NoPath)
    {
        message = "The lead bot has no walkable path to that point (no navmesh route reaches it).";
        if (RemoteCommander::NoProgressFromLead(facts))
            message += std::string(" Every path from the lead bot ends where it stands") + ".";
    }
    else
        message = "The point is not on walkable ground (the path ends more than 5 yd from it).";
    return Refused(RemoteCommander::MoveVerdictCode(verdict), message);
}

// One follower of a direct step as the core half measured it.
struct DirectFollower
{
    std::string name;
    bool alive = true;
    float horizontalToLead = 0.0f;      // 2D, the follower to the lead's spot
    float verticalToLead = 0.0f;
    bool seesLead = false;
    bool pathComplete = false;          // a complete navmesh path from the follower to the point's floor
    float pathLength = -1.0f;
};

// CommanderMoveDirect's checks for the LEAD (Commander.cpp:3810-3865) and the followers' reach (see the notes: the followers only `follow`, they are not
// walked along the lead's line, so each must have a complete navmesh path to the floor under the point). `groundValid` false = no floor read.
inline Judged JudgeDirect(std::string const& leadName, float horizontal, float vertical, bool groundValid, float groundZ, float pointZ, bool leadSees,
    std::vector<DirectFollower> const& followers)
{
    RemoteCommander::DirectVerdict const step = RemoteCommander::CheckDirectStep(horizontal, vertical);
    if (step != RemoteCommander::DirectVerdict::Ok)
        return Refused(RemoteCommander::DirectVerdictCode(step),
            "The point is more than " + std::to_string(static_cast<uint32_t>(RemoteCommander::kDirectMaxDistance)) + " yd (or " +
            std::to_string(static_cast<uint32_t>(RemoteCommander::kDirectMaxVertical)) + " yd up or down) from " + leadName +
            ". A direct step is short: walk closer first.");
    if (!groundValid || RemoteCommander::CheckDirectGround(groundZ, pointZ) != RemoteCommander::DirectVerdict::Ok)
        return Refused(RemoteCommander::DirectVerdictCode(RemoteCommander::DirectVerdict::NoGround),
            "The point has no ground within 3 yd below it (or 0.5 yd above it).");
    if (RemoteCommander::CheckDirectLos(leadSees) != RemoteCommander::DirectVerdict::Ok)
        return Refused(RemoteCommander::DirectVerdictCode(RemoteCommander::DirectVerdict::NoLos),
            leadName + " has no line of sight to the point. A direct step does not cut through walls: walk the lead bot to where it sees it first.");
    // The follower the step is refused for: a dead one first (it is the one not with the party), else the one farthest from the lead (as T names it).
    std::string refusedName;
    float refusedYd = -1.0f;
    bool refusedDead = false, refused = false;
    for (DirectFollower const& f : followers)
    {
        if (!f.alive)
        {
            if (!refusedDead)
            {
                refusedName = f.name;
                refusedDead = true;
            }
            continue;
        }
        if (f.pathComplete)
            continue;
        refused = true;
        if (!refusedDead && f.horizontalToLead > refusedYd)
        {
            refusedYd = f.horizontalToLead;
            refusedName = f.name;
        }
    }
    if (refused)
        return Refused(RemoteCommander::DirectVerdictCode(RemoteCommander::DirectVerdict::TooFar),
            refusedName + " is not with " + leadName + (refusedDead ? " (dead)" : "") +
            " (no complete navmesh path to the point: a follower of a direct step is not walked along the lead bot's line on this core). "
            "A direct step needs the whole party able to walk to its end: gather the party on the far side first.");
    return Judged();
}

// The target of a unit order (attack, focus), as RemoteCommander::CheckOrderTarget judges it, in the legacy words.
inline Judged JudgeTarget(RemoteCommander::TargetFacts const& facts)
{
    RemoteCommander::TargetVerdict const verdict = RemoteCommander::CheckOrderTarget(facts);
    if (verdict == RemoteCommander::TargetVerdict::Ok)
        return Judged();
    return Refused(RemoteCommander::TargetVerdictCode(verdict), RemoteCommander::TargetVerdictText(verdict));
}

// ---- the tracker of accepted orders ----
// An order is accepted when every bot it needs passed the host's pre-flight and was queued. The result of each queued order is known when the bot's
// engine ticks (h3::LastOrder: `queued` until then, `ok` or `fail:<code>` after). The core half records one slot per queued bot; `decisive` marks the
// bots whose result decides the order (the lead for a move or a pull, the named bot for a walk or an attack; every bot of a party-wide order, where one
// success is enough, as T's "No bot of the group accepted the order").
constexpr uint32_t kPendingExpiryMs = 20000;     // a queued order the bot's engine did not run within this long counts as failed (the host expires at 15 s on the next tick)
constexpr size_t kMaxPending = 32;

template <class Key>
struct Slot
{
    Key bot{};
    uint32_t seq = 0;
    bool decisive = true;
};

template <class Key>
struct PendingOrder
{
    std::string order;          // the string the policy sent
    uint32_t queuedMs = 0;
    std::vector<Slot<Key>> slots;
};

enum class SlotState : uint8_t { Queued, Ok, Failed, Superseded };

struct SlotResult
{
    SlotState state = SlotState::Queued;
    std::string result;         // the host's result text when Failed ("fail:<code>")
};

// One look at a pending order. Returns true when it is settled (drop it); `failure` is then non-empty exactly when the order failed: no decisive bot
// carried it out and at least one reported a failure (or never ran it). A decisive success settles it as done at once. A superseded slot (a later order of
// the same bot replaced its result) says nothing.
template <class Key, class Lookup>
inline bool Settle(PendingOrder<Key> const& pending, uint32_t nowMs, Lookup lookup, std::string& failure)
{
    failure.clear();
    size_t waiting = 0, failed = 0;
    std::string firstFailure;
    for (Slot<Key> const& slot : pending.slots)
    {
        if (!slot.decisive)
            continue;
        SlotResult r = lookup(slot.bot, slot.seq);
        if (r.state == SlotState::Queued && nowMs - pending.queuedMs > kPendingExpiryMs)
        {
            r.state = SlotState::Failed;
            r.result = "fail:expired";
        }
        switch (r.state)
        {
            case SlotState::Ok:
                return true;
            case SlotState::Queued:
                ++waiting;
                break;
            case SlotState::Failed:
                if (!failed++)
                    firstFailure = r.result;
                break;
            case SlotState::Superseded:
                break;
        }
    }
    if (waiting)
        return false;
    if (failed)
        failure = RefusalText("failed", "the bot's engine could not carry the order out (" + firstFailure + ")");
    return true;
}
}
}
}
