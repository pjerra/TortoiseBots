#pragma once

// Free-bot guild participation decisions: what a free bot does with a guild invite, when it leaves a
// guild that is too large, when it may buy, sign or turn in a charter, and which owner whisper is a guild
// management command. Core-free: every fact is passed in, so forever/tools/test_guild_join_pure.cpp proves the
// decisions without the server.
//
// A guild at 1000 members is the point where a free bot refuses an invitation and walks out of a guild it is in,
// so the pool can never fill a player's guild to the client's hard cap. A charter belongs to a bot above level 10,
// the level where the charter price starts being reserved.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tbhost
{
namespace guildjoin
{

inline constexpr std::uint32_t GuildMemberLimit = 1000;
inline constexpr std::uint32_t CharterMinLevel = 10;

// An outstanding invite may be accepted when the bot has no guild, the guild is not too large and the two are on
// the same side (or the core allows two-side guilds).
inline bool InviteAcceptable(bool inGuild, bool invited, bool sameTeam, bool twoSideGuilds, std::uint32_t memberCount)
{
    if (inGuild || !invited)
        return false;
    if (!sameTeam && !twoSideGuilds)
        return false;
    return memberCount < GuildMemberLimit;
}

// A free bot leaves a guild that reached the limit, so the bot rank never takes the last seats a player could use.
inline bool ShouldLeaveOversized(std::uint32_t memberCount)
{
    return memberCount >= GuildMemberLimit;
}

inline bool FoundingLevelEligible(std::uint32_t level)
{
    return level > CharterMinLevel;
}

// The buy leg: unguilded, no invite, no charter yet, high enough and able to pay the core's charter price.
inline bool FoundingEligible(bool inGuild, bool invited, bool hasCharter, std::uint32_t level,
    std::uint64_t money, std::uint32_t charterCost)
{
    if (inGuild || invited || hasCharter)
        return false;
    return FoundingLevelEligible(level) && money >= charterCost;
}

// The turn-in leg: every signature the core asks for is on the charter.
inline bool CharterComplete(std::size_t signs, std::uint32_t required)
{
    return required != 0 && signs >= required;
}

enum class Command
{
    None,
    Invite,
    Join,
    Leave,
    Promote,
    Demote,
    Remove,
    Leader,
};

struct Whisper
{
    Command command = Command::None;
    std::string argument;
};

namespace detail
{
inline bool EqualsIgnoreCase(std::string_view a, char const* b)
{
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
    {
        char const x = a[i];
        char const y = b[i];
        char const xl = (x >= 'A' && x <= 'Z') ? char(x - 'A' + 'a') : x;
        char const yl = (y >= 'A' && y <= 'Z') ? char(y - 'A' + 'a') : y;
        if (xl != yl)
            return false;
    }
    return i == a.size() && b[i] == '\0';
}
} // namespace detail

// "guild <verb> [name]": the owner's guild management whisper words.
// Anything else answers None, so the line still reaches the engine's own chat commands.
inline Whisper ParseGuildWhisper(std::string_view text)
{
    Whisper out;

    std::size_t pos = 0;
    auto nextWord = [&](std::string_view& word) -> bool
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
        if (pos >= text.size())
            return false;
        std::size_t const start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t')
            ++pos;
        word = text.substr(start, pos - start);
        return true;
    };

    std::string_view first;
    std::string_view verb;
    if (!nextWord(first) || !detail::EqualsIgnoreCase(first, "guild"))
        return out;
    if (!nextWord(verb))
        return out;

    if (detail::EqualsIgnoreCase(verb, "invite"))
        out.command = Command::Invite;
    else if (detail::EqualsIgnoreCase(verb, "join"))
        out.command = Command::Join;
    else if (detail::EqualsIgnoreCase(verb, "leave"))
        out.command = Command::Leave;
    else if (detail::EqualsIgnoreCase(verb, "promote"))
        out.command = Command::Promote;
    else if (detail::EqualsIgnoreCase(verb, "demote"))
        out.command = Command::Demote;
    else if (detail::EqualsIgnoreCase(verb, "remove"))
        out.command = Command::Remove;
    else if (detail::EqualsIgnoreCase(verb, "leader"))
        out.command = Command::Leader;
    else
        return out;

    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
    if (pos < text.size())
    {
        std::size_t end = text.size();
        while (end > pos && (text[end - 1] == ' ' || text[end - 1] == '\t'))
            --end;
        out.argument.assign(text.substr(pos, end - pos));
    }
    return out;
}

// Only the bot's owner (its host master) or a game master may manage its guild; other players cannot.
inline bool WhisperAuthorized(bool owner, bool gm)
{
    return owner || gm;
}

} // namespace guildjoin
} // namespace tbhost
