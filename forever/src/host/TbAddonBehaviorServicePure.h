// Pure grammar, allowlist and wire formatters for the `behavior` and `service`
// addon verbs (TBF #133 piece c-behsvc). No core includes; tested standalone
// by forever/tools/test_addon_behsvc.cpp.
#pragma once

#include "ai/playerbot/MagePortalPolicy.h"
#include "ai/playerbot/strategy/generic/OffSpecPolicy.h"
#include "host/TbAddonPure.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace behsvc
{

enum class BehaviorState : std::uint8_t
{
    Combat = 0,
    NonCombat = 1,
    Reaction = 2,
};

struct BehaviorToggle
{
    char const* key;
    char const* strategy;
    BehaviorState state;
    bool inverted = false;
    bool offSpec = false;
};

inline BehaviorToggle const* BehaviorToggles(std::size_t& outCount)
{
    static constexpr BehaviorToggle kToggles[] = {
        { "loot",     "loot",              BehaviorState::NonCombat, false, false },
        { "aoe",      "no aoe",            BehaviorState::Combat,    true,  false },
        { "autocc",   "auto cc",           BehaviorState::Combat,    false, false },
        { "savemana", "conserve mana",     BehaviorState::Combat,    false, false },
        { "boost",    "boost",             BehaviorState::Combat,    false, false },
        { "threat",   "threat",            BehaviorState::Combat,    false, false },
        { "potions",  "potions",           BehaviorState::Reaction,  false, false },
        { "mimic",    "mimic consumables", BehaviorState::NonCombat, false, false },
        { "spread",   "spread",            BehaviorState::Combat,    false, false },
        { "hold",     "hold position",     BehaviorState::Combat,    false, false },
        { "noblink",  "no blink instance", BehaviorState::Combat,    false, false },
        { "offheal",  "offheal",           BehaviorState::Combat,    false, true  },
        { "offdps",   "offdps",            BehaviorState::Combat,    false, true  },
    };
    outCount = sizeof(kToggles) / sizeof(kToggles[0]);
    return kToggles;
}

inline BehaviorToggle const* FindBehaviorToggle(std::string_view key)
{
    std::size_t count = 0;
    BehaviorToggle const* toggles = BehaviorToggles(count);
    for (std::size_t i = 0; i < count; ++i)
        if (key == toggles[i].key)
            return &toggles[i];
    return nullptr;
}

inline bool ClassAllowsToggle(std::uint8_t classId, BehaviorToggle const& toggle)
{
    return !toggle.offSpec || ai::ClassHasOffSpec(classId);
}

inline char const* const* SpecStrategies(std::size_t& outCount)
{
    static constexpr char const* kSpecs[] = {
        "tank feral", "dps feral", "balance", "restoration", "protection", "holy", "retribution", "arms", "fury",
        "beast mastery", "marksmanship", "survival", "combat", "assassination", "subtlety", "shadow", "discipline",
        "offdps", "offheal", "elemental", "enhancement", "tank shaman", "frost", "fire", "arcane", "affliction",
        "demonology", "destruction",
    };
    outCount = sizeof(kSpecs) / sizeof(kSpecs[0]);
    return kSpecs;
}

inline std::string Lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string Trim(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return std::string(text.substr(begin, end - begin));
}

inline std::string NextWord(std::string_view& rest)
{
    std::size_t pos = 0;
    while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t'))
        ++pos;
    std::size_t start = pos;
    while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '\t')
        ++pos;
    std::string word(rest.substr(start, pos - start));
    rest.remove_prefix(pos);
    return word;
}

struct ParsedBehavior
{
    bool ok = false;
    bool isStatus = false;
    bool isPartyScope = false;
    std::string botName;
    std::string key;
    bool enable = false;
    std::string intent = "behavior";
};

inline char const* BehaviorUsage()
{
    return "Usage: .bot behavior <bot|party|raid> <loot|aoe|autocc|savemana|boost|threat|potions|mimic|spread|hold|noblink|offheal|offdps> <on|off>";
}

inline ParsedBehavior ParseBehaviorArgs(std::string_view args)
{
    ParsedBehavior out;
    std::string_view rest = args;
    std::string const target = NextWord(rest);
    std::string const keyRaw = NextWord(rest);
    std::string const modeRaw = NextWord(rest);
    std::string const extra = NextWord(rest);

    if (target.empty() || !extra.empty())
    {
        if (!keyRaw.empty())
            out.intent = "behavior " + Lower(keyRaw);
        else
            out.intent = "behavior ?";
        return out;
    }

    std::string const loweredTarget = Lower(target);
    out.isPartyScope = (loweredTarget == "party" || loweredTarget == "raid");
    out.botName = target;

    std::string const key = Lower(keyRaw);
    std::string const mode = Lower(modeRaw);

    if (key.empty() || (key == "status" && mode.empty()))
    {
        out.ok = true;
        out.isStatus = true;
        out.intent = "behavior";
        return out;
    }

    out.intent = "behavior " + key;
    if (!FindBehaviorToggle(key))
        return out;
    if (mode != "on" && mode != "off")
        return out;

    out.ok = true;
    out.key = key;
    out.enable = (mode == "on");
    return out;
}

