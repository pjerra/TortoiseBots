// Tactical orders, M1: the core-free half. The command handlers, the queued order and the engine hook turn game state into
// plain values here, so tools/test_tactics_pure.cpp can prove the grammar without a world.
//
// M1: the six verbs of the round, the engage seconds (0-60, empty = the default 4) and the stance word (`turn` is `turnback`).
// M2: `interrupt` and the probe order the picked bot's action follows. The later rounds append their own functions next to these.
// M4: `PickCcBot`, the whole-party crowd-control pick over the run's own table (DungeonRun::kRunCc).
#pragma once

#include "ai/playerbot/policy/combat/CcPermissionPolicy.h"
#include "runtime/DungeonRunPolicy.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace tactics
{
// ai::kEngageDefaultDelaySeconds (PullJoinWindowPolicy.h). A static_assert in TbTactics.cpp keeps the two equal.
constexpr unsigned kEngageDefaultSeconds = 4;

// The verbs the tactical branch of `.tb order` / `.tb cmd` handles. Case matters (both callers lower the word).
inline bool IsTacticalVerb(std::string const& verb)
{
    return verb == "stop" || verb == "flee" || verb == "guard" || verb == "stance" || verb == "engage" || verb == "rebuff" ||
        verb == "interrupt" || verb == "aoe" || verb == "autocc" || verb == "sap" || verb == "loot" || verb == "corpserun";
}

// The toggle verbs (`aoe|autocc|sap|loot`) and the corpse run, normalised: `auto cc` is the two-word form of `autocc`, and
// `corpse run` is the two-word form of `corpserun` (section 4.3). A caller that split the words already can join them back
// or pass the first two words straight here.
inline std::string NormaliseTacticalVerb(std::string const& first, std::string const& second = std::string())
{
    if (second == "cc" && first == "auto")
        return "autocc";
    if (second == "run" && first == "corpse")
        return "corpserun";
    return first;
}

// M3: the four toggles and their state operations. `Flip` is a bare verb (each bot flips its own state); `Status` reads
// without queuing; `On`/`Off` set the absolute state. The op is parsed from the first argument after the verb.
enum class ToggleOp
{
    Flip,
    On,
    Off,
    Status
};

inline ToggleOp ParseToggle(std::string const& text, bool& ok)
{
    std::string word;
    word.reserve(text.size());
    for (char const c : text)
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    ok = true;
    if (word.empty())
        return ToggleOp::Flip;
    if (word == "on")
        return ToggleOp::On;
    if (word == "off")
        return ToggleOp::Off;
    if (word == "status")
        return ToggleOp::Status;
    ok = false;
    return ToggleOp::Flip;
}

// The wanted state of one bot after the op against its current state. A flip inverts; On/Off set; Status never changes
// anything (the caller should not call this for a read, but the answer is the current state).
inline bool ToggleWanted(ToggleOp op, bool current)
{
    if (op == ToggleOp::On)
        return true;
    if (op == ToggleOp::Off)
        return false;
    if (op == ToggleOp::Flip)
        return !current;
    return current;
}

// The `.tb cmd` aggregate answer for a party: "on" when every bot reads on, "off" when every bot reads off, "mixed"
// otherwise. An empty list is "off" (no bot to call on).
inline std::string AggregateState(std::vector<bool> const& states)
{
    if (states.empty())
        return "off";
    bool const first = states.front();
    for (bool const state : states)
        if (state != first)
            return "mixed";
    return first ? "on" : "off";
}

// M3: the strategy name each toggle reads or writes (section 4.3), and the state (combat or non-combat) it lives in.
// `loot` lives in non-combat like `no sap`; the others are combat.
inline char const* ToggleStrategy(std::string const& verb)
{
    if (verb == "aoe")
        return "no aoe";
    if (verb == "autocc")
        return "auto cc";
    if (verb == "sap")
        return "no sap";
    if (verb == "loot")
        return "loot";
    return "";
}

// `engage [seconds]`: empty means the default delay; digits only; more than 60 seconds is refused (the pull window's own limit).
inline bool ParseEngageSeconds(std::string const& text, unsigned& out)
{
    if (text.empty())
    {
        out = kEngageDefaultSeconds;
        return true;
    }
    unsigned seconds = 0;
    for (char const c : text)
    {
        if (c < '0' || c > '9')
            return false;
        seconds = seconds * 10 + unsigned(c - '0');
        if (seconds > 60)
            return false;
    }
    out = seconds;
    return true;
}

// `stance <word>`: near, tank, turnback (or `turn`), behind. The value is case-insensitive; the canonical word (the StanceValue name) is
// written to `out`, so `turn` becomes `turnback` and `Behind` becomes `behind`.
inline bool NormaliseStance(std::string const& text, std::string& out)
{
    std::string word;
    word.reserve(text.size());
    for (char const c : text)
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (word == "turn")
        word = "turnback";
    if (word != "near" && word != "tank" && word != "turnback" && word != "behind")
        return false;
    out = word;
    return true;
}

// The interrupt actions in TB's probe order (BotCommandContext.cpp:540-552), without the pet-only `spell lock` and the unported
// `repentance`. A class context registers its own action; the spell itself still has to interrupt on Forever (IsInterruptableSpellCasting).
inline char const* const kInterruptActions[] = {
    "counterspell", "silence", "kick", "pummel", "shield bash", "bash", "hammer of justice", "earth shock", "death coil"
};

// One bot's probe: the actions above that passed every check for this target now, with the spell range ignored (`actions`), and
// the subset castable from where the bot stands (`inRange`). `named` is the bot the order names: when any probe is named, only named
// probes are considered (TB ResolveInterruptExecutor), so the party picked by name is never overridden.
struct InterruptProbe
{
    std::string bot;
    bool named = false;
    std::vector<std::string> actions;
    std::vector<std::string> inRange;
};

struct InterruptPick
{
    bool found = false;
    size_t index = 0;
    std::string action;
    bool inRange = false;   // the pick can land from where it stands; false: no bot was in range, the queued action walks (D6)
};

// A bot that can interrupt from where it stands wins over one that would have to walk: the first in-range bot in the list's own order
// (the party order), else the first able bot with range ignored. Within a bot the first action of kInterruptActions wins, so the answer
// does not depend on the order the caller happened to test the spells in.
inline InterruptPick PickInterrupt(std::vector<InterruptProbe> const& probes)
{
    bool anyNamed = false;
    for (InterruptProbe const& probe : probes)
        anyNamed = anyNamed || probe.named;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < probes.size(); ++i)
        {
            if (anyNamed && !probes[i].named)
                continue;
            std::vector<std::string> const& known = pass == 0 ? probes[i].inRange : probes[i].actions;
            for (char const* action : kInterruptActions)
                for (std::string const& name : known)
                    if (name == action)
                    {
                        InterruptPick pick;
                        pick.found = true;
                        pick.index = i;
                        pick.action = action;
                        pick.inRange = pass == 0;
                        return pick;
                    }
        }
    return InterruptPick();
}

