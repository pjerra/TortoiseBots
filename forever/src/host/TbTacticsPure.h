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

// One bot's probe: the actions above that passed every check for this target now. `named` is the bot the order names: when any probe is
// named, only named probes are considered (TB ResolveInterruptExecutor), so the party picked by name is never overridden.
struct InterruptProbe
{
    std::string bot;
    bool named = false;
    std::vector<std::string> actions;
};

struct InterruptPick
{
    bool found = false;
    size_t index = 0;
    std::string action;
};

// The first able bot in the list's own order (the party order), and within it the first action of kInterruptActions, so the answer does not
// depend on the order the caller happened to test the spells in.
inline InterruptPick PickInterrupt(std::vector<InterruptProbe> const& probes)
{
    bool anyNamed = false;
    for (InterruptProbe const& probe : probes)
        anyNamed = anyNamed || probe.named;
    for (size_t i = 0; i < probes.size(); ++i)
    {
        if (anyNamed && !probes[i].named)
            continue;
        for (char const* action : kInterruptActions)
            for (std::string const& known : probes[i].actions)
                if (known == action)
                {
                    InterruptPick pick;
                    pick.found = true;
                    pick.index = i;
                    pick.action = action;
                    return pick;
                }
    }
    return InterruptPick();
}
}
}
