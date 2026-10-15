#pragma once

// `.bot cmd gossip <npc entry> <bot> <option index | "option text">` (issue #1012):
// a commanded bot talks to an NPC and picks one gossip option, the way a player
// does, so it can start dungeon events that need a conversation (Zul'Farrak:
// Weegli Blastfuse opens the End Door, Sergeant Bly starts his fight).
//
// The glue in commands/BotCommands.cpp walks the bot to the NPC and then hands
// the bot's session the client's own CMSG_GOSSIP_HELLO and
// CMSG_GOSSIP_SELECT_OPTION, so the core builds the menu (Player::PrepareGossipMenu,
// or the NPC's script through sScriptMgr.OnGossipHello) and runs the choice
// (sScriptMgr.OnGossipSelect or Player::OnGossipSelect) exactly as for a player.
// Everything that decides is here, pure, so tools/test_gossip_order_policy.cpp
// exercises it standalone: the allowlist, the argument parse, which options
// count as plain gossip, and which option an index or a text names.
//
// The allowlist is the guard. A gossip option can do anything its script or
// database row says (take money, give items, teleport, remove titles), and the
// hello alone already runs the NPC's OnGossipHello, a menu's script_id or a quest
// window. No rule on icons or action numbers can tell those apart, so a bot talks
// only to NPC entries an operator listed in AiPlayerbot.BotGossipNpcs after
// reading what their scripts do (empty by default: the order is off). The order
// checks the list before anything is sent to the NPC.
//
// Plain data and the standard library only. World thread only, like the rest of
// the commander.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace GossipOrder {

// The bot looks for the NPC this far around itself (the issue's 40 yd).
constexpr float kSearchYards = 40.0f;
// A creature entry is the 24-bit entry field of a GUID.
constexpr uint32_t kMaxEntry = 0xFFFFFF;
// A 1.12 gossip menu holds at most 15 options (SMSG_GOSSIP_MESSAGE).
constexpr uint32_t kMaxIndex = 15;

// Result codes (the second field of TBM:ACTION_ERR) of this order.
constexpr char const* kCodeNoNpc = "no-npc";
constexpr char const* kCodeNoOption = "no-option";
constexpr char const* kCodeAmbiguous = "ambiguous";
constexpr char const* kCodeNpcBusy = "npc-busy";
constexpr char const* kCodeNotAllowed = "not-allowed";

// AiPlayerbot.BotGossipNpcs: creature entries, separated by commas or spaces.
// Returns how many words were not an entry (1..kMaxEntry); those are skipped.
inline uint32_t ParseNpcList(std::string const& text, std::set<uint32_t>& out)
{
    out.clear();
    uint32_t bad = 0;
    std::string word;
    auto flush = [&]() {
        if (word.empty())
            return;
        bool digits = word.size() <= 8;
        for (char c : word)
            if (c < '0' || c > '9')
                digits = false;
        unsigned long const value = digits ? std::stoul(word) : 0;
        if (value >= 1 && value <= kMaxEntry)
            out.insert(static_cast<uint32_t>(value));
        else
            ++bad;
        word.clear();
    };
    for (char c : text)
    {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c)))
            flush();
        else
            word += c;
    }
    flush();
    return bad;
}

// The gate, before the bot walks and again right before the hello.
inline bool IsAllowedNpc(std::set<uint32_t> const& allowed, uint32_t entry)
{
    return allowed.count(entry) != 0;
}

inline std::string NotAllowedText(std::set<uint32_t> const& allowed)
{
    if (allowed.empty())
        return "Bots talk to no NPC: AiPlayerbot.BotGossipNpcs is empty (the order is off by default).";
    return "That NPC is not in AiPlayerbot.BotGossipNpcs, so bots may not talk to it.";
}

// The core's numbers this policy reads (GossipDef.h, ScriptedGossip.h). The glue
// checks them against the core's enums with static_asserts.
constexpr uint32_t kOptionGossip = 1;      // GOSSIP_OPTION_GOSSIP
constexpr uint32_t kOptionMax = 19;        // GOSSIP_OPTION_MAX
constexpr uint8_t kIconVendor = 1;         // GOSSIP_ICON_VENDOR
constexpr uint8_t kIconTaxi = 2;           // GOSSIP_ICON_TAXI
constexpr uint8_t kIconTrainer = 3;        // GOSSIP_ICON_TRAINER
constexpr uint8_t kIconMoneyBag = 6;       // GOSSIP_ICON_MONEY_BAG
constexpr uint8_t kIconTabard = 8;         // GOSSIP_ICON_TABARD
constexpr uint32_t kSenderMain = 1;        // GOSSIP_SENDER_MAIN

// One line of the menu the core built for the bot (GossipMenuItem).
struct Option
{
    std::string text;
    uint8_t icon = 0;
    uint32_t sender = 0;  // 0: a database menu option (Player::PrepareGossipMenu); else a script's
    uint32_t action = 0;  // database option: its GOSSIP_OPTION_*; script option: the script's action
    bool coded = false;   // asks for a typed code
};

// A second check on a listed NPC's menu, not the guard (that is the allowlist):
// the database's service options (vendor, trainer, taxi, banker, innkeeper,
// spirit healer, auction, battlemaster, stable, tabard, talent reset ...), a
// script option with a vendor, flight, trainer, money or tabard icon, and a coded
// option (it needs a typed code) are refused. A database option keeps its
// GOSSIP_OPTION_* in action with sender 0; a script option has a sender of its
// own (GOSSIP_SENDER_MAIN and up) and an action only its script reads, so a
// script's action number says nothing here.
inline bool IsPlainOption(Option const& option)
{
    if (option.coded)
        return false;
    if (option.sender == 0 && option.action < kOptionMax)
        return option.action == kOptionGossip;
    if (option.icon == kIconVendor || option.icon == kIconTaxi || option.icon == kIconTrainer ||
        option.icon == kIconMoneyBag || option.icon == kIconTabard)
        return false;
    return true;
}

