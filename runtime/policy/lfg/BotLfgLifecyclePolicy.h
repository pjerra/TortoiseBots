// The life of one bot LFG group (Forming -> Waiting -> Ready -> Ended);
// every end either disbands the bots or, with a real player in the group or
// a commander's binding on it, releases the group untouched.
// Plain data and the standard library only.

#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots::BotLfgLifecycle
{

enum class State : uint8_t { Forming, Waiting, Ready, Ended };
inline char const* StateName(State s)
{
    switch (s)
    {
        case State::Forming: return "forming";
        case State::Waiting: return "waiting";
        case State::Ready: return "ready";
        case State::Ended: return "ended";
    }
    return "ended";
}

enum class FillKind : uint8_t { None, Bot, Player };

enum class EndReason : uint8_t
{
    None, SwitchedOff, TakenOver, PlayerJoined, LeaderLost, MemberLost, NoFill,
    ReadyNoTravel, FormFailed, Shutdown, GmDisband
};
inline char const* EndCode(EndReason r)
{
    switch (r)
    {
        case EndReason::None: return "none";
        case EndReason::SwitchedOff: return "switched-off";
        case EndReason::TakenOver: return "taken-over";
        case EndReason::PlayerJoined: return "player-joined";
        case EndReason::LeaderLost: return "leader-lost";
        case EndReason::MemberLost: return "member-lost";
        case EndReason::NoFill: return "no-fill";
        case EndReason::ReadyNoTravel: return "ready-no-travel";
        case EndReason::FormFailed: return "form-failed";
        case EndReason::Shutdown: return "shutdown";
        case EndReason::GmDisband: return "gm";
    }
    return "none";
}
inline char const* EndText(EndReason r)
{
    switch (r)
    {
        case EndReason::None: return "none";
        case EndReason::SwitchedOff: return "switched off in the config";
        case EndReason::TakenOver: return "a commander bound the group";
        case EndReason::PlayerJoined: return "a player joined while the group was forming";
        case EndReason::LeaderLost: return "the leader left";
        case EndReason::MemberLost: return "a member left and no bot could take its place";
        case EndReason::NoFill: return "no bot of the open role could join";
        case EndReason::ReadyNoTravel: return "the group is full; getting it to the dungeon is not built yet";
        case EndReason::FormFailed: return "the group could not be formed";
        case EndReason::Shutdown: return "server shutdown";
        case EndReason::GmDisband: return "a GameMaster ended it";
    }
    return "none";
}

constexpr uint32_t kTickMs = 5000;
constexpr uint32_t kFillRetryMs = 300000;
constexpr uint32_t kReadyHoldMs = 120000;

// The GM `fill` answer after the immediate try: who took the open slot
// (a bot or a player), else why it waits (the service retries every tick).
inline std::string FillNowAnswer(State after, FillKind filledBy, std::string const& botName,
    std::string const& playerName, std::string const& role)
{
    if (after == State::Ended)
        return "the group ended while filling (see the log line)";
    if (after == State::Ready && (filledBy == FillKind::Player || !playerName.empty()))
        return "a player already took the open slot: " + playerName;
    if (after == State::Ready)
        return "filled by a bot: " + botName + " (" + role + ")";
    return "no free " + role + " bot in the band right now (in combat, flying, dead or busy); "
        "the service tries again every 5 s";
}

// A member out of the world keeps its slot while it may be travelling
// between maps: lost only after missing continuously for this long.
constexpr uint32_t kMissingGraceMs = 30000;
inline bool MissingGraceExpired(uint32_t missingSinceMs, uint32_t nowMs)
{
    return nowMs - missingSinceMs >= kMissingGraceMs;
}

struct Facts
{
    State state = State::Forming;
    uint32_t elapsedMs = 0;
    uint32_t fillAfterMs = 600000;
    bool forceFill = false;
    bool enabled = true;
    bool boundByOther = false;
    bool realPlayerInGroup = false;
    bool leaderLost = false;
    uint32_t lostMembers = 0;
    bool replacementFound = false;
    bool fillCandidateFound = false;
    bool invitePending = false;
};

enum class Step : uint8_t { None, ToWaiting, FillWithBot, Replace, ToReady, End };
struct Decision
{
    Step step = Step::None;
    EndReason reason = EndReason::None;
    FillKind fill = FillKind::None;
    bool release = false;
};

inline bool FillDue(State state, uint32_t elapsedMs, uint32_t fillAfterMs, bool forceFill)
{
    return state == State::Waiting && (forceFill || elapsedMs >= fillAfterMs);
}

inline bool ReleaseOnEnd(bool realPlayerInGroup, EndReason reason)
{
    return realPlayerInGroup || reason == EndReason::TakenOver;
}

inline Decision DecideStep(Facts const& f)
{
    auto End = [](EndReason reason, Facts const& facts) {
        Decision d;
        d.step = Step::End;
        d.reason = reason;
        d.release = ReleaseOnEnd(facts.realPlayerInGroup, reason);
        return d;
    };
    if (!f.enabled)
        return End(EndReason::SwitchedOff, f);
    if (f.boundByOther)
        return End(EndReason::TakenOver, f);
    if (f.realPlayerInGroup)
    {
        if (f.state == State::Forming)
            return End(EndReason::PlayerJoined, f);
        if (f.state == State::Waiting)
        {
            Decision d;
            d.step = Step::ToReady;
            d.fill = FillKind::Player;
            return d;
        }
    }
    if (f.leaderLost)
        return End(EndReason::LeaderLost, f);
    if (f.lostMembers > 0)
    {
        if (f.replacementFound)
        {
            Decision d;
            d.step = Step::Replace;
            return d;
        }
        return End(EndReason::MemberLost, f);
    }
    switch (f.state)
    {
        case State::Forming:
        {
            Decision d;
            d.step = Step::ToWaiting;
            return d;
        }
        case State::Waiting:
        {
            // A player holds the invite for the open slot: neither fill it nor give up on it.
            if (f.invitePending && !f.forceFill)
                return Decision{};
            if (FillDue(f.state, f.elapsedMs, f.fillAfterMs, f.forceFill))
            {
                if (f.fillCandidateFound)
                {
                    Decision d;
                    d.step = Step::FillWithBot;
                    d.fill = FillKind::Bot;
                    return d;
                }
                if (f.elapsedMs >= f.fillAfterMs + kFillRetryMs)
                    return End(EndReason::NoFill, f);
            }
            Decision d;
            return d;
        }
        case State::Ready:
        {
            if (f.elapsedMs >= kReadyHoldMs)
                return End(EndReason::ReadyNoTravel, f);
            Decision d;
            return d;
        }
        case State::Ended:
        {
            Decision d;
            return d;
        }
    }
    Decision d;
    return d;
}

enum class Verb : uint8_t { Invalid, List, Form, Fill, Disband };
struct Args
{
    Verb verb = Verb::Invalid;
    std::string plan;
    uint8_t team = 0;
    uint32_t id = 0;
};

inline std::string LogLine(uint32_t id, std::string const& plan, std::string const& team, std::string const& text)
{
    return "TortoiseBots: bot LFG group " + std::to_string(id) + " (" + plan + ", " + team + "): " + text;
}

inline Args ParseArgs(std::string const& text)
{
    auto Trim = [](std::string const& s) {
        size_t begin = 0;
        while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
            ++begin;
        size_t end = s.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return s.substr(begin, end - begin);
    };
    auto Split = [](std::string const& s) {
        std::vector<std::string> tokens;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            if (i >= s.size())
                break;
            size_t start = i;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            tokens.push_back(s.substr(start, i - start));
        }
        return tokens;
    };
    Args out;
    std::string trimmed = Trim(text);
    if (trimmed.empty())
        return out;
    std::string lowered;
    lowered.reserve(trimmed.size());
    for (char c : trimmed)
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    std::vector<std::string> tokens = Split(lowered);
    if (tokens.empty())
        return out;
    if (tokens[0] == "list")
    {
        if (tokens.size() == 1)
            out.verb = Verb::List;
        return out;
    }
    if (tokens[0] == "form")
    {
        if (tokens.size() > 3)
            return out;
        std::string plan;
        uint8_t team = 0;
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            bool isTeam = tokens[i] == "alliance" || tokens[i] == "horde";
            if (isTeam)
            {
                if (team != 0 || i != tokens.size() - 1)
                    return out;
                team = tokens[i] == "alliance" ? 1 : 2;
                continue;
            }
            if (!plan.empty() || team != 0)
                return out;
            plan = tokens[i];
        }
        out.verb = Verb::Form;
        out.plan = plan;
        out.team = team;
        return out;
    }
    if (tokens[0] == "fill" || tokens[0] == "disband")
    {
        if (tokens.size() != 2)
            return out;
        std::string const& digits = tokens[1];
        if (digits.empty())
            return out;
        for (char c : digits)
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return out;
        uint64_t value = 0;
        for (char c : digits)
        {
            value = value * 10 + static_cast<uint64_t>(c - '0');
            if (value > 4294967295ULL)
                return out;
        }
        if (value == 0)
            return out;
        out.verb = tokens[0] == "fill" ? Verb::Fill : Verb::Disband;
        out.id = static_cast<uint32_t>(value);
        return out;
    }
    return out;
}

} // namespace TortoiseBots::BotLfgLifecycle
