#pragma once

// Pure rules of the Lua API (host/LuaBindings.cpp, docs/developers/lua-api.md): argument checks, the
// `TB.Command` text rules, the reply buffer, the callback ledger and the plain-table field lists. Standard
// library only, so tools/test_lua_api_policy.cpp runs it without the core and without Eluna.
//
// Nothing here knows about Lua: the binding turns a Fields list into a Lua table of plain numbers, booleans
// and strings. No game object, no pointer and no userdata ever reaches a script.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace LuaApi {

constexpr size_t kMaxCommandLength = 255;
constexpr size_t kMaxReplyLines = 40;
constexpr size_t kMaxReplyLineLength = 400;
// Callbacks a script may have waiting at once, and how long one waits for its reply before it is dropped.
constexpr size_t kMaxPendingCallbacks = 64;
constexpr uint32_t kCallbackExpiryMs = 60000;

// ---------------------------------------------------------------------------------------------------------
// Arguments

// A guid counter (the low part of a player guid): a whole number from 1 to 2^32 - 1. Lua numbers are doubles;
// 1.5, NaN, 0, negatives and anything past 32 bits are refused rather than rounded.
inline bool ParseGuidCounter(double value, uint32_t& out)
{
    if (!(value >= 1.0) || value > 4294967295.0)
        return false;
    uint32_t const whole = static_cast<uint32_t>(value);
    if (static_cast<double>(whole) != value)
        return false;
    out = whole;
    return true;
}

// `TB.Command` text -> what the `.bot` command queue takes. Accepted forms: `.bot hire x`, `bot hire x`,
// `hire x` (a bare `.bot` subcommand) and `autogear ...` / `.autogear ...` (the top-level player command).
// Everything else is the same text as typed in chat, so TB's own parser and permission checks decide.
struct CommandRequest
{
    bool ok = false;
    std::string args;        // for the queue: the text after `.bot`, or the full `autogear ...` when topLevel
    bool topLevel = false;
    char const* error = "";  // short code when !ok
};

inline CommandRequest ParseCommand(std::string const& text)
{
    CommandRequest request;
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    if (begin == end)
    {
        request.error = "empty";
        return request;
    }
    if (end - begin > kMaxCommandLength)
    {
        request.error = "too_long";
        return request;
    }
    for (size_t i = begin; i < end; ++i)
    {
        unsigned char const c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7f)
        {
            request.error = "bad_char";
            return request;
        }
    }
    std::string body = text.substr(begin, end - begin);
    if (body[0] == '.')
        body.erase(0, 1);

    size_t wordEnd = 0;
    while (wordEnd < body.size() && body[wordEnd] != ' ' && body[wordEnd] != '\t')
        ++wordEnd;
    std::string word = body.substr(0, wordEnd);
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t restBegin = wordEnd;
    while (restBegin < body.size() && (body[restBegin] == ' ' || body[restBegin] == '\t'))
        ++restBegin;
    std::string const rest = body.substr(restBegin);

    if (word == "autogear")
    {
        request.ok = true;
        request.topLevel = true;
        request.args = rest.empty() ? "autogear" : "autogear " + rest;
        return request;
    }
    if (word == "bot")
    {
        if (rest.empty())
        {
            request.error = "empty";
            return request;
        }
        request.ok = true;
        request.args = rest;
        return request;
    }
    request.ok = true;
    request.args = body;
    return request;
}

// ---------------------------------------------------------------------------------------------------------
// Reply buffer: what a command answered, as lines, bounded.

class ReplyBuffer
{
public:
    // Splits on newlines, drops empty lines, cuts a long line, keeps the first kMaxReplyLines.
    void Append(char const* text)
    {
        if (!text)
            return;
        std::string const all(text);
        size_t start = 0;
        while (start <= all.size())
        {
            size_t const stop = all.find('\n', start);
            std::string line = all.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
            {
                if (m_lines.size() >= kMaxReplyLines)
                    m_truncated = true;
                else
                {
                    if (line.size() > kMaxReplyLineLength)
                    {
                        line.resize(kMaxReplyLineLength);
                        m_truncated = true;
                    }
                    m_lines.push_back(std::move(line));
                }
            }
            if (stop == std::string::npos)
                break;
            start = stop + 1;
        }
    }
    std::vector<std::string> const& Lines() const { return m_lines; }
    bool Truncated() const { return m_truncated; }

private:
    std::vector<std::string> m_lines;
    bool m_truncated = false;
};

// ---------------------------------------------------------------------------------------------------------
// Callback ledger: command ids whose script waits for a reply. A script that never gets one (the requester
// logged out, the state was reloaded) must not leave a function in the Lua registry for ever.

