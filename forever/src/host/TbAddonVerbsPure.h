// The addon-channel shape of the passthrough `.bot` verbs (TBF #130 piece f-verbs): which verbs answer natively over TBM
// and how one captured player-layer reply line maps onto the wire verdict. No core header and no legacy type; the host half
// (TbAddonVerbs.cpp) captures the lines through the TbPlayerCmd reply sink and emits them through the addon channel.
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace addonverbs
{

inline std::string Lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Every `.bot` word the addon sends that the player layer answers. `remove` is the logout alias (the layer prints `logout`
// lines for both). `follow`/`stay`/`attack`/`flee` ride the `action` verb (f-actions); `roster`/`list`/`alts` ride f-roster;
// `action`, `cc`, `run`, `features`, `config` ride f-actions/f-info; `invite`/`uninvite`, `role`, `strategy`, `formation`,
// `downtime`, `command` and the rest have no player-layer handler, so the channel answers those `unsupported` (f-chan).
inline bool IsPassthroughVerb(std::string_view verb)
{
    std::string const word = Lower(verb);
    return word == "add" || word == "logout" || word == "remove" || word == "summon" || word == "stats" ||
           word == "who" || word == "maintenance" || word == "autogear" || word == "hire";
}

// The advertised TBM:VERBS tokens: one per registered word, sorted. Registering the word advertises the bare verb itself.
inline std::vector<std::string> PassthroughVerbs()
{
    return {"add", "autogear", "hire", "logout", "maintenance", "remove", "stats", "summon", "who"};
}

// The word the player layer prints for a sent verb (`remove` answers with `logout` lines).
inline std::string CanonicalVerb(std::string_view verb)
{
    std::string const word = Lower(verb);
    if (word == "remove")
        return "logout";
    return word;
}

struct ReplyLine
{
    enum class Kind
    {
        Forward,   // a data or info line: sent verbatim, decides nothing
        Ok,        // a terminal success line
        Refused,   // a terminal refusal line
    };
    Kind kind = Kind::Forward;
    std::string name;   // the bot an ok line names, else empty
    std::string code;   // refused only
    std::string text;   // the ok detail, the refusal message, or a raw autogear line
};

inline bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// One captured Say/Refuse line onto the verdict. The shaped lines are `TortoiseBots: <verb> [<name>]: ok <detail>` and
// `TortoiseBots: <verb>: refused <code>: <text>`; `who` prints no `: ok` line, so its count line is the success signal.
// Autogear also answers a few raw lines (usage/pool/disabled/dead/unknown option, the `Re-geared` ok, `Warning:` info) that
// map onto the same verdict without changing the chat text.
inline ReplyLine ParseReplyLine(std::string_view verb, std::string_view line)
{
    ReplyLine out;
    std::string const canonical = CanonicalVerb(verb);
    std::string const prefix = "TortoiseBots: " + canonical;
    if (StartsWith(line, prefix) &&
        (line.size() == prefix.size() || line[prefix.size()] == ':' || line[prefix.size()] == ' '))
    {
        std::string_view rest = line.substr(prefix.size());
        std::string name;
        if (!rest.empty() && rest.front() == ':')
            rest.remove_prefix(1);
        else if (!rest.empty() && rest.front() == ' ')
        {
            rest.remove_prefix(1);
            std::size_t const colon = rest.find(':');
            if (colon == std::string_view::npos)
                return out;
            name = std::string(rest.substr(0, colon));
            rest.remove_prefix(colon + 1);
        }
        else if (!rest.empty())
            return out;
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
        if (canonical == "who" && EndsWith(rest, "bot(s)"))
        {
            out.kind = ReplyLine::Kind::Ok;
            return out;
        }
        if (rest == "ok" || StartsWith(rest, "ok "))
        {
            out.kind = ReplyLine::Kind::Ok;
            out.name = name;
            out.text = rest.size() > 3 ? std::string(rest.substr(3)) : std::string();
            return out;
        }
        if (StartsWith(rest, "refused "))
        {
            std::string_view tail = rest.substr(8);
            std::size_t const colon = tail.find(':');
            out.kind = ReplyLine::Kind::Refused;
            out.code = colon == std::string_view::npos ? std::string(tail) : std::string(tail.substr(0, colon));
            if (colon != std::string_view::npos)
            {
                std::string_view message = tail.substr(colon + 1);
                while (!message.empty() && message.front() == ' ')
                    message.remove_prefix(1);
                out.text = std::string(message);
            }
            return out;
        }
        return out;
    }
    if (canonical == "autogear")
    {
        if (StartsWith(line, "Re-geared "))
        {
            std::string_view tail = line.substr(10);
            std::size_t const space = tail.find(' ');
            out.kind = ReplyLine::Kind::Ok;
            out.name = space == std::string_view::npos ? std::string(tail) : std::string(tail.substr(0, space));
            out.text = std::string(line);
            return out;
        }
        if (StartsWith(line, "Warning: "))
            return out;
        if (StartsWith(line, "Usage: .bot autogear"))
        {
            out.kind = ReplyLine::Kind::Refused;
            out.code = "usage";
            out.text = std::string(line);
            return out;
        }
        if (line.find("keeps progressing organically") != std::string_view::npos)
        {
            out.kind = ReplyLine::Kind::Refused;
            out.code = "pool";
            out.text = std::string(line);
            return out;
        }
        if (StartsWith(line, "Owned-bot autogear is disabled"))
        {
            out.kind = ReplyLine::Kind::Refused;
            out.code = "disabled";
            out.text = std::string(line);
            return out;
        }
        if (StartsWith(line, "Bot '") && line.find("must be alive") != std::string_view::npos)
        {
            out.kind = ReplyLine::Kind::Refused;
            out.code = "dead";
            out.text = std::string(line);
            return out;
        }
        if (StartsWith(line, "Unknown autogear option"))
        {
            out.kind = ReplyLine::Kind::Refused;
            out.code = "usage";
            out.text = std::string(line);
            return out;
        }
    }
    return out;
}

// A `who` data row names one bot (`TortoiseBots: who: <name> level ...`); the count line does not.
inline bool IsWhoRow(std::string_view line)
{
    std::string_view const prefix = "TortoiseBots: who: ";
    if (!StartsWith(line, prefix))
        return false;
    return line.substr(prefix.size()).find(" level ") != std::string_view::npos;
}

struct Outcome
{
    bool answered = false;   // a terminal line was captured; without one the host sends nothing
    bool ok = false;         // Ack when true, Err when false
    std::string scope;       // Ack only: bot:<name> | party | roster
    std::uint32_t count = 0; // Ack only
    std::string detail = "-";// Ack only: the ok tail, or "-" for `who`
    std::string code;        // Err only
    std::string message;     // Err only
};

// The first terminal line decides; `who` counts its forwarded data rows.
inline Outcome Summarize(std::string_view verb, std::vector<std::string> const& lines)
{
    Outcome out;
    std::string const canonical = CanonicalVerb(verb);
    bool decided = false;
    std::string name;
    std::uint32_t whoRows = 0;
    for (std::string const& line : lines)
    {
        ReplyLine const parsed = ParseReplyLine(verb, line);
        if (canonical == "who" && parsed.kind == ReplyLine::Kind::Forward && IsWhoRow(line))
            ++whoRows;
        if (decided || (parsed.kind != ReplyLine::Kind::Ok && parsed.kind != ReplyLine::Kind::Refused))
            continue;
        decided = true;
        out.answered = true;
        out.ok = parsed.kind == ReplyLine::Kind::Ok;
        name = parsed.name;
        if (out.ok)
        {
            if (!parsed.text.empty())
                out.detail = parsed.text;
        }
        else
        {
            out.code = parsed.code;
            out.message = parsed.text;
        }
    }
    if (!out.answered || !out.ok)
        return out;
    if (canonical == "who")
    {
        out.scope = "roster";
        out.count = whoRows;
        out.detail = "-";
        return out;
    }
    if (canonical == "add")
    {
        out.scope = "party";
        out.count = 1;
        return out;
    }
    out.scope = name.empty() ? "party" : "bot:" + name;
    out.count = 1;
    return out;
}

} // namespace addonverbs
} // namespace TortoiseBots
