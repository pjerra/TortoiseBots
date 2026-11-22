// Standalone test of the core-free half of the tactical orders (forever/src/host/TbTacticsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_tactics_pure.cpp -o /tmp/test_tactics_pure && /tmp/test_tactics_pure
// M1: the engage seconds and the stance word. A mutation that accepts 61 s, refuses `turn`, or maps `turn` to `near` fails here.
#include "forever/src/host/TbTacticsPure.h"

#include <cstdio>
#include <string>

namespace t = TortoiseBots::tactics;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestEngageSeconds()
{
    unsigned seconds = 0;
    CHECK(t::kEngageDefaultSeconds == 4);
    CHECK(t::ParseEngageSeconds("", seconds) && seconds == t::kEngageDefaultSeconds);
    CHECK(t::ParseEngageSeconds("0", seconds) && seconds == 0);
    CHECK(t::ParseEngageSeconds("5", seconds) && seconds == 5);
    CHECK(t::ParseEngageSeconds("60", seconds) && seconds == 60);
    CHECK(!t::ParseEngageSeconds("61", seconds));
    CHECK(!t::ParseEngageSeconds("-1", seconds));
    CHECK(!t::ParseEngageSeconds("5s", seconds));
    CHECK(!t::ParseEngageSeconds("+5", seconds));
    CHECK(!t::ParseEngageSeconds(" ", seconds));
    CHECK(!t::ParseEngageSeconds("999999999999999999999", seconds));
}

static void TestStance()
{
    std::string out;
    CHECK(t::NormaliseStance("turn", out) && out == "turnback");
    CHECK(t::NormaliseStance("turnback", out) && out == "turnback");
    CHECK(t::NormaliseStance("Behind", out) && out == "behind");
    CHECK(t::NormaliseStance("near", out) && out == "near");
    CHECK(t::NormaliseStance("tank", out) && out == "tank");
    CHECK(!t::NormaliseStance("left", out));
    CHECK(!t::NormaliseStance("", out));
    CHECK(!t::NormaliseStance("turn back", out));
}

static void TestVerbs()
{
    for (char const* verb : { "stop", "flee", "guard", "stance", "engage", "rebuff" })
        CHECK(t::IsTacticalVerb(verb));
    for (char const* verb : { "", "follow", "attack", "status" })
        CHECK(!t::IsTacticalVerb(verb));
}

int main()
{
    TestEngageSeconds();
    TestStance();
    TestVerbs();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("%d checks passed\n", g_checks);
    return 0;
}
