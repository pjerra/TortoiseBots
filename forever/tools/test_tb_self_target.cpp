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
    CHECK(!tg::IsIdWord("00"));
    CHECK(!tg::IsIdWord("Mtank"));
    CHECK(!tg::IsIdWord("12a"));
    CHECK(!tg::IsIdWord("1000000000"));    // more than an id's 9 digits
    CHECK(!tg::IsIdWord(""));
    for (char const* word : { "0", "50", "100" })
        CHECK(tg::IsNumberWord(word));
    CHECK(!tg::IsNumberWord("Mtank"));
    CHECK(!tg::IsNumberWord("5%"));
    CHECK(!tg::IsNumberWord(""));

    // `.tb removeitem`: no count = all of it; a count is capped at what the bags hold; nothing in the bags removes nothing.
    CHECK(tg::RemoveAmount(7, 0) == 7);
    CHECK(tg::RemoveAmount(7, 3) == 3);
    CHECK(tg::RemoveAmount(7, 7) == 7);
    CHECK(tg::RemoveAmount(7, 50) == 7);
    CHECK(tg::RemoveAmount(0, 0) == 0);
    CHECK(tg::RemoveAmount(0, 5) == 0);

    // ParseIdWord (the id / count of additem, equip, removeitem): digits only. These words used to go through strtoul: "-1" wrapped to 4294967295 = "all".
    {
        std::uint32_t v = 0;
        for (char const* word : { "1", "5", "2589", "999999999" })
            CHECK(tg::ParseIdWord(word, v));
        v = 0;
        CHECK(tg::ParseIdWord("2589", v) && v == 2589);
        CHECK(tg::ParseIdWord("999999999", v) && v == 999999999u);
        for (char const* word : { "-1", "-5", "+3", "0", "00", "99999999999", "4294967295", "1000000000", "3x", "x3", " 5", "5 ", "0x10", "1e3", "3.5", "" })
        {
            v = 7;
            CHECK(!tg::ParseIdWord(word, v));
            CHECK(v == 7);   // a refused word leaves the output alone
        }
    }

    // The bags-only collection rule (IsBagStack(bagSlot, slot, backpackSlots, inTrade)): 28 backpack slots, as the pinned core's default.
    {
        std::uint8_t const N = 28;
        // the backpack: slots 35..62
        CHECK(tg::IsBagStack(255, 35, N, false));
        CHECK(tg::IsBagStack(255, 62, N, false));
        CHECK(!tg::IsBagStack(255, 63, N, false));      // the bank's first slot
        CHECK(!tg::IsBagStack(255, 34, N, false));      // the reagent bag item itself
        CHECK(tg::IsBagStack(255, 35, 16, false) && !tg::IsBagStack(255, 51, 16, false));   // a smaller backpack
        // equipped, the worn bags themselves, the profession gear: all of the player's own low slots
        for (unsigned slot = 0; slot < 35; ++slot)
            CHECK(!tg::IsBagStack(255, std::uint8_t(slot), N, false));
        // the contents of the four worn bags and of the reagent bag (bag slots 30..34)
        for (unsigned bag = 30; bag <= 34; ++bag)
            CHECK(tg::IsBagStack(std::uint8_t(bag), 0, N, false) && tg::IsBagStack(std::uint8_t(bag), 15, N, false));
        // the bank bags (63..71), the buyback area, anything else
        for (unsigned bag : { 35u, 63u, 64u, 71u, 72u, 84u, 100u, 254u })
            CHECK(!tg::IsBagStack(std::uint8_t(bag), 3, N, false));
        // the keyring / child equipment slots of the player (84..86) and the 87+ spell slots
        for (unsigned slot : { 84u, 85u, 86u, 87u, 96u })
            CHECK(!tg::IsBagStack(255, std::uint8_t(slot), N, false));
        // a stack in an open trade is never collected, wherever it lies (the case where the core's entry-based destroy ran on into equipment and bank)
        CHECK(!tg::IsBagStack(255, 40, N, true));
        CHECK(!tg::IsBagStack(31, 2, N, true));
        CHECK(!tg::IsBagStack(34, 2, N, true));
    }

    std::printf("test_tb_self_target: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
