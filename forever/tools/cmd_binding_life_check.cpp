// Standalone test of the PURE half of the commander binding lifecycle (forever/src/host/cmd/TbCmdBindingLifePure.h; package P2,
// forever/docs/CMD-P2-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/cmd_binding_life_check.cpp -o /tmp/cmd_binding_life_check && /tmp/cmd_binding_life_check
// It runs next to the shared release rule (runtime/RemoteCommanderPolicy.h), so the verdicts it checks are the ones the Turtle code uses.
#include "forever/src/host/cmd/TbCmdBindingLifePure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace L = TortoiseBots::cmd::life;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// fixture guids (raw character guids; the card code uses counters, these are the stored values)
constexpr uint64_t Tank = 0x000000000000000Aull, Heal = 0x000000000000000Bull, Dps = 0x000000000000000Cull;
constexpr uint64_t Cmd = 0x0000000000000014ull, LeadOther = 0x0000000000000015ull;

L::Stored Party()
{
    L::Stored stored;
    stored.commander = Cmd;
    stored.lead = Tank;
    stored.members = { Tank, Heal, Dps };
    stored.flags = L::FlagsOf(false, false, false, false);
    stored.boundAt = 1700000000;
    return stored;
}

L::Facts Healthy()
{
    L::Facts facts;
    facts.memberCount = 3;
    return facts;
}

void TestRow()
{
    L::Stored const party = Party();
    // A row is the five fields of Stored and nothing else: the field count is checked by the store's SQL column list (wiring pin), and the model
    // itself has no group member (D2 - the spec's failing case is a stored database id reused by another group).
    CHECK(L::Valid(party));
    // The lead must be one of the members, nobody twice, and no zero guid.
    {
        L::Stored row = party;
        row.members = { Heal, Dps };
        CHECK(!L::Valid(row));
        row = party;
        row.members = { Tank, Tank, Heal };
        CHECK(!L::Valid(row));
        row = party;
        row.members = { Tank, 0, Heal };
        CHECK(!L::Valid(row));
        row = party;
        row.lead = 0;
        CHECK(!L::Valid(row));
        row = party;
        row.commander = Tank;     // the lead cannot command itself
        CHECK(!L::Valid(row));
        row = party;
        row.flags = 16;           // a flag this build does not know
        CHECK(!L::Valid(row));
        row = party;
        row.members = { Tank, Cmd, Heal };    // the commander cannot be one of its own members (it would join its own party)
        CHECK(!L::Valid(row));
    }
    // Members are written and read back in order, without zeroes or repeats.
    CHECK(L::EncodeMembers({ Tank, Heal, Dps }) == "10,11,12");
    CHECK((L::DecodeMembers("12,11,11,0,10") == std::vector<uint64_t>{ Dps, Heal, Tank }));
    CHECK(L::DecodeMembers("").empty());
    // A malformed word is skipped, never guessed.
    CHECK((L::DecodeMembers("x,12") == std::vector<uint64_t>{ Dps }));
    // A value past 64 bits is a bad word: it must never wrap into a small guid (18446744073709551617 would wrap to 1).
    CHECK(L::DecodeMembers("18446744073709551617").empty());
    CHECK(L::DecodeMembers("99999999999999999999,12").size() == 1);
    CHECK((L::DecodeMembers("99999999999999999999,12") == std::vector<uint64_t>{ Dps }));
    // Flags round-trip.
    CHECK(L::FlagsOf(true, false, false, false) == L::FlagInParty);
    CHECK((L::FlagsOf(true, false, false, true) == (L::FlagInParty | L::FlagSystem)));
    L::Stored inParty = party;
    inParty.flags = L::FlagsOf(true, false, false, false);
    CHECK(L::HasFlag(inParty, L::FlagInParty) && !L::HasFlag(inParty, L::FlagRaid));
    // A group re-created after a restart changes nothing in the row: no group field exists to go stale.
    L::Stored recreated = party;
    CHECK(L::Valid(recreated) && recreated.flags == party.flags && recreated.members == party.members);
}