struct Request
{
    uint32_t entry = 0;
    std::string bot;
    bool byIndex = false;
    uint32_t index = 0;  // 1-based, as the menu lists the options from the top
    std::string text;    // by text: as typed, quotes removed
};

inline std::string Trimmed(std::string const& text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(start, end - start);
}

inline bool AllDigits(std::string const& token)
{
    if (token.empty())
        return false;
    for (char c : token)
        if (c < '0' || c > '9')
            return false;
    return true;
}

// Splits the next word (up to a space or a tab) off the front of text.
inline std::string NextWord(std::string& text)
{
    text = Trimmed(text);
    size_t const split = text.find_first_of(" \t");
    std::string const word = text.substr(0, split);
    text = split == std::string::npos ? std::string() : text.substr(split);
    return word;
}

// "<entry> <bot> <index | "text" | text>". The option is the whole rest of the
// line, so a text keeps its commas and spaces; quotes around it are optional and
// make a number a text ("1"). An index is 1 to 15.
inline bool ParseRequest(std::string const& args, Request& out)
{
    std::string rest = args;
    std::string const entryWord = NextWord(rest);
    std::string const botWord = NextWord(rest);
    std::string option = Trimmed(rest);
    if (!AllDigits(entryWord) || entryWord.size() > 8 || botWord.empty() || option.empty())
        return false;
    Request request;
    request.entry = static_cast<uint32_t>(std::stoul(entryWord));
    if (request.entry == 0 || request.entry > kMaxEntry)
        return false;
    request.bot = botWord;
    if (option.front() == '"')
    {
        if (option.size() < 3 || option.back() != '"')
            return false;
        request.text = option.substr(1, option.size() - 2);
        if (Trimmed(request.text).empty())
            return false;
    }
    else if (AllDigits(option))
    {
        if (option.size() > 2)
            return false;
        request.byIndex = true;
        request.index = static_cast<uint32_t>(std::stoul(option));
        if (request.index == 0 || request.index > kMaxIndex)
            return false;
    }
    else
        request.text = option;
    out = request;
    return true;
}

inline std::string Lowered(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

struct Match
{
    bool ok = false;
    uint32_t listIndex = 0;  // 0-based: what CMSG_GOSSIP_SELECT_OPTION carries
    std::string code;
    std::string message;
};

// The menu, for a refusal: `1 "text", 2 "text"`, or that it has none.
inline std::string DescribeMenu(std::vector<Option> const& options)
{
    if (options.empty())
        return "The NPC's menu has no options right now.";
    std::string text = "The menu offers:";
    for (size_t i = 0; i < options.size(); ++i)
    {
        text += (i ? ", " : " ") + std::to_string(i + 1) + " \"" + options[i].text + "\"";
        if (!IsPlainOption(options[i]))
            text += " (service)";
    }
    text += ".";
    return text;
}

inline Match Refuse(char const* code, std::string const& message)
{
    Match match;
    match.code = code;
    match.message = message;
    return match;
}

inline Match Pick(uint32_t listIndex)
{
    Match match;
    match.ok = true;
    match.listIndex = listIndex;
    return match;
}

// The option the request names on this menu. By index: that line, if it is plain.
// By text, first match wins in this order, among the plain options only: the exact
// text, the text without case, a part of the text without case; two or more at
// the first step that finds any is `ambiguous`. A text that names only service
// options is refused like a missing one.
inline Match MatchOption(std::vector<Option> const& options, Request const& request)
{
    static char const kService[] =
        " is a service option (vendor, trainer, flight, bank, inn or similar); bots do not pick those. ";
    if (request.byIndex)
    {
        if (request.index == 0 || request.index > options.size())
            return Refuse(kCodeNoOption, "No option " + std::to_string(request.index) + ". " + DescribeMenu(options));
        Option const& option = options[request.index - 1];
        if (!IsPlainOption(option))
            return Refuse(kCodeNoOption, "Option " + std::to_string(request.index) + kService + DescribeMenu(options));
        return Pick(request.index - 1);
    }

    std::string const wanted = request.text;
    std::string const wantedLower = Lowered(wanted);
    for (int step = 0; step < 3; ++step)
    {
        std::vector<uint32_t> hits;
        bool serviceHit = false;
        for (size_t i = 0; i < options.size(); ++i)
        {
            std::string const& text = options[i].text;
            bool hit = false;
            if (step == 0)
                hit = text == wanted;
            else if (step == 1)
                hit = Lowered(text) == wantedLower;
            else
                hit = Lowered(text).find(wantedLower) != std::string::npos;
            if (!hit)
                continue;
            if (IsPlainOption(options[i]))
                hits.push_back(static_cast<uint32_t>(i));
            else
                serviceHit = true;
        }
        if (hits.size() == 1)
            return Pick(hits.front());
        if (hits.size() > 1)
        {
            std::string message = "\"" + wanted + "\" matches " + std::to_string(hits.size()) +
                " options; give more of the text or the number. " + DescribeMenu(options);
            return Refuse(kCodeAmbiguous, message);
        }
        if (serviceHit)
            return Refuse(kCodeNoOption, "\"" + wanted + "\"" + kService + DescribeMenu(options));
    }
    return Refuse(kCodeNoOption, "No option matches \"" + wanted + "\". " + DescribeMenu(options));
}

} // namespace GossipOrder
} // namespace TortoiseBots
