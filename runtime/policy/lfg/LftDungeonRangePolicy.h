#pragma once

// One LFT level-range table for the LFT fill and bot LFG groups; source
// the LFT built into the Turtle client; unknown and invalid ranges fail
// closed; plain data and the standard library only.

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace TortoiseBots
{

struct LftLevelRange
{
    uint8_t minLevel;
    uint8_t maxLevel;
};

inline std::string NormalizeLftToken(std::string const& raw)
{
    std::string normalized;
    normalized.reserve(raw.size());
    for (unsigned char character : raw)
        if (std::isalnum(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized;
}

inline std::unordered_map<std::string, LftLevelRange> const& LftDungeonRangeTable()
{
    // Source: the LFT built into the Turtle client, Interface/FrameXML/LFT/LFT_Data.lua LFT_Instances
    // (client 1.18.1 (7272)); the core's TW_LFG queue receives these codes as the client sends them.
    // Keys are NormalizeLftToken(raw): the client code, the English name, and
    // the aliases deadmines / stockade / zulfarak / crescentgrove kept for ai_bot_lfg_plan rows.
    static std::unordered_map<std::string, LftLevelRange> const ranges = {
        {"ragefirechasm", {13, 19}}, {"rfc", {13, 19}},
        {"frostmanehollow", {13, 20}}, {"fmh", {13, 20}},
        {"wailingcaverns", {16, 25}}, {"wc", {16, 25}},
        {"thedeadmines", {16, 24}}, {"deadmines", {16, 24}}, {"dm", {16, 24}},
        {"shadowfangkeep", {20, 28}}, {"sfk", {20, 28}},
        {"thestockade", {23, 32}}, {"stockade", {23, 32}}, {"stocks", {23, 32}},
        {"blackfathomdeeps", {22, 31}}, {"bfd", {22, 31}},
        {"windhorncanyon", {25, 33}}, {"windhorn", {25, 33}},
        {"scarletmonasterygraveyard", {30, 37}}, {"smgy", {30, 37}},
        {"scarletmonasterylibrary", {32, 40}}, {"smlib", {32, 40}},
        {"gnomeregan", {28, 37}}, {"gnomer", {28, 37}},
        {"razorfenkraul", {29, 36}}, {"rfk", {29, 36}},
        {"thecrescentgrove", {33, 39}}, {"crescentgrove", {33, 39}}, {"tcg", {33, 39}},
        {"scarletmonasteryarmory", {34, 42}}, {"smarmory", {34, 42}},
        {"scarletmonasterycathedral", {35, 45}}, {"smcath", {35, 45}},
        {"razorfendowns", {35, 44}}, {"rfd", {35, 44}},
        {"uldaman", {41, 50}}, {"ulda", {41, 50}},
        {"gilneascity", {43, 52}}, {"gilneas", {43, 52}},
        {"zulfarrak", {42, 51}}, {"zulfarak", {42, 51}}, {"zf", {42, 51}},
        {"maraudonorange", {43, 51}}, {"maraorange", {43, 51}},
        {"maraudonpurple", {45, 52}}, {"marapurple", {45, 52}},
        {"maraudonprincess", {46, 54}}, {"maraprincess", {46, 54}},
        {"templeofatalhakkar", {49, 58}}, {"st", {49, 58}},
        {"hateforgequarry", {51, 60}}, {"hfq", {51, 60}},
        {"blackrockdepths", {54, 60}}, {"brd", {54, 60}},
        {"blackrockdepthsarena", {50, 60}}, {"brdarena", {50, 60}},
        {"blackrockdepthsemperor", {54, 60}}, {"brdemp", {54, 60}},
        {"lowerblackrockspire", {55, 60}}, {"lbrs", {55, 60}},
        {"diremauleast", {55, 60}}, {"dme", {55, 60}},
        {"diremaulnorth", {57, 60}}, {"dmn", {57, 60}},
        {"diremaultribute", {57, 60}}, {"dmt", {57, 60}},
        {"diremaulwest", {57, 60}}, {"dmw", {57, 60}},
        {"scholomance", {58, 60}}, {"scholo", {58, 60}},
        {"stratholmeundeaddistrict", {58, 60}}, {"stratud", {58, 60}},
        {"stratholmescarletbastion", {58, 60}}, {"stratlive", {58, 60}},
        {"karazhancrypt", {60, 60}}, {"kc", {60, 60}},
        {"cavernsoftimeblackmorass", {60, 60}}, {"cotbm", {60, 60}},
        {"stormwindvault", {60, 60}}, {"swv", {60, 60}},
        {"stormwroughtcastle", {32, 40}}, {"swc", {32, 40}},
        {"stormwroughtdescent", {35, 44}}, {"swd", {35, 44}},
        {"dragonmawretreat", {26, 35}}, {"dmr", {26, 35}}
    };
    return ranges;
}

inline LftLevelRange const* LftDungeonRange(std::string const& raw)
{
    std::unordered_map<std::string, LftLevelRange> const& ranges = LftDungeonRangeTable();
    auto it = ranges.find(NormalizeLftToken(raw));
    if (it == ranges.end() || it->second.minLevel == 0 ||
        it->second.minLevel > it->second.maxLevel || it->second.maxLevel > 60)
        return nullptr;
    return &it->second;
}

} // namespace TortoiseBots