void TestReleaseReasons()
{
    L::Stored const party = Party();
    // The healthy binding stands.
    CHECK(L::Decide(party, Healthy()) == L::End::None);
    // The lead left the world: released at once (the spec's first release).
    {
        L::Facts facts = Healthy();
        facts.leadInWorld = false;
        CHECK(L::Decide(party, facts) == L::End::LeadLeft);
    }
    // The lead dead: held for the grace, released past it.
    {
        L::Facts facts = Healthy();
        facts.leadAlive = false;
        facts.leadDeadGraceMs = 180000;
        facts.leadDeadMs = 179999;
        CHECK(L::Decide(party, facts) == L::End::None);
        facts.leadDeadMs = 180000;
        CHECK(L::Decide(party, facts) == L::End::LeadDead);
        facts.leadDeadMs = 900000;
        CHECK(L::Decide(party, facts) == L::End::LeadDead);
    }
    // The commander logged out: held for the grace (a quick re-login keeps the binding, and the bots wait for their master), released past it.
    {
        L::Facts facts = Healthy();
        facts.commanderOnline = false;
        facts.commanderGraceMs = 300000;
        facts.commanderGoneMs = 20000;
        CHECK(L::Decide(party, facts) == L::End::None);
        facts.commanderGoneMs = 300000;
        CHECK(L::Decide(party, facts) == L::End::CommanderAway);
    }
    // The group: gone, or a new one that lost the bound followers (a re-created group WITH a follower keeps the binding: spec section 4, the counter
    // is refreshed, not compared).
    {
        L::Facts facts = Healthy();
        facts.leadGroup = false;
        CHECK(L::Decide(party, facts) == L::End::GroupChanged);
        facts = Healthy();
        facts.leadGroupHoldsFollower = false;
        CHECK(L::Decide(party, facts) == L::End::GroupChanged);
        facts = Healthy();
        facts.leadGroupHoldsFollower = true;      // a new counter with the lead and a follower stays bound
        CHECK(L::Decide(party, facts) == L::End::None);
    }
    {
        L::Facts facts = Healthy();
        facts.leadIsLeader = false;
        CHECK(L::Decide(party, facts) == L::End::LeaderChanged);
        facts = Healthy();
        facts.groupIsRaidOrBg = true;
        CHECK(L::Decide(party, facts) == L::End::GroupRaid);
        facts = Healthy();
        facts.realPlayerInGroup = true;
        CHECK(L::Decide(party, facts) == L::End::PlayerJoined);
        facts = Healthy();
        facts.memberCount = 6;
        CHECK(L::Decide(party, facts) == L::End::GroupTooLarge);
        facts = Healthy();
        facts.enabled = false;
        CHECK(L::Decide(party, facts) == L::End::Disabled);
    }
    // An in-party binding stands with the commander inside the party and ends when he is gone from it; a real player joining is fine there.
    {
        L::Stored inParty = party;
        inParty.flags = L::FlagsOf(true, false, false, false);
        L::Facts facts = Healthy();
        facts.realPlayerInGroup = true;
        facts.commanderInGroup = true;
        CHECK(L::Decide(inParty, facts) == L::End::None);
        facts.commanderInGroup = false;
        CHECK(L::Decide(inParty, facts) == L::End::CommanderLeftParty);
    }
    // A raid binding: a raid group stands, a battleground group still ends it; a classic binding ends on any raid.
    {
        L::Stored raid = party;
        raid.flags = L::FlagsOf(false, true, false, false);
        L::Facts facts = Healthy();
        facts.groupIsRaidOrBg = true;
        CHECK(L::Decide(raid, facts) == L::End::None);
        facts.groupIsBg = true;
        CHECK(L::Decide(raid, facts) == L::End::GroupRaid);
        facts = Healthy();
        facts.groupIsRaidOrBg = true;
        facts.memberCount = 10;
        CHECK(L::Decide(party, facts) == L::End::GroupRaid);
        facts = Healthy();
        facts.memberCount = 6;
        facts.groupIsRaidOrBg = true;
        CHECK(L::Decide(raid, facts) == L::End::None);      // ten members fit a raid binding
        facts.memberCount = 41;
        CHECK(L::Decide(raid, facts) == L::End::GroupTooLarge);
    }
    // Leadership: the lead bot leading stands, and so does the binding's own commander leading his party (the live test's failing case: an in-party
    // commander who binds his own party is its leader). Another character leading - a third real player, or a bot that is not this lead - ends it.
    {
        L::Stored inParty = party;
        inParty.flags = L::FlagsOf(true, false, false, false);
        L::Facts facts = Healthy();
        facts.commanderInGroup = true;
        facts.leadIsLeader = false;
        facts.leaderGuid = Cmd;
        CHECK(L::Decide(inParty, facts) == L::End::None);            // the commander leads: kept
        facts.leadIsLeader = true;                                   // the lead bot itself leads
        facts.leaderGuid = Tank;
        CHECK(L::Decide(inParty, facts) == L::End::None);            // the lead bot leads: kept
        facts.leadIsLeader = false;
        facts.leaderGuid = LeadOther;
        CHECK(L::Decide(inParty, facts) == L::End::LeaderChanged);   // a third character leads: released
        // The stored player-led flag never excuses a foreign leader: only the commander's own lead counts.
        L::Stored playerLed = inParty;
        playerLed.flags = L::FlagsOf(true, false, true, false);
        CHECK(L::Decide(playerLed, facts) == L::End::LeaderChanged);
        facts.leaderGuid = Cmd;
        CHECK(L::Decide(playerLed, facts) == L::End::None);
        // A commander outside the party: unchanged, any leader but the lead bot still ends a classic binding - and that includes the commander
        // himself joining the party and taking the lead (the name was no permission to lead a binding he made from outside).
        facts = Healthy();
        facts.leadIsLeader = false;
        facts.leaderGuid = LeadOther;
        CHECK(L::Decide(party, facts) == L::End::LeaderChanged);
        facts = Healthy();
        facts.leadIsLeader = false;
        facts.leaderGuid = Cmd;
        CHECK(L::Decide(party, facts) == L::End::LeaderChanged);
        // A system binding ignores a real player joining.
        L::Stored system = party;
        system.flags = L::FlagsOf(false, false, false, true);
        facts = Healthy();
        facts.realPlayerInGroup = true;
        CHECK(L::Decide(system, facts) == L::End::None);
    }
    // The most specific verdict wins: a lost group is reported as the group, not as the commander's absence.
    {
        L::Facts facts = Healthy();
        facts.leadGroup = false;
        facts.commanderOnline = false;
        facts.commanderGoneMs = 900000;
        facts.commanderGraceMs = 300000;
        CHECK(L::Decide(party, facts) == L::End::GroupChanged);
    }
    // The shared policy reason the in-party rule reads.
    CHECK(L::PolicyReason(L::End::LeadDead) == TortoiseBots::RemoteCommander::ReleaseReason::LeadLeft);
    CHECK(L::PolicyReason(L::End::CommanderAway) == TortoiseBots::RemoteCommander::ReleaseReason::CommanderLeft);
    CHECK(L::PolicyReason(L::End::CommanderLeftParty) == TortoiseBots::RemoteCommander::ReleaseReason::CommanderLeftParty);
    CHECK(L::PolicyReason(L::End::GroupChanged) == TortoiseBots::RemoteCommander::ReleaseReason::Unbound);
    CHECK(std::string(L::EndText(L::End::LeadDead)).find("dead") != std::string::npos);
    CHECK(std::string(L::EndCode(L::End::CommanderAway)) == "commander-away");
}

