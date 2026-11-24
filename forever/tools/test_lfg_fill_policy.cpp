// Standalone test of the dungeon-finder fill's decisions (forever/src/host/TbLfgFillPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_lfg_fill_policy.cpp -o /tmp/test_lfg_fill_policy && /tmp/test_lfg_fill_policy
//
// The cases are the ones a live fill depends on: the party the core's finder asks for (1 tank, 1 healer, 3 damage), the
// eligibility list of TortoiseBots' LFT fill, the two role layers, the natural-spec rule and its borrowing switch, the
// preference order of two eligible bots, and when the fill lets a party go.
#include "forever/src/host/TbLfgFillPure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace lfg = TortoiseBots::lfgfill;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string Slots(std::vector<std::uint8_t> const& roles)
{
    std::string out;
    for (std::uint8_t role : roles)
        out += (out.empty() ? "" : ",") + std::string(lfg::RoleName(role));
    return out;
}

static lfg::CandidateFacts EligibleBot()
{
    lfg::CandidateFacts bot;
    bot.inWorld = true;
    bot.free = true;
    bot.hasBrain = true;
    bot.alive = true;
    bot.level = 40;
    bot.team = 469;
    bot.classId = 1;                  // warrior
    bot.naturalRoles = lfg::RoleTank;
    return bot;
}

