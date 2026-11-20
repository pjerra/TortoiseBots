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
    // A player-led binding ignores who the group leader is (the player may lead); a system binding ignores a real player joining.
    {
        L::Stored playerLed = party;
        playerLed.flags = L::FlagsOf(false, false, true, false);
        L::Facts facts = Healthy();
        facts.leadIsLeader = false;
        CHECK(L::Decide(playerLed, facts) == L::End::None);
        facts.realPlayerInGroup = true;
        CHECK(L::Decide(playerLed, facts) == L::End::PlayerJoined);   // player-led is not in-party: a stranger still ends it
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
        CHECK((claims.foreign == std::vector<uint64_t>{ Dps, Heal }));
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
    TestDepartedFollowers();
    TestRestore();
    TestClaim();
    std::printf("cmd_binding_life_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
