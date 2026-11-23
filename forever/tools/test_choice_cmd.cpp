// Standalone test of the class-choice command rule (forever/src/host/player/TbChoiceCmdPure.h, which also carries the
// player-command tier rule of TbPlayerAuthPure.h), and of the pure class policies the round-2 verbs read (the blessing
// class words, the choice kinds per class, the portal/service data): all of them header-only, no core header.
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_choice_cmd.cpp -o /tmp/test_choice_cmd && /tmp/test_choice_cmd
#include "host/player/TbChoiceCmdPure.h"

#include "../../ai/playerbot/MagePortalPolicy.h"
#include "../../ai/playerbot/strategy/generic/ClassChoicePolicy.h"
#include "../../ai/playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"

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

    // `.bot blessing` (<bot|party> <class> <blessing|auto>): the class words the command accepts are the policy's, in wire
    // order with their CLASS_* ids, and each blessing word is the base spell the paladin's own book is asked about.
    // case: BlessingClassWords
    {
        char const* const classes[] = { "warrior", "paladin", "hunter", "rogue", "priest", "shaman", "mage", "warlock", "druid" };
        std::uint8_t const ids[] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
        CHECK(ai::kBlessingOrderClassCount == 9);
        for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
        {
            CHECK(std::string(ai::BlessingOrderClassWord(i)) == classes[i]);
            CHECK(ai::BlessingOrderClassId(i) == ids[i]);
            CHECK(ai::BlessingOrderClassIndex(classes[i]) == i);
            CHECK(ai::BlessingOrderClassIndexById(ids[i]) == i);
        }
        CHECK(std::string(ai::BlessingOrderClassWord(-1)).empty());
        CHECK(std::string(ai::BlessingOrderClassWord(ai::kBlessingOrderClassCount)).empty());
        CHECK(ai::BlessingOrderClassIndex("deathknight") == -1);
        CHECK(ai::BlessingOrderClassIndex("") == -1);
        CHECK(ai::BlessingOrderClassIndexById(6) == -1);   // the death knight id has no row

        char const* const blessings[] = { "might", "wisdom", "kings", "salvation", "sanctuary", "light" };
        CHECK(ai::kBlessingOrderBlessingCount == 6);
        for (int i = 0; i < ai::kBlessingOrderBlessingCount; ++i)
        {
            CHECK(std::string(ai::BlessingOrderWord(i)) == blessings[i]);
            CHECK(ai::BlessingOrderSpell(blessings[i]) == "blessing of " + std::string(blessings[i]));
            CHECK(ai::IsBlessingOrderWord(blessings[i]));
        }
        CHECK(ai::BlessingOrderSpell("auto").empty());
        CHECK(ai::BlessingOrderSpell("fury").empty());
        CHECK(ai::BlessingOrderSpell("").empty());
        CHECK(ai::IsBlessingOrderWord("auto"));
        CHECK(!ai::IsBlessingOrderWord(""));
        CHECK(!ai::IsBlessingOrderWord("none"));
        CHECK(ai::BlessingOrderKey(3) == "blessing order rogue");
        CHECK(ai::BlessingOrderPlayersKey() == "blessing order players");

        // The per-character exception the `player` form writes replaces one class order; "auto" drops it again.
        std::string players = ai::SetBlessingPlayerOrder(std::string(), "Aegnor", "kings");
        CHECK(players == "Aegnor:kings");
        CHECK(ai::ResolveBlessingOrderSpell(players, "aegnor", "might") == "blessing of kings");
        CHECK(ai::ResolveBlessingOrderSpell("", "aegnor", "might") == "blessing of might");
        CHECK(ai::SetBlessingPlayerOrder(players, "AEGNOR", "auto").empty());
    }

    // `.bot choice` (<bot|party> <kind> <value|auto>): a kind belongs to one class (or two for the hunter and the warlock),
    // poisons are bag items rather than spells, and a word only counts for its own kind.
    // case: ChoiceKindForClass
    {
        CHECK(ai::ChoiceKindIndex("aura") == ai::CHOICE_AURA);
        CHECK(ai::ChoiceKindIndex("aspect") == ai::CHOICE_ASPECT);
        CHECK(ai::ChoiceKindIndex("curse") == ai::CHOICE_CURSE);
        CHECK(ai::ChoiceKindIndex("poison_mh") == ai::CHOICE_POISON_MH);
        CHECK(ai::ChoiceKindIndex("poison_oh") == ai::CHOICE_POISON_OH);
        CHECK(ai::ChoiceKindIndex("sting") == ai::CHOICE_STING);
        CHECK(ai::ChoiceKindIndex("pet") == ai::CHOICE_PET);
        CHECK(ai::ChoiceKindIndex("demon") == -1);
        CHECK(ai::ChoiceKindIndex("") == -1);

        CHECK(ai::ChoiceKindsForClass(2) == std::vector<int>({ ai::CHOICE_AURA }));
        CHECK(ai::ChoiceKindsForClass(3) == std::vector<int>({ ai::CHOICE_ASPECT, ai::CHOICE_STING }));
        CHECK(ai::ChoiceKindsForClass(9) == std::vector<int>({ ai::CHOICE_CURSE, ai::CHOICE_PET }));
        CHECK(ai::ChoiceKindsForClass(4) == std::vector<int>({ ai::CHOICE_POISON_MH, ai::CHOICE_POISON_OH }));
        CHECK(ai::ChoiceKindsForClass(1).empty());   // a warrior keeps no choice
        CHECK(ai::ChoiceKindsForClass(0).empty());
        // Every kind a class lists is one of the class's own, and every kind's class lists it.
        for (std::uint8_t classId : { 1, 2, 3, 4, 5, 7, 8, 9, 11 })
            for (int kind : ai::ChoiceKindsForClass(classId))
                CHECK(ai::ChoiceKindClassId(kind) == classId);
        for (int kind = 0; kind < ai::CHOICE_KIND_COUNT; ++kind)
            CHECK(ai::ChoiceKindsForClass(ai::ChoiceKindClassId(kind)).size() >= 1);

        // Poisons are items (a usable poison item makes the word known); the rest are spells of the bot's own book.
        CHECK(ai::ChoiceKindUsesItem(ai::CHOICE_POISON_MH));
        CHECK(ai::ChoiceKindUsesItem(ai::CHOICE_POISON_OH));
        CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_AURA));
        CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_ASPECT));
        CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_CURSE));
        CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_STING));
        CHECK(!ai::ChoiceKindUsesItem(ai::CHOICE_PET));
        CHECK(!ai::ChoicePoisonItemIds("instant").empty());
        CHECK(!ai::ChoicePoisonItemIds("deadly").empty());
        CHECK(!ai::ChoicePoisonItemIds("crippling").empty());
        CHECK(!ai::ChoicePoisonItemIds("mind").empty());
        CHECK(!ai::ChoicePoisonItemIds("wound").empty());
        CHECK(ai::ChoicePoisonItemIds("bogus").empty());

        // A word only counts for its own kind; "auto" counts for every kind.
        CHECK(ai::IsChoiceWord(ai::CHOICE_AURA, "shadow"));
        CHECK(!ai::IsChoiceWord(ai::CHOICE_AURA, "hawk"));
        CHECK(ai::IsChoiceWord(ai::CHOICE_ASPECT, "hawk"));
        CHECK(!ai::IsChoiceWord(ai::CHOICE_ASPECT, "shadow"));
        CHECK(ai::IsChoiceWord(ai::CHOICE_CURSE, "elements"));
        CHECK(ai::IsChoiceWord(ai::CHOICE_STING, "serpent"));
        CHECK(ai::IsChoiceWord(ai::CHOICE_PET, "voidwalker"));
        for (int kind = 0; kind < ai::CHOICE_KIND_COUNT; ++kind)
            CHECK(ai::IsChoiceWord(kind, "auto"));
        CHECK(!ai::IsChoiceWord(ai::CHOICE_CURSE, ""));

        // The spell each order casts, expanded the way the strategy glue reads it.
        CHECK(ai::ChoiceSpell(ai::CHOICE_AURA, "shadow") == "shadow resistance aura");
        CHECK(ai::ChoiceSpell(ai::CHOICE_AURA, "retribution") == "retribution aura");
        CHECK(ai::ChoiceSpell(ai::CHOICE_ASPECT, "hawk") == "aspect of the hawk");
        CHECK(ai::ChoiceSpell(ai::CHOICE_CURSE, "elements") == "curse of the elements");
        CHECK(ai::ChoiceSpell(ai::CHOICE_CURSE, "agony") == "curse of agony");
        CHECK(ai::ChoiceSpell(ai::CHOICE_POISON_MH, "mind") == "mind-numbing poison");
        CHECK(ai::ChoiceSpell(ai::CHOICE_POISON_OH, "instant") == "instant poison");
        CHECK(ai::ChoiceSpell(ai::CHOICE_STING, "serpent") == "serpent sting");
        CHECK(ai::ChoiceSpell(ai::CHOICE_PET, "imp") == "summon imp");
        CHECK(ai::ChoiceSpell(ai::CHOICE_AURA, "imp").empty());
        CHECK(ai::ChoiceOrderKey(ai::CHOICE_POISON_MH) == "choice poison_mh");

        // The service query's portal half: table order, "id:City" pairs, "-" when none.
        CHECK(std::string(ai::PortalCityName(10059)) == "Stormwind");
        CHECK(std::string(ai::PortalCityName(1)).empty());
        CHECK(ai::FormatServicePortals({}) == "-");
        CHECK(ai::FormatServicePortals({ 10059, 11416 }) == "10059:Stormwind,11416:Ironforge");
        CHECK(ai::MatchPortalCity("iron") == 11416);
        CHECK(ai::MatchPortalCity("nowhere") == 0);
        ai::PortalCastFacts facts;
        CHECK(ai::DecidePortalCast(facts, 10059, false, true, false).refusal == ai::PortalRefusal::NotLearned);
        CHECK(ai::DecidePortalCast(facts, 10059, true, false, false).needsRune);
        CHECK(ai::DecidePortalCast(facts, 10059, true, true, true).needsStop);
        facts.alive = false;
        CHECK(ai::DecidePortalCast(facts, 10059, true, true, false).refusal == ai::PortalRefusal::Dead);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