class CallbackLedger
{
public:
    // False when kMaxPendingCallbacks are already waiting or the id is taken.
    bool Add(uint64_t id, uint32_t nowMs)
    {
        if (m_pending.size() >= kMaxPendingCallbacks)
            return false;
        return m_pending.emplace(id, nowMs).second;
    }
    // True when the id was waiting; it no longer is.
    bool Take(uint64_t id) { return m_pending.erase(id) != 0; }
    bool Has(uint64_t id) const { return m_pending.count(id) != 0; }
    size_t Size() const { return m_pending.size(); }
    // Ids waiting longer than kCallbackExpiryMs, removed and appended to `expired`.
    void Expire(uint32_t nowMs, std::vector<uint64_t>& expired)
    {
        for (auto it = m_pending.begin(); it != m_pending.end();)
        {
            if (static_cast<uint32_t>(nowMs - it->second) > kCallbackExpiryMs)
            {
                expired.push_back(it->first);
                it = m_pending.erase(it);
            }
            else
                ++it;
        }
    }

private:
    std::unordered_map<uint64_t, uint32_t> m_pending;
};

// ---------------------------------------------------------------------------------------------------------
// Plain tables: an ordered field list the binding turns into a Lua table.

struct Field
{
    enum class Kind : uint8_t { Number, Bool, String };
    std::string key;
    Kind kind = Kind::Number;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

using Fields = std::vector<Field>;

inline void AddNumber(Fields& out, char const* key, double value)
{
    Field f;
    f.key = key;
    f.kind = Field::Kind::Number;
    f.number = value;
    out.push_back(std::move(f));
}
inline void AddBool(Fields& out, char const* key, bool value)
{
    Field f;
    f.key = key;
    f.kind = Field::Kind::Bool;
    f.boolean = value;
    out.push_back(std::move(f));
}
inline void AddString(Fields& out, char const* key, std::string value)
{
    Field f;
    f.key = key;
    f.kind = Field::Kind::String;
    f.text = std::move(value);
    out.push_back(std::move(f));
}

// A bot's row, copied from a BotTelemetrySnapshot (any type with these members): the same facts the dashboard
// shows. Strings are copied, so a script never holds a reference into the module.
template <class Snapshot>
Fields BotFields(Snapshot const& s)
{
    Fields out;
    AddNumber(out, "guid", s.guid);
    AddString(out, "name", s.name);
    AddString(out, "class", s.className);
    AddString(out, "role", s.role);
    AddString(out, "kind", s.kind);
    AddNumber(out, "level", s.level);
    AddNumber(out, "hp", s.hp);
    AddNumber(out, "maxHp", s.maxHp);
    AddNumber(out, "power", s.power);
    AddNumber(out, "maxPower", s.maxPower);
    AddString(out, "powerType", s.powerType);
    AddNumber(out, "map", s.mapId);
    AddNumber(out, "zone", s.zoneId);
    AddNumber(out, "instance", s.instanceId);
    AddBool(out, "raidMap", s.raidMap);
    AddNumber(out, "group", s.groupId);
    AddNumber(out, "subgroup", s.subgroupId);
    AddNumber(out, "x", s.x);
    AddNumber(out, "y", s.y);
    AddNumber(out, "z", s.z);
    AddNumber(out, "o", s.o);
    AddString(out, "target", s.target);
    AddString(out, "strategy", s.strategy);
    AddString(out, "state", s.state);
    AddString(out, "lastAction", s.lastAction);
    AddString(out, "travelTo", s.travelTo);
    AddBool(out, "isAlt", s.isAlt);
    return out;
}

// What the run leader reports about the run a commander owns (DungeonRun::StatusParts plus the pass and the
// elapsed time). `active` is false and the rest empty when the commander owns no run.
struct RunView
{
    bool active = false;
    std::string plan;
    std::string state;
    std::string leg;
    std::string kind;
    std::string phase;
    std::string note;
    uint32_t step = 0;
    uint32_t steps = 0;
    uint32_t pass = 0;
    uint64_t elapsedMs = 0;
};

inline Fields RunFields(RunView const& r)
{
    Fields out;
    AddBool(out, "active", r.active);
    if (!r.active)
        return out;
    AddString(out, "plan", r.plan);
    AddString(out, "state", r.state);
    AddString(out, "leg", r.leg);
    AddString(out, "kind", r.kind);
    AddString(out, "phase", r.phase);
    AddString(out, "note", r.note);
    AddNumber(out, "step", r.step);
    AddNumber(out, "steps", r.steps);
    AddNumber(out, "pass", r.pass);
    AddNumber(out, "elapsedMs", static_cast<double>(r.elapsedMs));
    return out;
}

} // namespace LuaApi
} // namespace TortoiseBots
