// The `.bot` rule without the core: who may name which bot for which verb (D-2, D-3, D-4, D-7, D-8 of the player-command spec).
// No core header and no legacy type; tested standalone by forever/tools/test_player_auth.cpp. The host half that reads the live
// objects is forever/src/host/player/TbPlayerCmd.cpp (`FactsOf`).
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace player
{
// D-4: what a verb costs. `help` names no bot; the owner tier holds every later verb that moves items, money or account state,
// and every work nobody pinned lower (unknown words fail closed).
enum class Tier
{
    None,
    Tactical,
    Owner,
};

inline std::string LowerWord(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// The class-choice commands (#133: `.bot totem`, `.bot behavior`, `.bot blessing`, `.bot choice`, `.bot service`). Every write and
// every cast is Owner tier; `status` and the bare service query read only and are Tactical. Fail closed: anything not pinned
// Tactical is Owner, and `TierOf` above is untouched by these verbs.
inline Tier TierOfChoice(std::string_view verb, std::string_view sub)
{
    std::string const verbWord = LowerWord(verb);
    std::string const subWord = LowerWord(sub);
    if (subWord == "status")
        return Tier::Tactical;
    if (verbWord == "service" && subWord.empty())
        return Tier::Tactical;
    return Tier::Owner;
}

inline Tier TierOf(std::string_view verb)
{
    auto lower = [](std::string_view word)
    {
        std::string out;
        out.reserve(word.size());
        for (char c : word)
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    };
    std::string const word = lower(verb);
    if (word.empty() || word == "help")
        return Tier::None;
    if (word == "follow" || word == "stay" || word == "attack" || word == "flee" || word == "who" || word == "roster" ||
        word == "list" || word == "stats")
        return Tier::Tactical;
    if (word == "add" || word == "logout" || word == "remove" || word == "summon")
        return Tier::Owner;
    return Tier::Owner;
}

// The caller and the bot, as the host reads them. Ownership is the Battle.net account (D-1 amendment, 2026-11-22):
// `Caller.account` is the Battle.net account of the caller's session; `BotFacts.account` is that same id when the bot
// character's game account is one of the caller's Battle.net account's game accounts (read fresh per command), else 0.
// So two game accounts of one Battle.net account own the same bots. A guid is never a guess at ownership.
struct Caller
{
    bool gm = false;
    std::uint32_t account = 0;
    std::uint64_t guid = 0;
};

struct BotFacts
{
    bool isBot = false;
    std::uint32_t account = 0;
    std::uint64_t master = 0;
    bool commandedByCaller = false;
    std::uint64_t groupLeader = 0;
    bool leaderIsBot = false;
    bool inRun = false;
};

enum class Verdict
{
    Allowed,
    NotABot,
    NotYours,
    InRun,
};

inline Verdict Decide(Caller const& caller, BotFacts const& bot, Tier tier)
{
    if (tier == Tier::None)
        return Verdict::Allowed;                 // nothing is addressed, so no bot rule applies
    if (!bot.isBot)
        return Verdict::NotABot;
    if (bot.inRun)
        return Verdict::InRun;                   // every caller, GMs included (`.tb run pause` is the GM's way)
    if (caller.gm)
        return Verdict::Allowed;
    // The bot's own Battle.net account. Account 0 is "unknown", never a match between two characters without one.
    if (caller.account != 0 && bot.account != 0 && caller.account == bot.account)
        return Verdict::Allowed;
    if (tier == Tier::Tactical)
    {
        if (bot.master != 0 && bot.master == caller.guid)
            return Verdict::Allowed;
        if (bot.commandedByCaller)
            return Verdict::Allowed;
        if (bot.groupLeader != 0 && bot.groupLeader == caller.guid && !bot.leaderIsBot)
            return Verdict::Allowed;
    }
    return Verdict::NotYours;
}

// The owner tier of the chat words (R5 fix, ADR 0005: ownership is the Battle.net account). The legacy whisper gate
// (PlayerbotAI::IsBotOwnerOrGm) asks this for a line from a real player to a bot: `callerBnet` is the sender's Battle.net account
// and `botOwnerBnet` is that same id when the bot character's game account is one of the sender's Battle.net account's game accounts
// (read fresh, the way `.bot` reads it: TbPlayerCmd.cpp OwnerScope), else 0. A GM always passes; account 0 is "unknown" and never
// matches. Two game accounts of one Battle.net account own the same bots; a stranger or a group member who is not the owner does not.
inline bool ChatOwnerAllowed(bool gm, std::uint32_t callerBnet, std::uint32_t botOwnerBnet)
{
    if (gm)
        return true;
    return callerBnet != 0 && botOwnerBnet != 0 && callerBnet == botOwnerBnet;
}

inline char const* CodeOf(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Allowed:  return "";
        case Verdict::NotABot:  return "not-a-bot";
        case Verdict::NotYours: return "not-yours";
        case Verdict::InRun:    return "in-run";
    }
    return "";
}

// D-1: the entry claims the command text only when its first word is `bot`, case-insensitively, followed by the end or a space.
// `rest` is the text after that word (leading blanks dropped, the tail as typed).
inline bool ClaimsCommand(std::string_view text, std::string_view& rest)
{
    rest = {};
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    std::string_view const word = text.substr(i);
    if (word.size() < 3)
        return false;
    for (std::size_t k = 0; k < 3; ++k)
        if (std::tolower(static_cast<unsigned char>(word[k])) != "bot"[k])
            return false;
    if (word.size() > 3 && !std::isspace(static_cast<unsigned char>(word[3])))
        return false;
    rest = word.substr(3);
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
        rest.remove_prefix(1);
    return true;
}

// D-3/D-7: an owned login may skip the one-bot-per-account rule only for a character on the owner's Battle.net account and
// only under the cap; a GM may log any character in. Both accounts are Battle.net ids. `code` is the reply word on
// refusal ("" when allowed).
inline bool MayLoginOwned(std::uint32_t callerAccount, std::uint32_t characterAccount, bool callerGm, std::uint32_t ownedOnline,
                          std::uint32_t maxBots, std::string& code)
{
    code.clear();
    if (callerGm)
        return true;
    if (callerAccount == 0 || characterAccount == 0 || callerAccount != characterAccount)
    {
        code = "not-yours";
        return false;
    }
    if (ownedOnline >= maxBots)
    {
        code = "too-many";
        return false;
    }
    return true;
}

// D-8: whether the summon cooldown still holds. GMs are exempt, 0 seconds means no cooldown, and a stamp of 0 is "never summoned".
// A clock that went backwards counts as just stamped: the cooldown never reads as expired.
inline bool SummonCooldownLeft(std::uint64_t nowMs, std::uint64_t lastMs, std::uint32_t cooldownS, bool gm, std::uint32_t& leftS)
{
    leftS = 0;
    if (gm || cooldownS == 0 || lastMs == 0)
        return false;
    std::uint64_t const cooldownMs = std::uint64_t(cooldownS) * 1000;
    std::uint64_t const elapsed = nowMs >= lastMs ? nowMs - lastMs : 0;
    if (elapsed >= cooldownMs)
        return false;
    leftS = static_cast<std::uint32_t>((cooldownMs - elapsed + 999) / 1000);
    return true;
}

// D-6: every value of a `.tb` reply record is percent-encoded (TbH3.h `Rec`: a byte outside [A-Za-z0-9._-] as %XX, upper-case hex).
// The `.bot` lines are human text, so the summon handler decodes the shared teleport's own refusal message back before showing it to
// the player (D-8: the message is that function's, never a second copy of its wording). A '%' that does not start two hex digits is
// kept as typed: a malformed escape is shown, never guessed at.
inline std::string EncDecode(std::string_view text)
{
    auto hexDigit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            int const high = hexDigit(text[i + 1]);
            int const low = hexDigit(text[i + 2]);
            if (high >= 0 && low >= 0)
            {
                out.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}
}
}
