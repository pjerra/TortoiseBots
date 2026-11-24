// Standalone test of BG2 live row 5's bind decision (forever/src/host/cmd/TbMasterBindPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_master_bind_pure.cpp -o /tmp/test_master_bind_pure && /tmp/test_master_bind_pure
//
// The live row (notes/live-test3-batch1-2026-11-22.md, BG2 row 5): `.tb master Nougat Bghum` from the operator's console was
// refused "not-allowed: Bghum is neither a host bot nor the sender's own character", so a bound bot could not be set up inside
// the match. The cases below are that refusal, kept for a player outside a match, and the new allowance for one match instance
// and one team, with the two refusals beside it (another instance, the other team).
#include "forever/src/host/cmd/TbMasterBindPure.h"

#include <cstdio>
#include <string>

namespace mb = TortoiseBots::cmd::master_bind;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    mb::Facts f;

    // The live refusal is kept: outside a battleground a real player who is not the sender is refused, with the old code and text.
    f = mb::Facts{};
    CHECK(mb::Judge(f) == mb::Verdict::NotAllowed);
    CHECK(std::string(mb::CodeOf(mb::Judge(f))) == "not-allowed");
    CHECK(mb::DetailOf(mb::Judge(f), "Bghum") ==
          "Bghum is neither a host bot nor your own character: it cannot be made the commander of bots");

    // The plain rule is not second-guessed: a host bot, or the sender's own character, binds anywhere (also across instances).
    f = mb::Facts{};
    f.commanderMayLead = true;
    CHECK(mb::Judge(f) == mb::Verdict::Allowed);
    f = mb::Facts{};
    f.commanderMayLead = true;
    f.botInstance = 41;
    f.commanderInstance = 7;
    f.sameTeam = false;
    CHECK(mb::Judge(f) == mb::Verdict::Allowed);

    // The live row's setup, now allowed: the bot and the human in the same Warsong Gulch instance, on the same team.
    f = mb::Facts{};
    f.botInstance = 41;
    f.commanderInstance = 41;
    f.sameTeam = true;
    CHECK(mb::Judge(f) == mb::Verdict::Allowed);
    CHECK(std::string(mb::CodeOf(mb::Verdict::Allowed)) == "allowed");   // never printed: it is not a refusal

    // The same instance, the other team: refused, and the text says why.
    f = mb::Facts{};
    f.botInstance = 41;
    f.commanderInstance = 41;
    f.sameTeam = false;
    CHECK(mb::Judge(f) == mb::Verdict::OtherTeam);
    CHECK(std::string(mb::CodeOf(mb::Judge(f))) == "other-team");
    CHECK(mb::DetailOf(mb::Judge(f), "Bghum").find("other team") != std::string::npos);

    // Two different match instances: refused.
    f = mb::Facts{};
    f.botInstance = 41;
    f.commanderInstance = 42;
    f.sameTeam = true;
    CHECK(mb::Judge(f) == mb::Verdict::OtherInstance);
    CHECK(std::string(mb::CodeOf(mb::Judge(f))) == "other-instance");

    // One of them outside a match: the plain refusal, not a battleground one (a bot in the world, the human in a match or the reverse).
    f = mb::Facts{};
    f.botInstance = 0;
    f.commanderInstance = 41;
    f.sameTeam = true;
    CHECK(mb::Judge(f) == mb::Verdict::NotAllowed);
    f = mb::Facts{};
    f.botInstance = 41;
    f.commanderInstance = 0;
    f.sameTeam = true;
    CHECK(mb::Judge(f) == mb::Verdict::NotAllowed);
    f = mb::Facts{};
    f.botInstance = 0;
    f.commanderInstance = 0;
    CHECK(mb::Judge(f) == mb::Verdict::NotAllowed);

    // An instance id is never read as "inside" when it is 0 on both sides: two players in the world with equal (0) ids are not a match.
    f = mb::Facts{};
    f.botInstance = 0;
    f.commanderInstance = 0;
    f.sameTeam = true;
    CHECK(mb::Judge(f) != mb::Verdict::Allowed);

    // Every verdict but Allowed has a distinct refusal code, and none of them is the old one except the plain refusal.
    CHECK(std::string(mb::CodeOf(mb::Verdict::OtherTeam)) != std::string(mb::CodeOf(mb::Verdict::OtherInstance)));
    CHECK(std::string(mb::CodeOf(mb::Verdict::OtherTeam)) != std::string(mb::CodeOf(mb::Verdict::NotAllowed)));
    CHECK(std::string(mb::CodeOf(mb::Verdict::OtherInstance)) != std::string(mb::CodeOf(mb::Verdict::NotAllowed)));
    CHECK(mb::DetailOf(mb::Verdict::Allowed, "Bghum").empty());

    // The kind the binding check gets (IsBgBind): the same match instance and team, whatever let the bind through. It is what makes the record a
    // battleground bind - never stored, ended only by its match - so the three live ids and the team decide it, nothing else.
    {
        f = mb::Facts{};
        f.botInstance = 41;
        f.commanderInstance = 41;
        f.sameTeam = true;
        CHECK(mb::IsBgBind(f));
        f.commanderMayLead = true;              // a host bot or the sender's own character inside the match is a battleground bind too
        CHECK(mb::IsBgBind(f));
        f.sameTeam = false;
        CHECK(!mb::IsBgBind(f));
        f = mb::Facts{};
        f.botInstance = 41;
        f.commanderInstance = 42;
        f.sameTeam = true;
        CHECK(!mb::IsBgBind(f));
        f = mb::Facts{};
        f.botInstance = 0;
        f.commanderInstance = 0;
        f.sameTeam = true;
        CHECK(!mb::IsBgBind(f));
        f = mb::Facts{};
        f.commanderInstance = 41;
        f.sameTeam = true;
        CHECK(!mb::IsBgBind(f));
    }

    // The decision is total: every fact combination has a verdict, and only the plain rule or one instance + one team allows.
    for (unsigned mask = 0; mask < 16; ++mask)
    {
        mb::Facts g;
        g.commanderMayLead = (mask & 1) != 0;
        unsigned const a = (mask >> 1) & 1, b = (mask >> 2) & 1;
        g.botInstance = a ? 41u : 0u;
        g.commanderInstance = b ? 41u : 0u;
        g.sameTeam = (mask & 8) != 0;
        bool const allowed = mb::Judge(g) == mb::Verdict::Allowed;
        bool const expect = g.commanderMayLead || (a && b && g.sameTeam);
        CHECK(allowed == expect);
        CHECK(mb::IsBgBind(g) == (a && b && g.sameTeam));
    }

    std::printf("test_master_bind_pure: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
