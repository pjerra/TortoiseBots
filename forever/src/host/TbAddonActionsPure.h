// The `.bot action <intent>` surface without the core: TB's intent grammar,
// the target scope rule and the TBF support table (TBF #130 piece f-actions).
// No core header and no legacy type; tested standalone by
// forever/tools/test_addon_actions.cpp. The live half that reads the live
// objects and queues the orders is host/TbAddonActions.cpp.
#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace action
{
// TB's ParseAction (TortoiseBots/commands/Action.cpp:251-488): the first word
// names the intent, the rest is the intent's own option. `ok` false answers
// Err(intent,"invalid",Usage()) with the first word as the intent, exactly
// like TB's HandleAction.
struct ParsedAction
{
    bool ok = false;
    std::string intent;
    std::string option;
};

// RtiTargetValue::GetRtiIndex (ai/playerbot/strategy/values/RtiTargetValue.h):
// the eight raid-icon words. "clear" is not one of them.
inline bool IsRaidTargetMark(std::string_view mark)
{
    return mark == "star" || mark == "circle" || mark == "diamond" || mark == "triangle" || mark == "moon" ||
        mark == "square" || mark == "cross" || mark == "skull";
}

inline std::string Lower(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string Trim(std::string const& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return text.substr(begin, end - begin);
}

namespace detail
{
// ai::ParsePullOrderOption (ai/playerbot/PullOpeningPolicy.h:136): words of
// seconds (digits, at most 60) with an optional `ranged` or `body` opener;
// `ranged` and `body` together never parse.
struct PullOption
{
    bool hasSeconds = false;
    unsigned seconds = 0;
    bool ranged = false;
    bool body = false;
};

inline bool ParsePullOption(std::string const& text, PullOption& out)
{
    out = PullOption();
    std::size_t pos = 0;
    while (true)
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
        if (pos >= text.size())
            return !(out.ranged && out.body);
        std::size_t const start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t')
            ++pos;
        std::string word = Lower(std::string_view(text).substr(start, pos - start));
        if (word == "ranged")
        {
            if (out.ranged)
                return false;
            out.ranged = true;
        }
        else if (word == "body")
        {
            if (out.body)
                return false;
            out.body = true;
        }
        else if (!word.empty() && word.find_first_not_of("0123456789") == std::string::npos)
        {
            if (out.hasSeconds || word.size() > 3)
                return false;
            unsigned const seconds = static_cast<unsigned>(std::atoi(word.c_str()));
            if (seconds > 60)
                return false;
            out.hasSeconds = true;
            out.seconds = seconds;
        }
        else
            return false;
    }
}

// TB's PullOptionText (commands/Action.cpp:240-249): "[seconds][ ranged|body]".
inline std::string PullOptionText(PullOption const& parsed)
{
    std::string text = parsed.hasSeconds ? std::to_string(parsed.seconds) : std::string();
    if (parsed.ranged)
        text += text.empty() ? "ranged" : " ranged";
    if (parsed.body)
        text += text.empty() ? "body" : " body";
    return text;
}
} // namespace detail

inline ParsedAction ParseAction(std::string_view args)
{
    ParsedAction out;
    std::string const input = Trim(std::string(args));
    if (input.empty())
        return out;
    std::size_t const firstSeparator = input.find_first_of(" \t");
    std::string const first = Lower(input.substr(0, firstSeparator));
    std::string const remainder = firstSeparator == std::string::npos ? std::string() : Trim(input.substr(firstSeparator + 1));

    if (first == "focus" || first == "cc")
    {
        std::size_t const secondSeparator = remainder.find_first_of(" \t");
        std::string const mark = Lower(remainder.substr(0, secondSeparator));
        if (first == "cc" && mark == "clear")
        {
            std::string const botToken =
                secondSeparator == std::string::npos ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return out;
            out.ok = true;
            out.intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return out;
        }
        if (first == "cc" && IsRaidTargetMark(mark))
        {
            std::string const botToken =
                secondSeparator == std::string::npos ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return out;
            out.ok = true;
            out.intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return out;
        }
        if ((first == "focus" && mark != "skull") || (first == "cc" && !IsRaidTargetMark(mark)) || mark.empty() ||
            (secondSeparator != std::string::npos && !Trim(remainder.substr(secondSeparator + 1)).empty()))
            return out;
        out.ok = true;
        out.intent = first + " " + mark;
        return out;
    }
    if (first == "corpse")
    {
        if (Lower(remainder) != "run")
            return out;
        out.ok = true;
        out.intent = "corpse run";
        return out;
    }

    if (first != "attack" && first != "interrupt" && first != "stop" && first != "pull" && first != "pullback" &&
        first != "engage" && first != "come" && first != "stay" && first != "follow" && first != "auto" &&
        first != "autocc" && first != "aoe" && first != "hold" && first != "comestay" && first != "ready" &&
        first != "loot" && first != "repair" && first != "sell" && first != "rest" && first != "drink" &&
        first != "eat" && first != "release" && first != "corpse run" && first != "corpserun" && first != "learn" &&
        first != "trade" && first != "raid" && first != "flee" && first != "sap" && first != "stance" &&
        first != "rebuff" && first != "guard" && first != "free" && first != "end" && first != "use")
        return out;

    if (first == "raid")
    {
        std::string const rest = Lower(remainder);
        if (rest != "status" && rest != "tankface" && rest != "douse" && rest != "custom status" &&
            rest != "custom on" && rest != "custom off")
            return out;
        out.ok = true;
        out.intent = "raid " + rest;
        return out;
    }
    if (first == "corpse run" || first == "corpserun")
    {
        out.ok = true;
        out.intent = "corpse run";
        return out;
    }
    if (first == "auto")
    {
        std::string const rest = Lower(remainder);
        if (rest == "cc")
        {
            out.ok = true;
            out.intent = "auto cc";
            return out;
        }
        if (rest == "cc on" || rest == "cc off" || rest == "cc status")
        {
            out.ok = true;
            out.intent = "auto cc";
            out.option = rest.substr(3);
            return out;
        }
        return out;
    }
    if (first == "aoe" || first == "loot" || first == "autocc")
    {
        std::string const rest = Lower(remainder);
        if (!rest.empty() && rest != "on" && rest != "off" && rest != "status")
            return out;
        out.ok = true;
        out.intent = first == "autocc" ? "auto cc" : first;
        out.option = rest;
        return out;
    }
    if (first == "sap")
    {
        std::size_t const modeSeparator = remainder.find_first_of(" \t");
        std::string mode = Lower(remainder.substr(0, modeSeparator));
        std::string botToken = modeSeparator == std::string::npos ? std::string() : Trim(remainder.substr(modeSeparator + 1));
        if (mode != "on" && mode != "off")
        {
            if (!botToken.empty())
                return out;
            botToken = remainder.substr(0, modeSeparator);
            mode = "toggle";
        }
        if (botToken.find_first_of(" \t") != std::string::npos)
            return out;
        out.ok = true;
        out.intent = first;
        out.option = botToken.empty() ? mode : mode + " " + botToken;
        return out;
    }
    if (first == "stance")
    {
        std::string const stanceRest = Lower(remainder);
        std::string word = stanceRest == "turn" ? "turnback" : stanceRest;
        if (word != "near" && word != "tank" && word != "turnback" && word != "behind")
            return out;
        out.ok = true;
        out.intent = "stance";
        out.option = word;
        return out;
    }
    if (first == "end")
    {
        if (Lower(remainder) != "pull")
            return out;
        out.ok = true;
        out.intent = "end pull";
        return out;
    }
    if (first == "guard" || first == "free" || first == "rebuff")
    {
        if (!remainder.empty())
            return out;
        out.ok = true;
        out.intent = first;
        return out;
    }
    if (first == "use")
    {
        out.ok = true;
        out.intent = first;
        out.option = remainder;
        return out;
    }
    if (first == "engage")
    {
        if (!remainder.empty())
        {
            for (char c : remainder)
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return out;
            if (remainder.size() > 3 || static_cast<unsigned>(std::atoi(remainder.c_str())) > 60)
                return out;
            out.option = std::to_string(static_cast<unsigned>(std::atoi(remainder.c_str())));
        }
        out.ok = true;
        out.intent = first;
        return out;
    }
    if (first == "pull" || first == "pullback")
    {
        if (!remainder.empty())
        {
            detail::PullOption parsed;
            if (!detail::ParsePullOption(remainder, parsed))
                return out;
            out.option = detail::PullOptionText(parsed);
        }
        out.ok = true;
        out.intent = first;
        return out;
    }
    if (!remainder.empty())
        return out;
    out.ok = true;
    out.intent = first;
    return out;
}
// TB's malformed-intent name (HandleAction): the first word, at most 32 bytes
// and never cut inside a UTF-8 character, ProtocolSafe'd; "unknown" when the
// result is empty.
inline std::string InvalidIntentName(std::string_view args)
{
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t'))
        args.remove_prefix(1);
    std::size_t end = 0;
    while (end < args.size() && args[end] != ' ' && args[end] != '\t')
        ++end;
    std::string word(args.substr(0, end));
    if (word.size() > 32)
    {
        word.resize(32);
        while (!word.empty() && (static_cast<unsigned char>(word.back()) & 0xC0) == 0x80)
            word.pop_back();
    }
    for (char& c : word)
        if (c == '|' || c == '\r' || c == '\n')
            c = ' ';
    word = Trim(word);
    return word.empty() ? "unknown" : word;
}

// TB's usage line for a refused parse (commands/Action.cpp:1112), the single
// source of truth for the surface.
inline std::string const& Usage()
{
    static std::string const usage = "Usage: .bot action attack/interrupt/stop/pull [seconds] [ranged|body]/pullback [seconds] [ranged|body]/"
        "end pull/engage [seconds]/come/stay/hold/follow/flee/guard/free/stance <near|tank|turnback|behind>/rebuff/focus skull/"
        "cc <mark> [bot]/cc clear [bot]/auto cc [on/off/status]/autocc [on/off/status]/sap [on/off] [bot]/aoe [on/off/status]/"
        "loot [on/off/status]/repair/sell/rest/drink/eat/release/corpse run/learn/trade/ready/use [bot]/"
        "raid [status/tankface/douse/custom status/custom on/custom off]";
    return usage;
}

// The scope rule (BotCommandContext.cpp BuildContext/ResolveDynamicScope):
// the player's target, when it is an owned live bot, is the scope; otherwise
// the owned party is. `targetIsOwnedBot` is decided live (selection passes the
// Tactical tier); the pure part is only which branch it takes.
enum class ScopeKind
{
    Single,
    Party,
};

inline ScopeKind DecideScope(bool targetIsOwnedBot)
{
    return targetIsOwnedBot ? ScopeKind::Single : ScopeKind::Party;
}

// TB's ACK scope field: "bot:<Name>" for a single bot, "party" otherwise.
inline std::string ScopeName(bool single, std::string_view botName)
{
    if (single && !botName.empty())
        return "bot:" + std::string(botName);
    return "party";
}
// How the live half runs the intent: Order queues one h3 order per scope bot,
// Interrupt picks one bot (tactics::PickInterruptBot) and queues there,
// FocusSkull queues a focus order, ComeAt a come order at the requester's
// spot, CcClear runs h8::ClearCcOrder, Unsupported answers Err unsupported.
enum class ExecKind
{
    Order,
    Interrupt,
    FocusSkull,
    ComeAt,
    CcClear,
    Unsupported,
};

inline ExecKind Classify(std::string_view intent)
{
    if (intent == "interrupt")
        return ExecKind::Interrupt;
    if (intent == "focus skull")
        return ExecKind::FocusSkull;
    if (intent == "come" || intent == "hold" || intent == "comestay")
        return ExecKind::ComeAt;
    if (intent == "cc clear" || (intent.size() > 9 && intent.compare(0, 9, "cc clear ") == 0))
        return ExecKind::CcClear;
    if (intent == "attack" || intent == "stop" || intent == "follow" || intent == "stay" || intent == "flee" ||
        intent == "guard" || intent == "stance" || intent == "engage" || intent == "rebuff" || intent == "pull" ||
        intent == "pullback" || intent == "end pull" || intent == "free" || intent == "release" || intent == "repair" ||
        intent == "sell" || intent == "learn" || intent == "rest" || intent == "drink" || intent == "eat")
        return ExecKind::Order;
    return ExecKind::Unsupported;
}

// Why the live half refuses the intent: the TBF work that would run it.
inline std::string WhyUnsupported(std::string_view intent)
{
    if (intent.compare(0, 3, "cc ") == 0)
        return "party cc pick needs the M4 order work (open PR #208)";
    if (intent.compare(0, 5, "raid ") == 0)
        return "raid tactics are not ported to Forever";
    if (intent == "aoe" || intent == "auto cc" || intent == "sap" || intent == "loot")
        return "behaviour toggles need the M3 order work (open PR #205)";
    if (intent == "corpse run")
        return "corpse run needs the M3 order work (open PR #205)";
    if (intent == "trade")
        return "trade has no Forever order";
    if (intent == "use")
        return "use-object has no Forever order (commander task machinery absent)";
    if (intent == "ready")
        return "ready check is not ported to Forever";
    return "no Forever implementation";
}

// The h3 order the intent queues (TbOrders.cpp verbs). `option` is the parsed
// option: stance word, engage/pull seconds text, else empty. Name kept cases:
// learn trains, drink/eat rest, hold/comestay come to the requester, end pull
// aborts, focus skull marks and burns.
struct OrderMap
{
    std::string verb;
    std::string arg;
};

inline OrderMap MapToOrder(std::string_view intent, std::string_view option = {})
{
    OrderMap out;
    if (intent == "learn")
        out.verb = "train";
    else if (intent == "drink" || intent == "eat")
        out.verb = "rest";
    else if (intent == "hold" || intent == "comestay")
        out.verb = "come";
    else if (intent == "end pull")
        out.verb = "endpull";
    else if (intent == "focus skull")
        out.verb = "focus";
    else
        out.verb = std::string(intent);
    if (intent == "stance" || intent == "engage" || intent == "pull" || intent == "pullback")
        out.arg = std::string(option);
    return out;
}

// Every intent the live half really runs (the Advertise tokens). Anything
// else parses but answers Err(intent,"unsupported",WhyUnsupported(intent)).
inline std::vector<std::string> const& SupportedIntents()
{
    static std::vector<std::string> const intents = {
        "attack", "stop", "follow", "stay", "flee", "guard", "stance", "engage", "rebuff", "interrupt", "pull",
        "pullback", "end pull", "come", "hold", "comestay", "free", "focus skull", "cc clear", "release", "repair",
        "sell", "learn", "rest", "drink", "eat",
    };
    return intents;
}

inline bool IsSupported(std::string_view intent)
{
    // "cc clear" and "cc clear <Bot>": the name keeps its case, so only the
    // prefix is pinned here.
    if (intent == "cc clear" || (intent.size() > 9 && intent.compare(0, 9, "cc clear ") == 0))
        return true;
    for (std::string const& supported : SupportedIntents())
        if (supported == intent)
            return true;
    return false;
}

// The TBM:VERBS token for the intent (f-chan Advertise).
inline std::string AdvertiseToken(std::string_view intent)
{
    return "action:" + std::string(intent);
}
} // namespace action
} // namespace TortoiseBots
