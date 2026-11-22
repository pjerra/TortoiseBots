// Pure parsing and wire-line formatting for the `choice` addon verb (TBF #133 piece c-choice).
// No core headers, so forever/tools/test_addon_choice_policy.cpp tests it standalone.
#pragma once

#include "ai/playerbot/strategy/generic/ClassChoicePolicy.h"
#include "host/TbAddonPure.h"

#include <sstream>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace choice
{
inline constexpr char const* kChoiceUsage =
    "Usage: .bot choice <bot|party|raid> <aura|aspect|curse|poison_mh|poison_oh|sting|pet> <value|auto> | status";

struct ParsedChoice
{
    bool ok = false;
    bool status = false;
    bool scopeIsParty = false;
    int kind = -1;
    std::string scope;
    std::string word2;
    std::string word3;
    std::string intent;
};

inline ParsedChoice ParseChoiceArgs(std::string_view args)
{
    ParsedChoice out;
    std::istringstream input{std::string(addon::pure::Trim(args))};
    input >> out.scope >> out.word2 >> out.word3;
    out.scope = addon::pure::Lower(out.scope);
    out.word2 = addon::pure::Lower(out.word2);
    out.word3 = addon::pure::Lower(out.word3);

    out.status = (out.word2 == "status");
    out.kind = ai::ChoiceKindIndex(out.word2);
    out.intent = out.status ? "choice status" : "choice";
    out.scopeIsParty = (out.scope == "party" || out.scope == "raid");
    out.ok = !out.scope.empty() && (out.status || (out.kind >= 0 && ai::IsChoiceWord(out.kind, out.word3)));
    return out;
}

inline std::string FormatChoiceLine(std::string_view botName, int kind, std::string const& stored,
    std::string const& known)
{
    std::string const& value = stored.empty() ? std::string("auto") : stored;
    return "TBM:CHOICE|" + addon::pure::Safe(botName) + "|" +
        ai::ChoiceKindWord(kind) + "=" + value + ",known=" + known;
}
} // namespace choice
} // namespace TortoiseBots
