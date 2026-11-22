// Tactical orders, M1: the core-free half. The command handlers, the queued order and the engine hook turn game state into
// plain values here, so tools/test_tactics_pure.cpp can prove the grammar without a world.
//
// M1: the six verbs of the round, the engage seconds (0-60, empty = the default 4) and the stance word (`turn` is `turnback`).
// M2: `interrupt` and the probe order the picked bot's action follows. The later rounds append their own functions next to these.
#pragma once

#include <cctype>
#include <cstddef>
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
        verb == "interrupt";
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
}
}