void TestBgBind()
{
    // A battleground bind (BG2 live row 5): `.tb master <bot> <human>` made inside one match instance. It is its own kind - never stored, and the
    // normal release reasons do not apply - and it has exactly one end, the match it was made in. The live failure this replaces: the 1 Hz tick
    // read the battleground raid as the binding's group and released it within a second (group-raid, and usually leader-changed first).
    L::Stored const party = Party();
    // A bg bind is no member of the record, so the normal verdicts never reach it: it is judged by CheckBg on its own facts only. Whatever the
    // battleground raid looks like (the bot does not lead it, a real player is in it, it is a raid of ten), the bind stands while both are inside.
    {
        L::BgBook book;
        L::BindResult r;
        r.bot = Dps;
        r.bound = true;
        r.kind = L::Kind::Bg;
        CHECK(L::ApplyBind(book, Cmd, { r }, 41));
        L::BgLive live;
        live.bot = Dps;
        live.botOnline = true;
        live.botInstance = 41;
        live.botCommander = Cmd;
        live.commanderOnline = true;
        live.commanderInstance = 41;
        CHECK(L::CheckBg(book, { live }, true).empty());
        L::BgLive dead = live;   // a dead bot in a match is still in it (a revive wave): only the instance and the login count
        CHECK(L::CheckBg(book, { dead }, true).empty());
        live.botInstance = 0;
        CHECK(std::string(L::EndCode(L::CheckBg(book, { live }, true).at(0).end)) == "bg-bot-left");
        CHECK(std::string(L::EndText(L::End::BgBotLeft)).find("bot left") != std::string::npos);
        live.botInstance = 41;
        live.commanderInstance = 0;
        CHECK(std::string(L::EndCode(L::CheckBg(book, { live }, true).at(0).end)) == "bg-commander-left");
        live.botInstance = 0;
        CHECK(std::string(L::EndCode(L::CheckBg(book, { live }, true).at(0).end)) == "bg-match-ended");
        live.botInstance = 41;
        live.commanderInstance = 41;
        live.botOnline = false;
        live.commanderOnline = false;
        CHECK(L::CheckBg(book, { live }, true).at(0).end == L::End::BgBotLeft);      // the bot's own departure first
        // #321 follow-up: a mid-match transfer puts them on different teams. The order gate refuses every command then, and the bind must be
        // released, not left standing to the end of the match.
        live.botOnline = true;
        live.commanderOnline = true;
        live.sameTeam = false;
        auto const transferred = L::CheckBg(book, { live }, true);
        CHECK(transferred.size() == 1 && transferred[0].release && transferred[0].end == L::End::BgTeamChanged);
        CHECK(std::string(L::EndCode(L::End::BgTeamChanged)) == "bg-team-changed");
        CHECK(std::string(L::EndText(L::End::BgTeamChanged)).find("different teams") != std::string::npos);
        live.sameTeam = true;
        CHECK(L::CheckBg(book, { live }, true).empty());     // one team again: the bind stands
    }
    // The direct rule, without a stored row: the bind survives only while both are inside the very instance it was made in.
    {
        L::BgFacts facts;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::MatchEnded);
        facts.instance = 41;
        facts.botInstance = 41;
        facts.commanderInstance = 41;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::None);
        facts.commanderInstance = 42;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::CommanderLeft);
        facts.commanderInstance = 41;
        facts.botInstance = 42;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::BotLeft);
        facts.botInstance = 42;
        facts.commanderInstance = 42;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::MatchEnded);       // both somewhere else: this match is gone
        facts.botInstance = 41;
        facts.commanderInstance = 41;
        facts.sameTeam = false;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::TeamChanged);      // same instance, opposite sides
        facts.botOnline = false;
        CHECK(L::BgBindEnd(facts) == L::BgEnd::BotLeft);          // leaving the match still reads first
    }
    // Outside the kind nothing changes: a normal binding in a battleground group is released as before (the live row's failure, kept as a test).
    {
        L::Facts facts = Healthy();
        facts.groupIsRaidOrBg = true;
        facts.groupIsBg = true;
        CHECK(L::Decide(party, facts) == L::End::GroupRaid);
        facts.groupIsRaidOrBg = false;
        facts.groupIsBg = false;
        facts.leadIsLeader = false;
        CHECK(L::Decide(party, facts) == L::End::LeaderChanged);
    }
    // The battleground end texts exist and never read as a normal release; the codes are distinct.
    CHECK(std::string(L::EndText(L::End::BgCommanderLeft)).find("commander left the battleground") != std::string::npos);
    CHECK(std::string(L::EndText(L::End::BgMatchEnded)).find("has ended") != std::string::npos);
    CHECK(std::string(L::EndCode(L::End::BgBotLeft)) != std::string(L::EndCode(L::End::BgCommanderLeft)));
    CHECK(std::string(L::EndCode(L::End::BgMatchEnded)) != std::string(L::EndCode(L::End::BgCommanderLeft)));
}