// An interrupt order does not end at its first look. The bot's own AI starts a cast of its own every tick, the GCD or its facing can refuse
// the first attempt, and the engine's action pick (the `interrupt target` value) only sees a caster that is already a possible attack target.
// So the order is retried at every engine tick for as long as the target still casts, up to this many ms from the order's first run: the
// order ends `ok` only when the class's interrupt spell was really requested.
constexpr unsigned kInterruptWindowMs = 3500;

enum class InterruptTick
{
    Done,       // the spell cast was requested: the order is ok
    Wait,       // not yet, try again at the next engine tick
    NoTarget,   // the target is gone or dead: fail:no-target
    NotCasting, // the target finished its cast before the interrupt could start: fail:not-casting
    TimedOut    // the target still casts and nothing could be cast inside the window: fail:interrupt
};

inline InterruptTick NextInterruptTick(bool targetPresent, bool targetAlive, bool targetCasting, bool castRequested, unsigned ageMs)
{
    if (castRequested)
        return InterruptTick::Done;
    if (!targetPresent || !targetAlive)
        return InterruptTick::NoTarget;
    if (!targetCasting)
        return InterruptTick::NotCasting;
    return ageMs >= kInterruptWindowMs ? InterruptTick::TimedOut : InterruptTick::Wait;
}

// The order's result text for a tick (Wait is `pending:interrupt`, which the order shows until a later tick settles it).
inline char const* InterruptTickResult(InterruptTick tick)
{
    switch (tick)
    {
        case InterruptTick::Done: return "ok";
        case InterruptTick::NoTarget: return "fail:no-target";
        case InterruptTick::NotCasting: return "fail:not-casting";
        case InterruptTick::TimedOut: return "fail:interrupt";
        default: return "pending:interrupt";
    }
}

