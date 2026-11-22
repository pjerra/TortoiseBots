#pragma once

// Core-free pure rules for `.bot autogear` and `.autogear`.
// Tested standalone by forever/tools/test_autogear_pure.cpp.

#include "runtime/OwnedBotQolPolicy.h"
#include "runtime/PlayerAutogearPolicy.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace autogear
{

inline bool ClaimsAutogearCommand(std::string_view text, std::string_view& rest)
{
    rest = {};
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    std::string_view const word = text.substr(i);
    constexpr std::string_view kCmd = "autogear";
    if (word.size() < kCmd.size())
        return false;
    for (std::size_t k = 0; k < kCmd.size(); ++k)
    {
        if (std::tolower(static_cast<unsigned char>(word[k])) != kCmd[k])
            return false;
    }
    if (word.size() > kCmd.size() && !std::isspace(static_cast<unsigned char>(word[kCmd.size()])))
        return false;
    rest = word.substr(kCmd.size());
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
        rest.remove_prefix(1);
    return true;
}

inline std::vector<std::string> SplitWords(std::string_view text)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::size_t const start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i > start)
            words.emplace_back(text.substr(start, i - start));
    }
    return words;
}

inline void ParseBotAutogearTail(std::string_view tail, uint32_t qualityCap, uint32_t configIlvlCap,
                                 std::string& outBotToken, OwnedBotAutogearRequest& outReq)
{
    outBotToken.clear();
    std::size_t i = 0;
    while (i < tail.size() && std::isspace(static_cast<unsigned char>(tail[i])))
        ++i;
    std::size_t const start = i;
    while (i < tail.size() && !std::isspace(static_cast<unsigned char>(tail[i])))
        ++i;
    outBotToken = std::string(tail.substr(start, i - start));
    std::string const param = i < tail.size() ? std::string(tail.substr(i)) : std::string();
    uint32_t const clampedCap = qualityCap > 5 ? 5 : qualityCap;
    outReq = ParseOwnedBotAutogearArg(param, clampedCap, configIlvlCap);
}

inline bool ParsePlayerAutogearArgs(std::vector<std::string> const& tokensIn,
                                    std::string& outNameToken,
                                    std::string& outSpecToken,
                                    uint32_t& outRequestedIlvl)
{
    outNameToken.clear();
    outSpecToken.clear();
    outRequestedIlvl = 0;

    std::vector<std::string> tokens = tokensIn;
    if (!tokens.empty())
    {
        std::string const& last = tokens.back();
        bool digitsOnly = !last.empty() && last.size() <= 5;
        for (char c : last)
        {
            if (c < '0' || c > '9')
                digitsOnly = false;
        }
        if (digitsOnly)
        {
            outRequestedIlvl = static_cast<uint32_t>(std::stoul(last));
            tokens.pop_back();
        }
    }
    if (tokens.empty())
        return false;

    std::size_t specStart = 0;
    if (tokens.size() >= 2 && !IsAutogearSpecToken(tokens[0]))
    {
        outNameToken = tokens[0];
        specStart = 1;
    }
    outSpecToken = tokens[specStart];
    for (std::size_t i = specStart + 1; i < tokens.size(); ++i)
        outSpecToken += " " + tokens[i];
    return true;
}

inline bool CandidateFitsAutogearCaps(uint8_t candQuality, uint16_t candIlvl, uint32_t maxQuality, uint32_t ilvlCap)
{
    if (candQuality > maxQuality)
        return false;
    if (ilvlCap > 0 && candIlvl > ilvlCap)
        return false;
    return true;
}

inline bool ShouldEquipAutogearUpgrade(bool slotOccupied,
                                       uint32_t currentScore,
                                       uint32_t candidateScore,
                                       bool isRangedSlot,
                                       RangedAmmoKind currentRanged,
                                       RangedAmmoKind candidateRanged,
                                       bool hasArrows,
                                       bool hasBullets)
{
    if (candidateScore == 0)
        return false;

    if (isRangedSlot)
    {
        if (!OwnedBotAcceptRangedPick(currentRanged, currentScore, candidateRanged, candidateScore, hasArrows, hasBullets))
            return false;
        if (slotOccupied && OwnedBotRangedReplacesDud(currentRanged, candidateRanged, hasArrows, hasBullets))
            return true;
    }

    if (!slotOccupied)
        return true;
    return candidateScore > currentScore;
}

} // namespace autogear
} // namespace TortoiseBots
