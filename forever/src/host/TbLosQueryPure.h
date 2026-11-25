// `.tb los` (#308): the rules of the server line-of-sight query, core-free so a unit test can prove them
// (forever/tools/test_los_query.cpp). The handler is host/TbLosQuery.cpp; here live the argument forms, the one-line
// answer's fields, the refusal codes, the per-caller rate limit and the access rule.
//
// The command answers ONE line (forever/docs/TB-COMMANDS.md, section 24): `los a=.. b=.. map=.. los=.. dist=.. same_map=..
// in_world=.. [reason=..]`. A side is a character/bot name, `target` (the caller's selection), a 32-hex creature/character
// guid as `.tb state` prints it (or the core's `0x...` form), or a position `x,y,z` on the other side's map (two positions
// need `map=<id>`).
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace losq
{
// A character name, as this core allows one (letters and digits, never a leading digit). Generous on length: the core
// enforces its own limit at creation, and a name that is too long is simply not found in the world.
constexpr unsigned kMaxNameLength = 24;

// The map bound of a `map=<id>` word: no map of this core is above 65535, and map 0 does not exist.
constexpr std::uint32_t kMaxMapId = 65535;

// Why a query was refused. The code is the `reason=` word of the answer and a stable interface for tools.
enum class Reason
{
    None,
    BadArgument,
    UnknownUnit,
    NotInWorld,
    DifferentMaps,
    NoAccess,
    Rate,
};
inline char const* ReasonCode(Reason reason)
{
    switch (reason)
    {
        case Reason::None:          return nullptr;
        case Reason::BadArgument:   return "bad-argument";
        case Reason::UnknownUnit:   return "unknown-unit";
        case Reason::NotInWorld:    return "not-in-world";
        case Reason::DifferentMaps: return "different-maps";
        case Reason::NoAccess:      return "no-access";
        case Reason::Rate:          return "rate";
    }
    return nullptr;
}

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class SideKind
{
    Name,       // a character or bot name, looked up in the world
    Target,     // the caller's current selection; the console has none
    Guid,       // a guid as it is printed (32 hex digits, or 0x and 32 hex)
    Position,   // x,y,z on the other side's map
};

// One side of the query, as typed. A guid is kept as its two 64-bit halves (high first, the order GuidText prints).
struct Side
{
    SideKind kind = SideKind::Name;
    std::string word;           // the word as typed (the name to look up, or `target`)
    std::uint64_t guidHigh = 0;
    std::uint64_t guidLow = 0;
    Vec3 pos;
};

struct Query
{
    Side a;
    Side b;
    bool hasMap = false;
    std::uint32_t mapId = 0;    // `map=<id>`: a position-position query only
};

struct QueryResult
{
    Query query;
    Reason reason = Reason::None;   // BadArgument when the words are not a query
};

// One word as a side. False = not a side (the caller answers `bad-argument`).
bool ParseSide(std::string_view word, Side& out);

// `.tb los <a> <b> [map=<id>]`. Exactly two sides; a third word must be `map=` with 1-65535, and only two positions
// may carry one (a position with a unit side sits on that unit's map; either way nothing is guessed).
QueryResult ParseQuery(std::vector<std::string> const& words);

// Five queries a second per caller: a bucket of five tokens, one token back every 200 ms, no burst above five. A clock
// that stands still or goes backwards refills nothing.
class RateBucket
{
public:
    static constexpr std::uint32_t kCapacity = 5;
    static constexpr std::uint32_t kRefillMs = 200;
    static constexpr std::uint64_t kIdleMs = 1000;

    bool Take(std::uint64_t nowMs);
    // No token taken for a second and full again: the caller's entry can be dropped.
    bool Idle(std::uint64_t nowMs) const;
    std::uint32_t Tokens() const { return _tokens; }

private:
    std::uint32_t _tokens = kCapacity;
    std::uint64_t _lastMs = 0;
    bool _started = false;
};

// One side of the access rule, after resolution: whether it is a unit at all, whether that unit is a player character,
// whether it is a host bot, whether it is the caller's own character, and whether the shared `.tb run` check
// (CheckRunObserverAccess / CheckPartyLeadAccess) allows this caller to read that bot.
// The observer check logs an INFO line for a GameMaster, so a GameMaster caller skips it (the access rule answers `true`
// for a GameMaster anyway) and a polling tool cannot flood the log.
struct AccessSide
{
    bool isUnit = false;
    bool isPlayerCharacter = false;
    bool isHostBot = false;
    bool isOwnCharacter = false;
    bool botAccessAllowed = false;
};

// Console and GameMaster always. Anyone else needs one unit of their own - their character, or a host bot in a run or
// party they lead or observe - and may never name another player's character, so the command cannot probe players.
bool AccessAllowed(bool senderIsGm, std::vector<AccessSide> const& sides);

// What a side turned out to be once resolved.
struct SideState
{
    bool isPosition = false;    // a position side (no unit)
    bool found = false;         // a unit side: a unit was found; a position side: always true
    bool inWorld = false;       // the unit is in the world; the map id of a position is loaded
    bool mapKnown = false;
    std::uint32_t mapId = 0;
    bool instanceKnown = false; // the copy of that map: a unit's own, or a position's (its unit side's, or the loaded copy's)
    std::uint32_t instanceId = 0;
};

// One place: the same map and, when both instances are known, the same instance of it. Two copies of one dungeon are not
// one place even though their map id is equal, so every "same map" question (the refusal and the `same_map` field) asks
// this one function.
bool SamePlace(SideState const& a, SideState const& b);

// The query's facts: the decision order is resolution, authorization, world state, maps (instances included), so a caller
// that may not read a unit never learns anything about it. `Rate` is decided before this, by the caller's bucket.
struct Facts
{
    SideState a;
    SideState b;
    bool accessAllowed = false;
};
Reason Decide(Facts const& facts);

// What the caller is told. A caller below GameMaster never learns whether a side was another player's character or a
// name that resolved to nothing: both answer `no-access` (the same code, the same fields), so the command cannot be used
// as a presence or map oracle. A `no-access` answer also carries no resolved name, map, `same_map` or `in_world`: only
// the words the caller typed. The GameMaster and the console keep the precise `unknown-unit`.
Reason SeenByCaller(bool senderIsGm, Reason reason);
bool AnswerHidesWorld(Reason reason);

// The line's values. A position side prints as `x,y,z` with two decimals (the grammar's position form); a resolved
// unit as its name; an unresolved guid as its 32 hex digits; a name or `target` it could not resolve as typed.
std::string FormatCoord(Vec3 const& pos);
std::string FormatGuid(std::uint64_t high, std::uint64_t low);
// The horizontal (x/y) distance between two points, in yards. Height is ignored: `dist` is what a reach comparison uses,
// so a difference in z is never added to it (a position-position answer is the plain centre-to-centre distance).
inline float HorizontalDistance(Vec3 const& a, Vec3 const& b)
{
    float const dx = b.x - a.x;
    float const dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

// "" for a distance that was not measured (a refusal before both sides were placed).
std::string FormatDist(float yards);
std::string DisplaySide(Side const& side, std::string const& resolvedName);
}
}