// ---- M4: `cc <mark> <guid>` for the whole party (D12) ----
// One bot as the pick sees it. `known` is the CcBit of every run family the bot has the spell for, `denied` the owner's deny mask in
// ai::CcPermissions kinds (CcDeniedForOwner), `hasCcKit` whether the bot's combat kit carries the `cc` strategy (today the mage's and the
// rogue's; #123 adds the rest, and this pick needs no change for them).
struct CcCandidate
{
    std::string name;
    bool alive = true;
    bool inCombat = false;
    bool hasCcKit = false;
    std::uint32_t known = 0;
    std::uint32_t denied = 0;
};

struct CcPick
{
    bool found = false;
    size_t index = 0;                                   // into the candidate list
    DungeonRun::CcFamily family = DungeonRun::CcFamily::None;
    std::string familyToken;                            // the token the `cc` order carries (CcFamilyToken)
    std::string reasons;                                // when !found: `name:reason;...` (dead, no-cc-kit, no-fitting-spell, denied, in-combat)
};

// The family a token names ("polymorph", "shackleundead", ...): None for "", "none" and anything that is not a family of the run's table.
inline DungeonRun::CcFamily CcFamilyFromToken(std::string const& text)
{
    std::string const token = DungeonRun::CcFamilyTokenNorm(text);
    if (token.empty() || token == "none")
        return DungeonRun::CcFamily::None;
    for (DungeonRun::RunCcSpell const& row : DungeonRun::kRunCc)
        if (DungeonRun::CcFamilyTokenNorm(row.spell) == token)
            return row.family;
    return DungeonRun::CcFamily::None;
}

// The bot and the family for a `cc <mark> <guid>` with no bot named. Over every (bot, family row) pair of the run's table (the same one the run's
// automatic crowd control reads) the best one wins: tier A before S before R, then the table order (polymorph before sap), then the party order.
// A row qualifies when it is an automatic pick (Freezing Trap is not), the target's creature type fits it (CcFits), the bot knows the spell and
// carries the `cc` kit, and the owner's permissions do not deny its kind. A `prePull` row (Sap) also needs the target and the bot out of combat.
// `only` (not None) restricts the pick to that family. With no pick `reasons` names each bot's reason.
inline CcPick PickCcBot(std::vector<CcCandidate> const& cands, std::uint32_t creatureType, bool targetEngaged,
    DungeonRun::CcFamily only = DungeonRun::CcFamily::None)
{
    CcPick best;
    int bestRank = 0;
    std::string reasons;
    for (size_t ci = 0; ci < cands.size(); ++ci)
    {
        CcCandidate const& c = cands[ci];
        char const* reason = nullptr;
        if (!c.alive)
            reason = "dead";
        else if (!c.hasCcKit)
            reason = "no-cc-kit";
        else
        {
            bool fits = false, denied = false;
            bool eligible = false;
            for (size_t ri = 0; ri < sizeof(DungeonRun::kRunCc) / sizeof(DungeonRun::kRunCc[0]); ++ri)
            {
                DungeonRun::RunCcSpell const& row = DungeonRun::kRunCc[ri];
                if (!row.autoPick || (only != DungeonRun::CcFamily::None && row.family != only))
                    continue;
                if (!(c.known & DungeonRun::CcBit(row.family)) || !DungeonRun::CcFits(row.family, creatureType))
                    continue;
                fits = true;
                if (c.denied & ai::CcPermissions::KindFromSpellName(row.spell))
                {
                    denied = true;
                    continue;
                }
                if (row.prePull && (targetEngaged || c.inCombat))
                    continue;
                eligible = true;
                int const rank = DungeonRun::RunCcTierRank(row.tier) * 100 + int(ri);
                if (!best.found || rank < bestRank)
                {
                    best.found = true;
                    best.index = ci;
                    best.family = row.family;
                    bestRank = rank;
                }
            }
            if (!eligible)
                reason = !fits ? "no-fitting-spell" : denied ? "denied" : "in-combat";
        }
        if (reason)
            reasons += (reasons.empty() ? "" : ";") + c.name + ":" + reason;
    }
    if (best.found)
        best.familyToken = DungeonRun::CcFamilyToken(best.family);
    else
        best.reasons = reasons;
    return best;
}
}
}
