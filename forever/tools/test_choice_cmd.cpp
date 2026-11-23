// Standalone test of the class-choice command rule (forever/src/host/player/TbChoiceCmdPure.h, which also carries the
// player-command tier rule of TbPlayerAuthPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_choice_cmd.cpp -o /tmp/test_choice_cmd && /tmp/test_choice_cmd
#include "host/player/TbChoiceCmdPure.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace c = TortoiseBots::choice;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// The toggle table as old TB had it (commands/Party.cpp:1268-1281): key, strategy, state, inversion, off-spec only.
struct Expected
{
    char const* key;
    char const* strategy;
    c::ToggleState state;
    bool inverted;
    bool offSpec;
};

bool SameToggle(c::Toggle const& toggle, Expected const& expected)
{
    return std::string_view(toggle.key) == expected.key && std::string_view(toggle.strategy) == expected.strategy &&
           toggle.state == expected.state && toggle.inverted == expected.inverted && toggle.offSpec == expected.offSpec;
}
}

int main()
{
    // THE FAILING CASE: the ported table is old TB's thirteen keys, in old TB's order, with old TB's strategies and states.
    // case: ToggleTableMatchesTb
    {
        static Expected const expected[] = {
            { "loot", "loot", c::ToggleState::NonCombat, false, false },
            { "aoe", "no aoe", c::ToggleState::Combat, true, false },
            { "autocc", "auto cc", c::ToggleState::Combat, false, false },
            { "savemana", "conserve mana", c::ToggleState::Combat, false, false },
            { "boost", "boost", c::ToggleState::Combat, false, false },
            { "threat", "threat", c::ToggleState::Combat, false, false },
            { "potions", "potions", c::ToggleState::Reaction, false, false },
            { "mimic", "mimic consumables", c::ToggleState::NonCombat, false, false },
            { "spread", "spread", c::ToggleState::Combat, false, false },
            { "hold", "hold position", c::ToggleState::Combat, false, false },
            { "noblink", "no blink instance", c::ToggleState::Combat, false, false },
            { "offheal", "offheal", c::ToggleState::Combat, false, true },
            { "offdps", "offdps", c::ToggleState::Combat, false, true },
        };
        std::size_t const expectedCount = sizeof(expected) / sizeof(expected[0]);
        std::size_t count = 0;
        c::Toggle const* table = c::ToggleTable(count);
        CHECK(count == expectedCount);
        for (std::size_t i = 0; i < count && i < expectedCount; ++i)
            CHECK(SameToggle(table[i], expected[i]));
        // Every key finds its own row, and nothing else.
        for (std::size_t i = 0; i < count; ++i)
        {
            c::Toggle const* found = c::FindToggle(table[i].key);
            CHECK(found == &table[i]);
        }
        CHECK(c::FindToggle("") == nullptr);
        CHECK(c::FindToggle("aoe2") == nullptr);
        CHECK(c::FindToggle("AOE") == nullptr);   // the caller lowers its words; the table is lowercase
    }

    // `aoe` is the inverted toggle: `no aoe` held reads aoe=off; dropping it reads aoe=on.
    // case: AoeIsInverted
    {
        c::Toggle const* aoe = c::FindToggle("aoe");
        CHECK(aoe != nullptr);
        if (aoe)
        {
            CHECK(aoe->inverted);
            CHECK(!c::IsToggleOn(true, *aoe));    // "no aoe" present: aoe is off
            CHECK(c::IsToggleOn(false, *aoe));    // "no aoe" absent: aoe is on
            CHECK(c::ToggleChange(false, *aoe) == "+no aoe");
            CHECK(c::ToggleChange(true, *aoe) == "-no aoe");
        }
        c::Toggle const* loot = c::FindToggle("loot");
        CHECK(loot != nullptr);
        if (loot)
        {
            CHECK(!loot->inverted);
            CHECK(c::IsToggleOn(true, *loot));
            CHECK(!c::IsToggleOn(false, *loot));
            CHECK(c::ToggleChange(true, *loot) == "+loot");
            CHECK(c::ToggleChange(false, *loot) == "-loot");
        }
    }

    // status reads only: Tactical for both verbs, and for R2's bare service query.
    // case: StatusIsTactical
    {
        CHECK(c::TierOfChoice("totem", "status") == c::Tier::Tactical);
        CHECK(c::TierOfChoice("behavior", "status") == c::Tier::Tactical);
        CHECK(c::TierOfChoice("behaviour", "STATUS") == c::Tier::Tactical);
        CHECK(c::TierOfChoice("service", "") == c::Tier::Tactical);
    }

    // set and every other sub-command moves the bot: Owner tier.
    // case: SetIsOwner
    {
        CHECK(c::TierOfChoice("totem", "set") == c::Tier::Owner);
        CHECK(c::TierOfChoice("totem", "earth") == c::Tier::Owner);
        CHECK(c::TierOfChoice("totem", "mode") == c::Tier::Owner);
        CHECK(c::TierOfChoice("behavior", "loot") == c::Tier::Owner);
        CHECK(c::TierOfChoice("behavior", "offheal") == c::Tier::Owner);
        CHECK(c::TierOfChoice("service", "portal") == c::Tier::Owner);
        CHECK(c::TierOfChoice("service", "conjure") == c::Tier::Owner);
    }

    // Anything not pinned Tactical fails closed to Owner: an empty sub-command, a word nobody knows, an empty verb.
    // case: UnknownSubIsOwner
    {
        CHECK(c::TierOfChoice("totem", "") == c::Tier::Owner);
        CHECK(c::TierOfChoice("behavior", "") == c::Tier::Owner);
        CHECK(c::TierOfChoice("totem", "bogus") == c::Tier::Owner);
        CHECK(c::TierOfChoice("", "") == c::Tier::Owner);
        CHECK(c::TierOfChoice("", "status") == c::Tier::Tactical);   // the sub-command alone still reads only
    }

    // The scope word: `party` is the caller's group; every other word is a bot name, as typed (the host normalizes it).
    // case: ScopePartyAndName
    {
        c::ParsedScope party = c::ParseScope("party");
        CHECK(party.kind == c::ScopeKind::Party);
        CHECK(party.name.empty());

        c::ParsedScope bot = c::ParseScope("bob");
        CHECK(bot.kind == c::ScopeKind::Bot);
        CHECK(bot.name == "bob");

        // `raid` is not a scope word in this round: it is read as a bot name (and fails the bot lookup).
        c::ParsedScope raid = c::ParseScope("raid");
        CHECK(raid.kind == c::ScopeKind::Bot);
        CHECK(raid.name == "raid");

        // The tokenizer: whitespace-split, lowered, the scope first.
        std::vector<std::string> const words = c::ChoiceWords("  Bob   EARTH  strength of EARTH totem ");
        CHECK(words.size() == 6);
        if (words.size() == 6)
        {
            CHECK(words[0] == "bob");
            CHECK(words[1] == "earth");
            CHECK(words[2] == "strength");
            CHECK(words[3] == "of");
            CHECK(words[4] == "earth");
            CHECK(words[5] == "totem");   // the caller rejoins the tail; the tokenizer only splits and lowers
            CHECK(c::ParseScope(words[0]).kind == c::ScopeKind::Bot);
        }
        CHECK(c::ChoiceWords("   ").empty());
        CHECK(c::ChoiceWords("").empty());
        CHECK(c::ChoiceWords("party status").size() == 2);
    }

    // on/off is the only write word; anything else is a usage refusal.
    // case: ParseOnOff
    {
        bool on = false;
        CHECK(c::ParseOnOff("on", on) && on);
        CHECK(c::ParseOnOff("off", on) && !on);
        CHECK(!c::ParseOnOff("", on));
        CHECK(!c::ParseOnOff("maybe", on));
        CHECK(!c::ParseOnOff("1", on));
    }

    // Off-spec toggles exist only on paladins, priests, shamans and druids (OffSpecPolicy.h); the others have every toggle.
    // case: ToggleAppliesToClass
    {
        c::Toggle const* offheal = c::FindToggle("offheal");
        c::Toggle const* loot = c::FindToggle("loot");
        CHECK(offheal != nullptr && loot != nullptr);
        if (offheal && loot)
        {
            CHECK(!c::ToggleAppliesToClass(*offheal, false));
            CHECK(c::ToggleAppliesToClass(*offheal, true));
            CHECK(c::ToggleAppliesToClass(*loot, false));
            CHECK(c::ToggleAppliesToClass(*loot, true));
        }
    }

    // The plain tier rule stays what it was; the choice tier is a separate question.
    // case: PlainTierUnchanged
    {
        CHECK(c::TierOf("follow") == c::Tier::Tactical);
        CHECK(c::TierOf("logout") == c::Tier::Owner);
        CHECK(c::TierOf("totem") == c::Tier::Owner);
        CHECK(c::TierOf("behavior") == c::Tier::Owner);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
