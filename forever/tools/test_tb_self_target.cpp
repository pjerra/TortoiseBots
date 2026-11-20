// Standalone test of the TB-1 target rule (forever/src/host/cmd/TbTestTargetPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_tb_self_target.cpp -o /tmp/test_tb_self_target && /tmp/test_tb_self_target
// The three cases of the brief: the sender's own character is accepted, another real player is refused, a host bot is still accepted; plus the words
// that say "no name was given" (the no-name forms of the five commands).
#include "forever/src/host/cmd/TbTestTargetPure.h"

#include <cstdint>
#include <cstdio>

namespace tg = TortoiseBots::h3::target;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
constexpr tg::Guid Self{10, 1}, Other{11, 1}, Bot{12, 1}, None{};
}

int main()
{
    // The sender's own character.
    CHECK(tg::MayTarget(Self, Self, false));
    CHECK(tg::MayTarget(Self, Self, true));
    // Another real player: never a target, from the console or from a player.
    CHECK(!tg::MayTarget(Other, Self, false));
    CHECK(!tg::MayTarget(Other, None, false));
    // A host bot is still accepted, whoever typed the command.
    CHECK(tg::MayTarget(Bot, Self, true));
    CHECK(tg::MayTarget(Bot, None, true));
    // No character at all, and the console's own character (it has none).
    CHECK(!tg::MayTarget(None, Self, false));
    CHECK(!tg::MayTarget(None, Self, true));
    // The same low word under a different high word is another identity.
    CHECK(!tg::MayTarget(tg::Guid{10, 2}, Self, false));

    // The words that are already the command's own argument mean "no name was given".
    for (char const* word : { "tank", "healer", "heal", "dps", "none", "clear" })
        CHECK(tg::IsRoleWord(word));
    CHECK(!tg::IsRoleWord("party"));
    CHECK(!tg::IsRoleWord("Mtank"));
    CHECK(!tg::IsRoleWord(""));
    for (char const* word : { "mana", "rage", "energy" })
        CHECK(tg::IsPowerWord(word));
    CHECK(!tg::IsPowerWord("tank"));
    CHECK(!tg::IsPowerWord("Mtank"));
    for (char const* word : { "1", "1234", "999999999" })
        CHECK(tg::IsIdWord(word));
    CHECK(!tg::IsIdWord("0"));
    CHECK(!tg::IsIdWord("Mtank"));
    CHECK(!tg::IsIdWord("12a"));
    CHECK(!tg::IsIdWord("1000000000"));    // more than an id's 9 digits
    CHECK(!tg::IsIdWord(""));
    for (char const* word : { "0", "50", "100" })
        CHECK(tg::IsNumberWord(word));
    CHECK(!tg::IsNumberWord("Mtank"));
    CHECK(!tg::IsNumberWord("5%"));
    CHECK(!tg::IsNumberWord(""));

    std::printf("test_tb_self_target: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
