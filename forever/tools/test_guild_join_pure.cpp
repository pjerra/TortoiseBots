// Pure tests for the free-bot guild participation decisions (TbHostGuildJoinPure.h).
// Build & run:
//   g++ -std=c++17 -I. -Iforever/src forever/tools/test_guild_join_pure.cpp -o /tmp/test_guild_join_pure && /tmp/test_guild_join_pure
#include "host/TbHostGuildJoinPure.h"

#include <cassert>

using namespace tbhost::guildjoin;

int main()
{
    // An invite is accepted only when the bot is unguilded, invited, on the guild's side and the guild has room.
    assert(InviteAcceptable(false, true, true, false, 0));
    assert(InviteAcceptable(false, true, true, false, GuildMemberLimit - 1));
    assert(!InviteAcceptable(false, true, true, false, GuildMemberLimit));
    assert(!InviteAcceptable(false, true, true, false, GuildMemberLimit + 1));
    assert(!InviteAcceptable(true, true, true, false, 1));   // already in a guild
    assert(!InviteAcceptable(false, false, true, false, 1)); // nothing standing
    assert(!InviteAcceptable(false, true, false, false, 1)); // other side, two-side guilds off
    assert(InviteAcceptable(false, true, false, true, 1));   // other side, two-side guilds on

    // The stay rule: a guild at the limit loses its free bots.
    assert(!ShouldLeaveOversized(GuildMemberLimit - 1));
    assert(ShouldLeaveOversized(GuildMemberLimit));
    assert(ShouldLeaveOversized(GuildMemberLimit + 1));

    // Founding: above level 10, no guild, no invite, no charter yet, enough gold for the core's price.
    assert(FoundingLevelEligible(11));
    assert(!FoundingLevelEligible(10));
    assert(FoundingEligible(false, false, false, 11, 1000, 1000));
    assert(!FoundingEligible(false, false, false, 11, 999, 1000));
    assert(!FoundingEligible(false, false, false, 10, 5000, 1000));
    assert(!FoundingEligible(true, false, false, 40, 5000, 1000));
    assert(!FoundingEligible(false, true, false, 40, 5000, 1000));
    assert(!FoundingEligible(false, false, true, 40, 5000, 1000));

    // Turn-in only when the core's required signatures are on the charter.
    assert(CharterComplete(4, 4));
    assert(CharterComplete(5, 4));
    assert(!CharterComplete(3, 4));
    assert(!CharterComplete(0, 0)); // a zero requirement never founds by accident

    // Owner whispers: guild <verb> [name], case-insensitive.
    {
        Whisper w = ParseGuildWhisper("guild invite Bob");
        assert(w.command == Command::Invite);
        assert(w.argument == "Bob");

        w = ParseGuildWhisper("  GUILD   Join ");
        assert(w.command == Command::Join);
        assert(w.argument.empty());

        w = ParseGuildWhisper("guild leave");
        assert(w.command == Command::Leave);

        w = ParseGuildWhisper("guild promote Jane Doe");
        assert(w.command == Command::Promote);
        assert(w.argument == "Jane Doe");

        w = ParseGuildWhisper("guild demote Bob");
        assert(w.command == Command::Demote);
        assert(w.argument == "Bob");

        w = ParseGuildWhisper("guild remove Bob");
        assert(w.command == Command::Remove);

        w = ParseGuildWhisper("guild leader Bob");
        assert(w.command == Command::Leader);
        assert(w.argument == "Bob");

        // Missing the name is still the command; the handler refuses it with a reason.
        w = ParseGuildWhisper("guild invite");
        assert(w.command == Command::Invite);
        assert(w.argument.empty());

        // Not a guild command: the engine's own chat words keep working.
        assert(ParseGuildWhisper("follow").command == Command::None);
        assert(ParseGuildWhisper("guild").command == Command::None);
        assert(ParseGuildWhisper("guild somethingelse Bob").command == Command::None);
        assert(ParseGuildWhisper("my guild invite Bob").command == Command::None);
        assert(ParseGuildWhisper("").command == Command::None);
    }

    // Only the owner or a game master may manage the bot's guild.
    assert(WhisperAuthorized(true, false));
    assert(WhisperAuthorized(false, true));
    assert(WhisperAuthorized(true, true));
    assert(!WhisperAuthorized(false, false));

    return 0;
}