// ---- the kind is per bound bot (BG2 review F2/F3) ----

L::BindResult Bound(uint64_t bot, L::Kind kind) { L::BindResult r; r.bot = bot; r.bound = true; r.kind = kind; return r; }
L::BindResult Refused(uint64_t bot, L::Kind kind) { L::BindResult r; r.bot = bot; r.bound = false; r.kind = kind; return r; }
L::BgLive Live(uint64_t bot, uint32_t botInstance, uint32_t commanderInstance, uint64_t botCommander = Cmd)
{
    L::BgLive live;
    live.bot = bot;
    live.botOnline = true;
    live.botInstance = botInstance;
    live.botCommander = botCommander;
    live.commanderOnline = true;
    live.commanderInstance = commanderInstance;
    return live;
}
constexpr uint64_t BgBot = 0x30ull, BgBot2 = 0x31ull, WorldBot = 0x32ull;

bool Same(std::vector<uint64_t> const& a, std::vector<uint64_t> const& b) { return a == b; }

void TestKindPerBot()
{
    // (a) A commander with a standing world binding (Party: Tank/Heal/Dps outside any match) binds a pool bot inside a battleground. The new bot is
    // bg; the world binding keeps its members (so its stored row), its kind and its normal rules; the bg bind survives the 1 Hz check.
    {
        L::Stored const party = Party();
        L::BgBook book;
        std::vector<L::BindResult> const results = { Bound(BgBot, L::Kind::Bg) };
        CHECK(L::ApplyBind(book, Cmd, results, 41));
        CHECK(L::NormalRecordUpdate(results, party.members) == L::NormalUpdate::Keep);
        CHECK(Same(L::NormalMembers(book, Cmd, { Tank, Heal, Dps, BgBot }), party.members));
        CHECK(Same(L::BgMembersOf(book, Cmd), { BgBot }));
        // The instance is the handler's (the commander's match), never a bot of the commander's whole list.
        CHECK(L::FindBg(book, BgBot) && L::FindBg(book, BgBot)->instance == 41 && L::FindBg(book, BgBot)->commander == Cmd);
        CHECK(!L::FindBg(book, Tank));
        CHECK(L::Valid(party));
        CHECK(L::Decide(party, Healthy()) == L::End::None);
        CHECK(L::CheckBg(book, { Live(BgBot, 41, 41) }, true).empty());
        // The world binding's normal rules are untouched by the bg bind: a world lead leaving still ends it.
        L::Facts gone = Healthy();
        gone.leadInWorld = false;
        CHECK(L::Decide(party, gone) == L::End::LeadLeft);
    }
    // (b) A standing bg bind, then a normal bind of a world bot: the bg bind stays bg (not in the normal record, never stored).
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg) }, 41));
        std::vector<L::BindResult> const later = { Bound(WorldBot, L::Kind::Normal) };
        CHECK(L::ApplyBind(book, Cmd, later, 41));     // the commander's instance does not make a normal bot bg
        CHECK(L::FindBg(book, BgBot) && L::FindBg(book, BgBot)->instance == 41);
        CHECK(!L::FindBg(book, WorldBot));
        CHECK(L::NormalRecordUpdate(later, {}) == L::NormalUpdate::Rebind);
        CHECK(Same(L::NormalMembers(book, Cmd, { BgBot, WorldBot }), { WorldBot }));
        CHECK(L::CheckBg(book, { Live(BgBot, 41, 41) }, true).empty());
    }
    // (c) A bind command in which every bot was refused changes nothing: not the book, not the normal record.
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg) }, 41));
        L::BgBook const before = book;
        std::vector<L::BindResult> const refused = { Refused(BgBot, L::Kind::Normal), Refused(WorldBot, L::Kind::Bg), Refused(Tank, L::Kind::Normal) };
        CHECK(!L::ApplyBind(book, Cmd, refused, 0));
        CHECK(book.size() == before.size() && book[0].bot == BgBot && book[0].instance == 41 && book[0].commander == Cmd);
        CHECK(L::NormalRecordUpdate(refused, Party().members) == L::NormalUpdate::Keep);
        CHECK(L::NormalRecordUpdate({}, Party().members) == L::NormalUpdate::Keep);
    }
    // (d) Two bg-bound bots; one leaves the match (or logs out): only that one is released.
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg), Bound(BgBot2, L::Kind::Bg) }, 41));
        auto const left = L::CheckBg(book, { Live(BgBot, 41, 41), Live(BgBot2, 0, 41) }, true);
        CHECK(left.size() == 1 && left[0].bot == BgBot2 && left[0].commander == Cmd && left[0].release && left[0].end == L::End::BgBotLeft);
        L::BgLive out = Live(BgBot2, 41, 41);
        out.botOnline = false;
        auto const logout = L::CheckBg(book, { Live(BgBot, 41, 41), out }, true);
        CHECK(logout.size() == 1 && logout[0].bot == BgBot2 && logout[0].end == L::End::BgBotLeft);
        // A member with no live row is gone too (never kept on a missing fact).
        auto const missing = L::CheckBg(book, { Live(BgBot, 41, 41) }, true);
        CHECK(missing.size() == 1 && missing[0].bot == BgBot2 && missing[0].end == L::End::BgBotLeft);
        // After its release only the other stays.
        L::DropBg(book, BgBot2);
        CHECK(Same(L::BgMembersOf(book, Cmd), { BgBot }));
    }
    // (e) The commander leaves the match (or logs out): all his bg members are released, his normal members are untouched.
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg), Bound(BgBot2, L::Kind::Bg) }, 41));
        auto const left = L::CheckBg(book, { Live(BgBot, 41, 0), Live(BgBot2, 41, 0) }, true);
        CHECK(left.size() == 2 && left[0].end == L::End::BgCommanderLeft && left[1].end == L::End::BgCommanderLeft && left[0].release && left[1].release);
        L::BgLive a = Live(BgBot, 41, 41), b = Live(BgBot2, 41, 41);
        a.commanderOnline = b.commanderOnline = false;
        auto const logout = L::CheckBg(book, { a, b }, true);
        CHECK(logout.size() == 2 && logout[0].end == L::End::BgCommanderLeft && logout[1].end == L::End::BgCommanderLeft);
        CHECK(Same(L::NormalMembers(book, Cmd, { Tank, Heal, Dps, BgBot, BgBot2 }), Party().members));
        CHECK(L::Decide(Party(), Healthy()) == L::End::None);
        // Another commander's bg member in the same book is not his.
        L::BgBook mixed = book;
        CHECK(L::ApplyBind(mixed, LeadOther, { Bound(WorldBot, L::Kind::Bg) }, 41));
        auto const mine = L::CheckBg(mixed, { Live(BgBot, 41, 0), Live(BgBot2, 41, 0), Live(WorldBot, 41, 41, LeadOther) }, true);
        CHECK(mine.size() == 2 && mine[0].bot != WorldBot && mine[1].bot != WorldBot);
    }
    // (f) The release paths never touch the raid leader: a bg release reaches no party at all; a normal one only through its own in-party rule.
    {
        CHECK(!L::ReleaseReachesParty(L::Kind::Bg, L::FlagInParty | L::FlagPlayerLed | L::FlagRaid));
        CHECK(!L::ReleaseReachesParty(L::Kind::Bg, 0));
        CHECK(L::ReleaseReachesParty(L::Kind::Normal, L::FlagInParty));
        CHECK(!L::ReleaseReachesParty(L::Kind::Normal, 0));
    }
    // A bot moved between the kinds: a normal member rebound in the match becomes bg and leaves the normal record (the record shrinks, or is dropped
    // when it was the last); a bg member rebound normally leaves the book.
    {
        L::BgBook book;
        std::vector<L::BindResult> const moved = { Bound(Dps, L::Kind::Bg) };
        CHECK(L::ApplyBind(book, Cmd, moved, 41));
        CHECK(L::NormalRecordUpdate(moved, Party().members) == L::NormalUpdate::Shrink);
        CHECK(Same(L::NormalMembers(book, Cmd, { Tank, Heal, Dps }), { Tank, Heal }));
        CHECK(L::NormalRecordUpdate(moved, { Dps }) == L::NormalUpdate::Drop);
        CHECK(L::NormalRecordUpdate(moved, {}) == L::NormalUpdate::Keep);
        std::vector<L::BindResult> const back = { Bound(Dps, L::Kind::Normal) };
        CHECK(L::ApplyBind(book, Cmd, back, 0));
        CHECK(!L::FindBg(book, Dps));
        CHECK(L::NormalRecordUpdate(back, { Tank, Heal }) == L::NormalUpdate::Rebind);
        // A rebind to another commander moves the bot's entry (one entry per bot).
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg) }, 41));
        CHECK(L::ApplyBind(book, LeadOther, { Bound(BgBot, L::Kind::Bg) }, 42));
        CHECK(book.size() == 1 && book[0].commander == LeadOther && book[0].instance == 42);
    }
    // A member h3 now gives to another commander (or to none) is dropped from the book, never released (the release would undo the other's bind).
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg) }, 41));
        auto const handed = L::CheckBg(book, { Live(BgBot, 41, 41, LeadOther) }, true);
        CHECK(handed.size() == 1 && !handed[0].release && handed[0].end == L::End::None);
        auto const none = L::CheckBg(book, { Live(BgBot, 41, 41, 0) }, true);
        CHECK(none.size() == 1 && !none[0].release);
    }
    // The module switched off ends every bg member too; a match that ended for both reads as bg-match-ended.
    {
        L::BgBook book;
        CHECK(L::ApplyBind(book, Cmd, { Bound(BgBot, L::Kind::Bg) }, 41));
        auto const off = L::CheckBg(book, { Live(BgBot, 41, 41) }, false);
        CHECK(off.size() == 1 && off[0].release && off[0].end == L::End::Disabled);
        auto const ended = L::CheckBg(book, { Live(BgBot, 0, 0) }, true);
        CHECK(ended.size() == 1 && ended[0].end == L::End::BgMatchEnded);
        L::BgBook zero;
        CHECK(L::ApplyBind(zero, Cmd, { Bound(BgBot, L::Kind::Bg) }, 0));
        auto const noMatch = L::CheckBg(zero, { Live(BgBot, 41, 41) }, true);
        CHECK(noMatch.size() == 1 && noMatch[0].end == L::End::BgMatchEnded);
    }
}

