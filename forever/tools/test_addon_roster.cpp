// Standalone test of the addon roster rule (forever/src/host/TbAddonRosterPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_addon_roster.cpp -o /tmp/test_addon_roster && /tmp/test_addon_roster
#include "host/TbAddonRosterPure.h"

#include <cstdio>
#include <string>

namespace roster = TortoiseBots::roster;
namespace player = TortoiseBots::player;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
player::Caller Owner()
{
    player::Caller caller;
    caller.gm = false;
    caller.account = 42;
    caller.guid = 100;
    return caller;
}

player::BotFacts OwnBot()
{
    player::BotFacts bot;
    bot.isBot = true;
    bot.account = 42;
    bot.master = 100;
    return bot;
}
}

int main()
{
    // THE FAILING CASE the piece exists for: a roster row encodes TB's exact
    // field order, and the trailer order is VERSION, PROTO, LATEST, VERBS,
    // CAPS, TRANSPORT.
    // case: roster_row_field_order
    {
        roster::RosterRow row;
        row.guidLow = 7;
        row.name = "Pip";
        row.classId = 9;
        row.state = "online";
        row.grouped = true;
        row.location = "Stormwind";
        row.className = "Warlock";
        CHECK(roster::RosterLine(row) == "TBM:ROSTER|7|Pip|9|online|1|Stormwind|Warlock");
        CHECK(roster::RosterBegin(2) == "TBM:ROSTER_BEGIN|2");
        CHECK(roster::ProtoLine() == "TBM:PROTO|1|1");
        CHECK(roster::LatestLine() == "TBM:LATEST|-|-");
    }

    // Wire fields are escaped like TB's ProtocolSafe: '|' CR LF become blanks,
    // empty becomes "-".
    // case: field_escaping
    {
        roster::RosterRow row;
        row.guidLow = 7;
        row.name = "a|b";
        row.classId = 1;
        row.state = "on\nline";
        row.grouped = false;
        row.location = "";
        row.className = "x\ry";
        CHECK(roster::RosterLine(row) == "TBM:ROSTER|7|a b|1|on line|0|-|x y");
    }

    // ALTBOTS joins the away names; ALTSUM carries one sentence per away alt
    // and END counts them, so the addon commits only a matching snapshot.
    // case: altsum_stream
    {
        CHECK(roster::AltBotsLine({}) == "TBM:ALTBOTS|");
        CHECK(roster::AltBotsLine({ "Pip", "Bob" }) == "TBM:ALTBOTS|Pip,Bob");
        roster::AltSumLine line{ "Pip", "Pip is away: grinding" };
        CHECK(roster::AltSumLineText(line) == "TBM:ALTSUM|Pip|Pip is away: grinding");
        CHECK(roster::AltSumEnd(1) == "TBM:ALTSUM_END|1");
    }

    // CLAIMED keeps TB's eleven-field order; BOTSTATE is name + move= fields.
    // case: claimed_botstate_shapes
    {
        roster::ClaimedRow row;
        row.guidLow = 9;
        row.name = "Pip";
        row.classId = 9;
        row.level = 60;
        row.state = "online";
        row.grouped = true;
        row.location = "Orgrimmar";
        row.gearLocked = 1;
        row.profs = "-";
        row.isAlt = 0;
        row.className = "Warlock";
        CHECK(roster::ClaimedLine(row) ==
            "TBM:CLAIMED|9|Pip|9|60|online|1|Orgrimmar|1|-|0|Warlock");
        CHECK(roster::ClaimedBegin(0) == "TBM:CLAIMED_BEGIN|0");
        CHECK(roster::BotStateBegin(1) == "TBM:BOTSTATE_BEGIN|1");
        CHECK(roster::BotStateText({ "Pip", "move=follow" }) == "TBM:BOTSTATE|Pip|move=follow");
    }

    // TRANSPORT verdict follows TB's AddonCommandChannel: party only for a
    // grouped requester while the channel switch is on and the group is no BG.
    // case: transport_verdict
    {
        using roster::TransportVerdict;
        CHECK(TransportVerdict(true, true, false) == "party");
        CHECK(TransportVerdict(false, true, false) == "none");
        CHECK(TransportVerdict(true, false, false) == "none");
        CHECK(TransportVerdict(true, true, true) == "none");
        CHECK(roster::TransportLine("none") == "TBM:TRANSPORT|none");
    }

    // VERBS/CAPS join the token lists; the base caps earn only the live
    // claimed-roster stream (flee waits for the action router, the rest for
    // their verbs). "tb-gm" rides along only for a .tb-permitted requester.
    // case: verbs_caps_join
    {
        CHECK(roster::VerbsLine({ "alts", "list", "roster" }) == "TBM:VERBS|alts,list,roster");
        CHECK(roster::CapsLine(roster::CapsFor(false)) == "TBM:CAPS|claimed-roster");
        CHECK(roster::CapsLine(roster::CapsFor(true)) == "TBM:CAPS|claimed-roster,tb-gm");
        CHECK(roster::JoinTokens({}) == "");
    }

    // The move= word stays inside the vocabulary TB's clients render.
    // case: bot_move_word
    {
        CHECK(roster::BotMoveWord(false, "follow") == "follow");
        CHECK(roster::BotMoveWord(false, "attack") == "follow");
        CHECK(roster::BotMoveWord(false, "stay") == "stay");
        CHECK(roster::BotMoveWord(false, "flee") == "flee");
        CHECK(roster::BotMoveWord(true, "stay") == "free");
        CHECK(roster::RosterStateOf(true) == "online");
        CHECK(roster::RosterStateOf(false) == "offline");
        CHECK(roster::VersionLine("abc") == "TBM:VERSION|abc");
    }

    // The roster shows exactly what `.bot who` shows: the owner's bots, the
    // caller's mastered/commanded/led bots, never the GM's realm-wide pool.
    // case: roster_visible_matches_who
    {
        player::Caller owner = Owner();
        CHECK(roster::RosterVisible(owner, OwnBot()));
        player::BotFacts mastered;
        mastered.isBot = true;
        mastered.account = 99;
        mastered.master = 100;
        CHECK(roster::RosterVisible(owner, mastered));
        player::BotFacts commanded = mastered;
        commanded.master = 0;
        commanded.commandedByCaller = true;
        CHECK(roster::RosterVisible(owner, commanded));
        player::BotFacts led = mastered;
        led.master = 0;
        led.groupLeader = 100;
        led.leaderIsBot = false;
        CHECK(roster::RosterVisible(owner, led));
        player::BotFacts botLed = led;
        botLed.leaderIsBot = true;
        CHECK(!roster::RosterVisible(owner, botLed));
        player::BotFacts stranger = mastered;
        stranger.master = 0;
        CHECK(!roster::RosterVisible(owner, stranger));
        player::BotFacts notBot;
        CHECK(!roster::RosterVisible(owner, notBot));
        // A GM sees what a player sees: no `all` widening over the addon.
        player::Caller gm = owner;
        gm.gm = true;
        CHECK(roster::RosterVisible(gm, OwnBot()));
        CHECK(!roster::RosterVisible(gm, stranger));
        // A zero account never matches, even against zero.
        player::Caller nobody;
        player::BotFacts unknown = mastered;
        unknown.account = 0;
        unknown.master = 0;
        CHECK(!roster::RosterVisible(nobody, unknown));
    }

    if (g_fail == 0)
        std::printf("OK addon roster pure: %d checks\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
