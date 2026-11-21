// Tactical orders, M1: the core-free half. The command handlers, the queued order and the engine hook turn game state into
// plain values here, so tools/test_tactics_pure.cpp can prove the grammar without a world.
//
// M1: the six verbs of the round, the engage seconds (0-60, empty = the default 4) and the stance word (`turn` is `turnback`). The later
// rounds append their own functions next to these.
#pragma once

#include <cctype>
#include <string>

namespace TortoiseBots
{
namespace tactics
{
// ai::kEngageDefaultDelaySeconds (PullJoinWindowPolicy.h). A static_assert in TbTactics.cpp keeps the two equal.
constexpr unsigned kEngageDefaultSeconds = 4;

// The verbs the M1 branch of `.tb order` / `.tb cmd` handles: stop, flee, guard, stance, engage, rebuff. Case matters (both callers lower the word).
inline bool IsTacticalVerb(std::string const& verb)
{
    return verb == "stop" || verb == "flee" || verb == "guard" || verb == "stance" || verb == "engage" || verb == "rebuff";
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
}
}