void TestDeferredRelease()
{
    // A group hook records its event; the tick releases only while the binding and the very group the hook saw still stand.
    L::DeferredFacts facts;
    facts.bindingStands = true;
    facts.recordedGroup = 7;
    facts.recordedGroupExists = true;
    CHECK(L::DeferredRelease(facts) == L::Deferred::Release);
    // The binding was released meanwhile (another verdict, a logout): the event has nothing to release.
    facts.bindingStands = false;
    CHECK(L::DeferredRelease(facts) == L::Deferred::Drop);
    facts.bindingStands = true;
    // THE CRASH'S FOLLOW-UP: the group the hook saw was disbanded and freed before the tick (Disband deletes it). The binding still
    // ends with its recorded reason, but no group is reached into.
    facts.recordedGroupExists = false;
    CHECK(L::DeferredRelease(facts) == L::Deferred::WithoutParty);
    // No counter recorded (the hook always has one; a zero means nothing to reach into): the release ends the binding without a
    // party action - it must never fall back to the lead's or a member's current group, which can be another party by this tick.
    facts.recordedGroup = 0;
    CHECK(L::DeferredRelease(facts) == L::Deferred::WithoutParty);
}

void TestMayCommand()
{
    // A standing binding, a bot still bound to it and still in the lead's party: orders may use it.
    CHECK(L::MayCommand(true, false, true, true));
    // THE STALE WINDOW: a group hook recorded the release (a player joined, the lead left, the leader changed) and the tick has not run
    // it yet - the change it recorded is already true, so nothing may be ordered through the binding.
    CHECK(!L::MayCommand(true, true, true, true));
    // The binding is gone or was never restored.
    CHECK(!L::MayCommand(false, false, true, true));
    // h3 binds the bot to another commander.
    CHECK(!L::MayCommand(true, false, false, true));
    // The bot is no longer a member of the lead's live party (it left, and only its own slot was erased): refused at once, not next tick.
    CHECK(!L::MayCommand(true, false, true, false));
}