struct ParsedService
{
    bool ok = false;
    bool isQuery = false;
    std::string botName;
    std::uint32_t spellId = 0;
    std::string intent = "service portal";
    std::string errCode = "invalid";
    std::string errMsg;
};

inline char const* ServiceUsage()
{
    return "Usage: .bot service <bot> [portal <spellId|city>]";
}

inline ParsedService ParseServiceArgs(std::string_view args)
{
    ParsedService out;
    std::string_view rest = args;
    std::string const botName = NextWord(rest);
    if (botName.empty())
    {
        out.intent = "service";
        out.errCode = "invalid";
        out.errMsg = ServiceUsage();
        return out;
    }
    out.botName = botName;

    std::string const verb = Lower(NextWord(rest));
    if (verb.empty())
    {
        out.ok = true;
        out.isQuery = true;
        out.intent = "service";
        return out;
    }

    if (verb != "portal")
    {
        out.intent = "service portal";
        out.errCode = "invalid";
        out.errMsg = ServiceUsage();
        return out;
    }

    std::string const arg = Trim(rest);
    if (arg.empty())
    {
        out.intent = "service portal";
        out.errCode = "unknown";
        out.errMsg = "Unknown portal city.";
        return out;
    }

    bool allDigits = true;
    for (char c : arg)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            allDigits = false;
            break;
        }
    }

    if (allDigits)
    {
        std::uint64_t val = 0;
        for (char c : arg)
        {
            val = val * 10 + static_cast<std::uint64_t>(c - '0');
            if (val > 0xFFFFFFFFULL)
            {
                val = 0;
                break;
            }
        }
        if (!val)
        {
            out.intent = "service portal";
            out.errCode = "unknown";
            out.errMsg = "Unknown portal city.";
            return out;
        }
        out.ok = true;
        out.spellId = static_cast<std::uint32_t>(val);
        return out;
    }

    std::uint32_t const matched = ai::MatchPortalCity(arg);
    if (!matched)
    {
        out.intent = "service portal";
        out.errCode = "unknown";
        out.errMsg = "Unknown portal city.";
        return out;
    }
    out.ok = true;
    out.spellId = matched;
    return out;
}

inline std::string FormatBotStateLine(std::string_view botName, std::string_view fields)
{
    // The addon channel caps at 255 bytes and the state parser reads each
    // comma field on its own, so a long tail drops whole trailing fields —
    // focus first, then the spec list — never a cut mid-field. The move and
    // toggle fields the panel needs always survive (bounded, up front).
    std::string tail(fields);
    for (char const* optional : { ",focus=", ",specs=" })
    {
        std::string line = "TBM:BOTSTATE|" + addon::pure::Safe(botName) + "|" + tail;
        if (line.size() <= addon::pure::kMaxLineBytes)
            return line;
        std::size_t const cut = tail.rfind(optional);
        if (cut == std::string::npos)
            return line;
        tail.erase(cut);
    }
    return "TBM:BOTSTATE|" + addon::pure::Safe(botName) + "|" + tail;
}

inline std::string FormatServiceLine(std::string_view botName, bool food, bool water, bool healthstone,
    std::string_view portals)
{
    // The portal list is the only unbounded tail (a portal per city the mage
    // trained). A truncated mid-entry id would fail the entry parse and, with
    // no valid entry left, the whole line — so the line keeps the longest
    // entry-boundary prefix that fits, and "-" when even one entry will not.
    std::string const head = "TBM:SERVICE|" + addon::pure::Safe(botName) + "|" + (food ? "1" : "0") + "|" +
        (water ? "1" : "0") + "|" + (healthstone ? "1" : "0") + "|";
    std::string kept;
    std::size_t begin = 0;
    while (begin < portals.size())
    {
        std::size_t const end = portals.find(',', begin);
        std::string const entry = std::string(portals.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        std::string const candidate = kept.empty() ? entry : kept + "," + entry;
        if ((head + candidate).size() > addon::pure::kMaxLineBytes)
            break;
        kept = candidate;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    if (kept.empty() && !portals.empty() && portals != "-")
        return head + "-";
    return head + (kept.empty() ? "-" : kept);
}

} // namespace behsvc
} // namespace TortoiseBots