int main()
{
    // ---- roles: the words, the names and the two bit layers ----
    CHECK(lfg::RoleOf("tank") == lfg::RoleTank);
    CHECK(lfg::RoleOf("heal") == lfg::RoleHealer);
    CHECK(lfg::RoleOf("healer") == lfg::RoleHealer);
    CHECK(lfg::RoleOf("dps") == lfg::RoleDps);
    CHECK(lfg::RoleOf("damage") == lfg::RoleDps);
    CHECK(lfg::RoleOf("paladin") == lfg::RoleNone);
    CHECK(std::string(lfg::RoleName(lfg::RoleHealer)) == "healer");
    CHECK(lfg::CoreRoleBits(lfg::RoleTank) == lfg::CoreRoleTank);
    CHECK(lfg::CoreRoleBits(lfg::RoleHealer) == lfg::CoreRoleHealer);
    CHECK(lfg::CoreRoleBits(lfg::RoleDps) == lfg::CoreRoleDamage);
    CHECK(lfg::CoreRoleBits(lfg::RoleNone) == 0);
    CHECK(lfg::EngineRoleOfCore(lfg::CoreRoleLeader | lfg::CoreRoleTank) == lfg::RoleTank);
    CHECK(lfg::EngineRoleOfCore(lfg::CoreRoleLeader | lfg::CoreRoleHealer) == lfg::RoleHealer);
    CHECK(lfg::EngineRoleOfCore(lfg::CoreRoleLeader | lfg::CoreRoleDamage) == lfg::RoleDps);

    // ---- the party the core asks for ----
    CHECK(Slots(lfg::MissingSlots(lfg::RoleDps, {})) == "tank,healer,dps,dps");
    CHECK(Slots(lfg::MissingSlots(lfg::RoleTank, {})) == "healer,dps,dps,dps");
    CHECK(Slots(lfg::MissingSlots(lfg::RoleHealer, {})) == "tank,dps,dps,dps");
    CHECK(Slots(lfg::MissingSlots(lfg::RoleDps, { lfg::RoleTank })) == "healer,dps,dps");
    CHECK(Slots(lfg::MissingSlots(lfg::RoleDps, { lfg::RoleTank, lfg::RoleHealer })) == "dps,dps");
    CHECK(Slots(lfg::MissingSlots(lfg::RoleDps, { lfg::RoleTank, lfg::RoleHealer, lfg::RoleDps })) == "dps");
    CHECK(lfg::MissingSlots(lfg::RoleDps, { lfg::RoleTank, lfg::RoleHealer, lfg::RoleDps, lfg::RoleDps }).empty());
    // A second tank or healer claim does not eat a slot it does not have; the excess is ignored.
    CHECK(Slots(lfg::MissingSlots(lfg::RoleTank, { lfg::RoleTank, lfg::RoleTank })) == "healer,dps,dps,dps");

    // ---- one refusal at a time, from an otherwise eligible tank ----
    {
        struct Case { char const* what; void (*spoil)(lfg::CandidateFacts&); lfg::Refusal expected; };
        Case const cases[] =
        {
            { "not in the world", [](lfg::CandidateFacts& f) { f.inWorld = false; }, lfg::Refusal::NotInWorld },
            { "not free", [](lfg::CandidateFacts& f) { f.free = false; }, lfg::Refusal::NotFree },
            { "has a master", [](lfg::CandidateFacts& f) { f.hasMaster = true; }, lfg::Refusal::NotFree },
            { "grouped", [](lfg::CandidateFacts& f) { f.grouped = true; }, lfg::Refusal::Grouped },
            { "no brain", [](lfg::CandidateFacts& f) { f.hasBrain = false; }, lfg::Refusal::NoBrain },
            { "in a battleground", [](lfg::CandidateFacts& f) { f.inBattleground = true; }, lfg::Refusal::InBattleground },
            { "in a battleground queue", [](lfg::CandidateFacts& f) { f.inBattlegroundQueue = true; }, lfg::Refusal::InBattleground },
            { "teleporting", [](lfg::CandidateFacts& f) { f.teleporting = true; }, lfg::Refusal::Teleporting },
            { "on a taxi", [](lfg::CandidateFacts& f) { f.flying = true; }, lfg::Refusal::Flying },
            { "in combat", [](lfg::CandidateFacts& f) { f.inCombat = true; }, lfg::Refusal::InCombat },
            { "dead", [](lfg::CandidateFacts& f) { f.alive = false; }, lfg::Refusal::Dead },
            { "level 9", [](lfg::CandidateFacts& f) { f.level = 9; }, lfg::Refusal::LowLevel },
            { "other team", [](lfg::CandidateFacts& f) { f.team = 67; }, lfg::Refusal::OtherTeam },
            { "claimed", [](lfg::CandidateFacts& f) { f.claimed = true; }, lfg::Refusal::Claimed },
            { "no tank spec", [](lfg::CandidateFacts& f) { f.naturalRoles = lfg::RoleDps; }, lfg::Refusal::WrongRole },
        };
        for (Case const& c : cases)
        {
            lfg::CandidateFacts facts = EligibleBot();
            c.spoil(facts);
            lfg::Refusal const got = lfg::CandidateRefusal(facts, lfg::RoleTank, 469, false);
            if (got != c.expected)
                std::printf("FAIL %s: got %s\n", c.what, lfg::RefusalName(got));
            ++g_checks;
            g_fail += got != c.expected;
        }
    }

    // ---- natural spec vs borrowing ----
    {
        lfg::CandidateFacts mage = EligibleBot();
        mage.classId = 8;
        mage.naturalRoles = lfg::RoleDps;
        CHECK(lfg::CandidateRefusal(mage, lfg::RoleDps, 469, false) == lfg::Refusal::None);
        CHECK(lfg::CandidateRefusal(mage, lfg::RoleTank, 469, false) == lfg::Refusal::WrongRole);
        CHECK(lfg::CandidateRefusal(mage, lfg::RoleTank, 469, true) == lfg::Refusal::NoKit);

        lfg::CandidateFacts warrior = EligibleBot();   // arms warrior: no tank spec, but the class has a tank kit
        warrior.naturalRoles = lfg::RoleDps;
        CHECK(lfg::CandidateRefusal(warrior, lfg::RoleTank, 469, false) == lfg::Refusal::WrongRole);
        CHECK(lfg::CandidateRefusal(warrior, lfg::RoleTank, 469, true) == lfg::Refusal::None);

        lfg::CandidateFacts priest = EligibleBot();
        priest.classId = 5;
        priest.naturalRoles = lfg::RoleHealer;
        CHECK(lfg::CandidateRefusal(priest, lfg::RoleHealer, 469, false) == lfg::Refusal::None);
        CHECK(lfg::ClassFillsRole(5, lfg::RoleHealer));
        CHECK(lfg::ClassFillsRole(7, lfg::RoleHealer));
        CHECK(!lfg::ClassFillsRole(8, lfg::RoleHealer));
        CHECK(!lfg::ClassFillsRole(5, lfg::RoleTank));
    }

    // ---- preference: natural over borrowed, then the level, then the order ----
    {
        std::vector<lfg::CandidateFacts> bots(3, EligibleBot());
        bots[0].naturalRoles = lfg::RoleDps;      // borrows the tank slot, closest level
        bots[0].level = 40;
        bots[1].naturalRoles = lfg::RoleTank;     // natural, far from the player
        bots[1].level = 60;
        bots[2].naturalRoles = lfg::RoleTank;     // natural, closest level and smaller index than 1's gap? 20 vs 1
        bots[2].level = 41;
        lfg::Choice const choice = lfg::PickCandidate(bots, lfg::RoleTank, 40, 469, true);
        CHECK(choice.index == 2);
        CHECK(!choice.borrowed);
        CHECK(lfg::PickCandidate(bots, lfg::RoleTank, 40, 469, false).index == 2);

        // Two natural candidates at the same gap: the lower index wins (the caller passes them by guid).
        std::vector<lfg::CandidateFacts> tied(2, EligibleBot());
        tied[0].level = 41;
        tied[1].level = 39;
        CHECK(lfg::PickCandidate(tied, lfg::RoleTank, 40, 469, false).index == 0);

        // No eligible candidate: -1.
        std::vector<lfg::CandidateFacts> none(1, EligibleBot());
        none[0].naturalRoles = lfg::RoleNone;
        CHECK(lfg::PickCandidate(none, lfg::RoleTank, 40, 469, false).index == -1);
        // Borrowing picks it and says so.
        lfg::Choice const borrowed = lfg::PickCandidate(none, lfg::RoleTank, 40, 469, true);
        CHECK(borrowed.index == 0);
        CHECK(borrowed.borrowed);
    }

    // ---- release and completion ----
    {
        lfg::PlayerFacts player;
        player.online = true;
        player.inWorld = true;
        player.inGroup = true;
        CHECK(!lfg::ShouldRelease(player));
        player.inGroup = false;
        CHECK(lfg::ShouldRelease(player));
        player.inGroup = true;
        player.online = false;
        CHECK(lfg::ShouldRelease(player));
        player.online = true;
        player.inWorld = false;
        CHECK(lfg::ShouldRelease(player));
        CHECK(!lfg::PartyComplete(4));
        CHECK(lfg::PartyComplete(5));
    }

    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("test_lfg_fill_policy: %d checks OK\n", g_checks);
    return 0;
}