void TestBgCommandGate()
{
    // A bg member bound to this commander, both in the record's instance, both online, one team: commands may use it.
    L::BgCommandFacts here;
    here.botOnline = here.commanderOnline = here.botInInstance = here.commanderInInstance = here.sameTeam = true;
    CHECK(L::MayCommandBg(true, here));
    // h3 no longer binds the bot to this commander.
    CHECK(!L::MayCommandBg(false, here));

    // THE LEAVE BETWEEN TWO MAINTENANCE TICKS (#315): the commander left the match (or logged out, or was moved to the other side) and the 1 Hz
    // CheckBg has not run yet - the stored member still names him, so an authorisation that read only the member (and h3's row) answered true.
    {
        L::BgCommandFacts left = here;
        left.commanderInInstance = false;
        CHECK(!L::MayCommandBg(true, left));
        L::BgCommandFacts offline = here;
        offline.commanderOnline = false;
        CHECK(!L::MayCommandBg(true, offline));
        L::BgCommandFacts transferred = here;
        transferred.sameTeam = false;
        CHECK(!L::MayCommandBg(true, transferred));
    }
    // The bot's own leave counts the same way, and so does its logout.
    {
        L::BgCommandFacts botLeft = here;
        botLeft.botInInstance = false;
        CHECK(!L::MayCommandBg(true, botLeft));
        L::BgCommandFacts botOffline = here;
        botOffline.botOnline = false;
        CHECK(!L::MayCommandBg(true, botOffline));
    }
}