namespace TortoiseBots
{
namespace losq
{
namespace detail
{
inline bool IsNameChar(char c, bool first)
{
    unsigned char const u = static_cast<unsigned char>(c);
    if (u >= 0x80)
        return false;
    return first ? std::isalpha(u) != 0 : std::isalnum(u) != 0;
}

inline bool ParseFloatExact(std::string_view text, float& out)
{
    if (text.empty())
        return false;
    std::string const copy(text);
    char* end = nullptr;
    float const value = std::strtof(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

// Three comma-separated numbers, nothing between or after them.
inline bool ParseCoords(std::string_view text, Vec3& out)
{
    float values[3];
    size_t start = 0;
    for (int part = 0; part < 3; ++part)
    {
        size_t const comma = text.find(',', start);
        if (part < 2 ? comma == std::string_view::npos : comma != std::string_view::npos)
            return false;
        std::string_view const piece = text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (!ParseFloatExact(piece, values[part]))
            return false;
        start = comma + 1;
    }
    out = { values[0], values[1], values[2] };
    return true;
}

inline bool ParseGuid(std::string_view text, std::uint64_t& high, std::uint64_t& low)
{
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.remove_prefix(2);
    if (text.size() != 32)
        return false;
    for (char c : text)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    std::string const highText(text.substr(0, 16));
    std::string const lowText(text.substr(16, 16));
    high = std::strtoull(highText.c_str(), nullptr, 16);
    low = std::strtoull(lowText.c_str(), nullptr, 16);
    return (high | low) != 0;      // the empty guid is not a unit
}

inline std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}

inline bool ParseSide(std::string_view word, Side& out)
{
    out = Side();
    out.word = std::string(word);
    if (word.empty())
        return false;
    if (detail::Lower(word) == "target")
    {
        out.kind = SideKind::Target;
        return true;
    }
    if (word.find(',') != std::string_view::npos)
    {
        if (!detail::ParseCoords(word, out.pos))
            return false;
        out.kind = SideKind::Position;
        return true;
    }
    if (detail::ParseGuid(word, out.guidHigh, out.guidLow))
    {
        out.kind = SideKind::Guid;
        return true;
    }
    if (word.size() > kMaxNameLength)
        return false;
    for (size_t i = 0; i < word.size(); ++i)
        if (!detail::IsNameChar(word[i], i == 0))
            return false;
    out.kind = SideKind::Name;
    return true;
}

inline QueryResult ParseQuery(std::vector<std::string> const& words)
{
    QueryResult result;
    if (words.size() < 2 || words.size() > 3)
    {
        result.reason = Reason::BadArgument;
        return result;
    }
    if (!ParseSide(words[0], result.query.a) || !ParseSide(words[1], result.query.b))
    {
        result.reason = Reason::BadArgument;
        return result;
    }
    bool const bothPositions = result.query.a.kind == SideKind::Position && result.query.b.kind == SideKind::Position;
    if (bothPositions && words.size() != 3)
    {
        // Two positions are on no side's map: without `map=` there is no map to query, and guessing one is worse than
        // saying so.
        result.reason = Reason::BadArgument;
        return result;
    }
    if (words.size() == 3)
    {
        std::string_view const word = words[2];
        if (word.substr(0, 4) != "map=" || word.size() == 4)
        {
            result.reason = Reason::BadArgument;
            return result;
        }
        std::string_view const digits = word.substr(4);
        for (char c : digits)
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                result.reason = Reason::BadArgument;
                return result;
            }
        std::string const copy(digits);
        unsigned long long const id = std::strtoull(copy.c_str(), nullptr, 10);
        // Only two positions need a map: a position with a unit side sits on that unit's map, and a unit's own map is
        // read from the unit, never overridden by an argument.
        if (id < 1 || id > kMaxMapId || !bothPositions)
        {
            result.reason = Reason::BadArgument;
            return result;
        }
        result.query.hasMap = true;
        result.query.mapId = static_cast<std::uint32_t>(id);
    }
    return result;
}

inline bool RateBucket::Take(std::uint64_t nowMs)
{
    if (!_started)
    {
        _started = true;
        _lastMs = nowMs;
        _tokens = kCapacity;
    }
    std::uint64_t const elapsed = nowMs > _lastMs ? nowMs - _lastMs : 0;
    std::uint64_t const slices = elapsed / kRefillMs;
    if (slices)
    {
        _lastMs += slices * kRefillMs;
        std::uint64_t const refilled = std::uint64_t(_tokens) + slices;
        _tokens = refilled > kCapacity ? kCapacity : static_cast<std::uint32_t>(refilled);
    }
    if (_tokens == 0)
        return false;
    --_tokens;
    return true;
}

inline bool RateBucket::Idle(std::uint64_t nowMs) const
{
    if (!_started)
        return true;
    std::uint64_t const elapsed = nowMs > _lastMs ? nowMs - _lastMs : 0;
    std::uint64_t const refilled = std::uint64_t(_tokens) + elapsed / kRefillMs;
    return refilled >= kCapacity && elapsed >= kIdleMs;
}

inline bool AccessAllowed(bool senderIsGm, std::vector<AccessSide> const& sides)
{
    if (senderIsGm)
        return true;
    bool granted = false;
    for (AccessSide const& side : sides)
    {
        if (!side.isUnit || !side.isPlayerCharacter)
            continue;                               // a mob, or a position: neither is anybody's to read
        if (side.isOwnCharacter)
        {
            granted = true;
            continue;
        }
        if (!side.isHostBot || !side.botAccessAllowed)
            return false;                           // another player's character, or a bot outside this caller's run or party
        granted = true;
    }
    return granted;
}

inline bool SamePlace(SideState const& a, SideState const& b)
{
    if (!a.mapKnown || !b.mapKnown || a.mapId != b.mapId)
        return false;
    return !a.instanceKnown || !b.instanceKnown || a.instanceId == b.instanceId;
}

inline Reason Decide(Facts const& facts)
{
    if (!facts.a.found || !facts.b.found)
        return Reason::UnknownUnit;
    if (!facts.accessAllowed)
        return Reason::NoAccess;
    if (!facts.a.inWorld || !facts.b.inWorld)
        return Reason::NotInWorld;
    if (facts.a.mapKnown && facts.b.mapKnown && !SamePlace(facts.a, facts.b))
        return Reason::DifferentMaps;
    return Reason::None;
}

inline Reason SeenByCaller(bool senderIsGm, Reason reason)
{
    return !senderIsGm && reason == Reason::UnknownUnit ? Reason::NoAccess : reason;
}

inline bool AnswerHidesWorld(Reason reason) { return reason == Reason::NoAccess; }

inline std::string FormatCoord(Vec3 const& pos)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.2f,%.2f,%.2f", pos.x, pos.y, pos.z);
    return buffer;
}

inline std::string FormatGuid(std::uint64_t high, std::uint64_t low)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%016llX%016llX", static_cast<unsigned long long>(high),
                  static_cast<unsigned long long>(low));
    return buffer;
}

inline std::string FormatDist(float yards)
{
    if (!(yards >= 0.0f))
        return {};
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f", yards);
    return buffer;
}

inline std::string DisplaySide(Side const& side, std::string const& resolvedName)
{
    if (side.kind == SideKind::Position)
        return FormatCoord(side.pos);
    if (!resolvedName.empty())
        return resolvedName;
    if (side.kind == SideKind::Guid)
        return FormatGuid(side.guidHigh, side.guidLow);
    return side.word;
}
}
}
