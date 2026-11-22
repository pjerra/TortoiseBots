// Core-free rule for the `blessing` addon verb (forever/src/host/TbAddonBlessing.cpp).
// Mirrors TortoiseBots/commands/Blessing.cpp:118-289 and ai/playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h:
// parses `<scope> <class|player|status> ...`, formats `TBM:BLESSING|<bot>|...` lines and ACK details.
// No core or legacy-engine includes; tested standalone by forever/tools/test_addon_blessing.cpp.
#pragma once

#include "playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"

#include <array>
#include <sstream>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace blessing
{

inline char const* BlessingUsage()
{
    return "Usage: .bot blessing <bot|party|raid> <warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid> "
           "<might|wisdom|kings|salvation|sanctuary|light|auto> | "
           ".bot blessing <bot|party|raid> player <name> <might|wisdom|kings|salvation|sanctuary|light|auto> | status";
}

struct ParsedBlessingArgs
{
    bool valid = false;
    bool status = false;
    bool playerMode = false;
    bool scopeIsParty = false;
    std::string scope;
    std::string word2;
    std::string targetName;
    std::string blessingWord;
    int classIndex = -1;
};

inline ParsedBlessingArgs ParseBlessingArgs(std::string_view args)
{
    ParsedBlessingArgs out;
    std::istringstream input{std::string(args)};
    std::string word3;
    std::string word4;
    std::string extra;
    input >> out.scope >> out.word2 >> word3 >> word4 >> extra;

    out.scope = ai::BlessingOrderLower(out.scope);
    out.word2 = ai::BlessingOrderLower(out.word2);
    word4 = ai::BlessingOrderLower(word4);

    out.status = (out.word2 == "status");
    out.playerMode = (out.word2 == "player");
    out.scopeIsParty = (out.scope == "party" || out.scope == "raid");
    out.targetName = out.playerMode ? word3 : std::string();
    out.blessingWord = out.playerMode ? word4 : ai::BlessingOrderLower(word3);
    out.classIndex = out.playerMode ? -1 : ai::BlessingOrderClassIndex(out.word2);

    out.valid = !out.scope.empty() && extra.empty() &&
        (out.status ||
         (out.playerMode && !out.targetName.empty() && ai::IsBlessingOrderWord(out.blessingWord)) ||
         (!out.playerMode && out.classIndex >= 0 && ai::IsBlessingOrderWord(out.blessingWord) && word4.empty()));
    return out;
}

inline char const* BlessingIntent(ParsedBlessingArgs const& parsed)
{
    return parsed.status ? "blessing status" : "blessing";
}

inline std::string FormatBlessingLine(
    std::string_view safeBotName,
    std::array<std::string, ai::kBlessingOrderClassCount> const& classStored,
    std::array<bool, ai::kBlessingOrderBlessingCount> const& known,
    std::string const& playersStored)
{
    std::string line = "TBM:BLESSING|";
    line.append(safeBotName);
    line.push_back('|');

    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
    {
        std::string word = classStored[i];
        if (ai::BlessingOrderSpell(word).empty())
            word = "auto";
        line += ai::BlessingOrderClassWord(i);
        line += "=" + word + ",";
    }

    line += "known=";
    bool first = true;
    for (int i = 0; i < ai::kBlessingOrderBlessingCount; ++i)
    {
        if (!known[i])
            continue;
        if (!first)
            line.push_back('+');
        line += ai::BlessingOrderWord(i);
        first = false;
    }

    std::string const players = ai::FormatBlessingPlayerOrders(ai::ParseBlessingPlayerOrders(playersStored));
    if (!players.empty())
        line += ",players=" + players;

    return line;
}

inline std::string FormatBlessingAckDetail(ParsedBlessingArgs const& parsed, std::string const& canonicalPlayerName)
{
    if (parsed.playerMode)
        return "player:" + canonicalPlayerName + "=" + parsed.blessingWord;
    return parsed.word2 + "=" + parsed.blessingWord;
}

} // namespace blessing
} // namespace TortoiseBots