void TestPickLead()
{
    // A named host bot this commander commands is the lead the command asked for.
    {
        L::LeadFacts facts;
        facts.namedLeader = Tank;
        facts.namedLeaderIsBotOfCommander = true;
        facts.bound = { Heal, Tank };
        facts.partyBots = { Heal, Tank };
        CHECK(L::PickLead(facts) == Tank);
    }
    // THE MUTATION (a player leading his own party): the commander names himself and h3 has no commander of his to compare, so the lead is a bot of
    // his party - a tank first, then the party's order - never the arbitrary first bot h3 happens to hold.
    {
        L::LeadFacts facts;
        facts.namedLeader = Cmd;
        facts.bound = { Heal, Tank, Dps };
        facts.partyBots = { Tank, Heal };
        facts.partyTanks = { Tank };
        CHECK(L::PickLead(facts) == Tank);
        facts = L::LeadFacts();
        facts.namedLeader = Cmd;
        facts.bound = { Dps, Heal, Tank };
        facts.partyBots = { Heal };
        CHECK(L::PickLead(facts) == Heal);          // no tank in the party: its order, not the first bound bot
    }
    // A rebind keeps the previous lead while it is still bound.
    {
        L::LeadFacts facts;
        facts.bound = { Heal, Dps };
        facts.previousLead = Dps;
        CHECK(L::PickLead(facts) == Dps);
        facts.previousLead = Tank;                  // no longer bound: a stale lead is never resurrected
        CHECK(L::PickLead(facts) == Heal);
    }
    // With no lead named or kept, the first bound bot; and nothing bound at all chooses nobody.
    {
        L::LeadFacts facts;
        facts.bound = { Heal, Dps };
        CHECK(L::PickLead(facts) == Heal);
        CHECK(L::PickLead(L::LeadFacts()) == 0);
    }
}

void TestDepartedFollowers()
{
    L::Stored const party = Party();
    // Every follower still in the group: nobody to let go.
    CHECK(L::Departed(party.members, party.lead, { Tank, Heal, Dps, Cmd }).empty());
    // A follower kicked out: its follow target goes back, the binding stands.
    auto const gone = L::Departed(party.members, party.lead, { Tank, Heal });
    CHECK((gone == std::vector<uint64_t>{ Dps }));
    // The lead is never a "departed follower" even if the group scan does not list it.
    CHECK(L::Departed(party.members, party.lead, { Heal, Dps }).empty());
}

void TestRestore()
{
    L::Stored const party = Party();
    // The whole party is back: the followers join the lead's group and every member is bound to the commander.
    {
        auto const plan = L::PlanRestore(party, true, { Tank, Heal, Dps });
        CHECK(plan.ready);
        CHECK((plan.join == std::vector<uint64_t>{ Heal, Dps }));
        CHECK((plan.bind == std::vector<uint64_t>{ Tank, Heal, Dps }));
    }
    // THE MUTATION: a restore with the lead offline must not happen (the row would bind bots around a lead that cannot lead them).
    {
        auto const plan = L::PlanRestore(party, true, { Heal, Dps });
        CHECK(!plan.ready);
        CHECK(plan.join.empty() && plan.bind.empty());
    }
    // ... and neither with the commander offline.
    CHECK(!L::PlanRestore(party, false, { Tank, Heal, Dps }).ready);
    // A member that is not back yet is skipped; the row stays and its next login restores it.
    {
        auto const plan = L::PlanRestore(party, true, { Tank, Heal });
        CHECK(plan.ready);
        CHECK((plan.join == std::vector<uint64_t>{ Heal }));
        CHECK((plan.bind == std::vector<uint64_t>{ Tank, Heal }));
    }
    // A group re-created after the restart: the row itself is unchanged (no group number is stored), and the plan is the same after a round trip
    // through the stored text - the value a restart really reads is the decoded one.
    {
        L::Stored const recreated = party;
        CHECK(recreated.members == party.members && recreated.flags == party.flags && recreated.boundAt == party.boundAt);
        L::Stored decoded;
        decoded.commander = party.commander;
        decoded.lead = party.lead;
        decoded.members = L::DecodeMembers(L::EncodeMembers(party.members));
        decoded.flags = party.flags;
        decoded.boundAt = party.boundAt;
        CHECK(L::Valid(decoded));
        auto const plan = L::PlanRestore(decoded, true, { Tank, Heal, Dps });
        CHECK(plan.ready && plan.bind.size() == 3 && plan.join.size() == 2);
    }
}

void TestClaim()
{
    L::Stored const party = Party();
    L::Owner tank; tank.member = Tank; tank.bot = true;
    L::Owner heal; heal.member = Heal; heal.bot = true;
    L::Owner dps; dps.member = Dps; dps.bot = true;
    // Everything unbound and a host bot: the restore may take it all.
    {
        auto const claims = L::Claim(Cmd, party.members, { tank, heal, dps });
        CHECK((claims.free == party.members) && claims.foreign.empty());
    }
    // A member bound to another commander is never taken (the review's steal): it is dropped from the row, the rest restores.
    {
        heal.commander = LeadOther;
        auto const claims = L::Claim(Cmd, party.members, { tank, heal, dps });
        CHECK((claims.free == std::vector<uint64_t>{ Tank, Dps }));
        CHECK((claims.foreign == std::vector<uint64_t>{ Heal }));
        heal.commander = 0;
    }
    // A member mastered by another character is never taken either (h3 may lag behind a `.tb master`), and neither is a real player.
    {
        dps.master = LeadOther;
        heal.bot = false;
        auto const claims = L::Claim(Cmd, party.members, { tank, heal, dps });
        CHECK((claims.free == std::vector<uint64_t>{ Tank }));
        CHECK((claims.foreign == std::vector<uint64_t>{ Heal, Dps }));   // stored order, never reordered
        dps.master = 0;
        heal.bot = true;
    }
    // Still this commander's: free. An unknown (offline) member contradicts nothing: free.
    {
        heal.commander = Cmd;
        dps.master = Cmd;
        auto const claims = L::Claim(Cmd, party.members, { tank, heal, dps });
        CHECK((claims.free == party.members) && claims.foreign.empty());
        auto const offline = L::Claim(Cmd, party.members, { tank });
        CHECK((offline.free == party.members) && offline.foreign.empty());
    }
}
}

int main()
{
    TestRow();
    TestReleaseReasons();
    TestBgBind();
    TestKindPerBot();
    TestDeferredRelease();
    TestMayCommand();
    TestBgCommandGate();
    TestPickLead();
    TestDepartedFollowers();
    TestRestore();
    TestClaim();
    std::printf("cmd_binding_life_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
