// Pure tests for the `choice` addon verb (TbAddonChoicePure.h + ClassChoicePolicy.h).
// Build & run:
//   g++ -std=c++17 -I. -Iforever/src forever/tools/test_addon_choice_policy.cpp -o /tmp/test_addon_choice_policy && /tmp/test_addon_choice_policy
#include "host/TbAddonChoicePure.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace TortoiseBots::choice;

int main()
{
    // ParseChoiceArgs: status and valid set commands
    {
        auto p = ParseChoiceArgs("  Party Status  ");
        assert(p.ok);
        assert(p.status);
        assert(p.scopeIsParty);
        assert(p.scope == "party");
        assert(p.intent == "choice status");
    }
    {
        auto p = ParseChoiceArgs("raid status");
        assert(p.ok);
        assert(p.status);
        assert(p.scopeIsParty);
        assert(p.scope == "raid");
    }
    {
        auto p = ParseChoiceArgs("Legolas Aspect Hawk");
        assert(p.ok);
        assert(!p.status);
        assert(!p.scopeIsParty);
        assert(p.scope == "legolas");
        assert(p.kind == ai::CHOICE_ASPECT);
        assert(p.word2 == "aspect");
        assert(p.word3 == "hawk");
        assert(p.intent == "choice");
    }
    {
        auto p = ParseChoiceArgs("Guldan pet voidwalker");
        assert(p.ok);
        assert(p.kind == ai::CHOICE_PET);
        assert(p.word3 == "voidwalker");
    }
    {
        auto p = ParseChoiceArgs("Guldan pet auto");
        assert(p.ok);
        assert(p.kind == ai::CHOICE_PET);
        assert(p.word3 == "auto");
    }
    {
        auto p = ParseChoiceArgs("Vaelira poison_mh instant");
        assert(p.ok);
        assert(p.kind == ai::CHOICE_POISON_MH);
        assert(p.word3 == "instant");
    }
    // Invalid inputs
    {
        assert(!ParseChoiceArgs("").ok);
        assert(!ParseChoiceArgs("Legolas").ok);
        assert(!ParseChoiceArgs("Legolas bogus hawk").ok);
        assert(!ParseChoiceArgs("Legolas aspect bogus").ok);
        assert(!ParseChoiceArgs("Legolas aspect").ok);
    }

    // FormatChoiceLine
    {
        std::string const line = FormatChoiceLine("Legolas", ai::CHOICE_ASPECT, "", "hawk+monkey");
        assert(line == "TBM:CHOICE|Legolas|aspect=auto,known=hawk+monkey");
        std::string const setLine = FormatChoiceLine("Guldan", ai::CHOICE_PET, "voidwalker", "imp+voidwalker");
        assert(setLine == "TBM:CHOICE|Guldan|pet=voidwalker,known=imp+voidwalker");
    }

    // ClassChoicePolicy pure helpers
    {
        assert(ai::ChoiceSpell(ai::CHOICE_AURA, "shadow") == "shadow resistance aura");
        assert(ai::ChoiceSpell(ai::CHOICE_AURA, "devotion") == "devotion aura");
        assert(ai::ChoiceSpell(ai::CHOICE_ASPECT, "hawk") == "aspect of the hawk");
        assert(ai::ChoiceSpell(ai::CHOICE_CURSE, "elements") == "curse of the elements");
        assert(ai::ChoiceSpell(ai::CHOICE_CURSE, "agony") == "curse of agony");
        assert(ai::ChoiceSpell(ai::CHOICE_POISON_MH, "mind") == "mind-numbing poison");
        assert(ai::ChoiceSpell(ai::CHOICE_STING, "viper") == "viper sting");
        assert(ai::ChoiceSpell(ai::CHOICE_PET, "voidwalker") == "summon voidwalker");

        assert(ai::ChoiceOrderedWord(ai::CHOICE_PET, "auto").empty());
        assert(ai::ChoiceOrderedWord(ai::CHOICE_PET, "").empty());
        assert(ai::ChoiceOrderedWord(ai::CHOICE_PET, "voidwalker") == "voidwalker");
        assert(ai::ChoiceRowAllowed("", "serpent"));
        assert(ai::ChoiceRowAllowed("viper", "viper"));
        assert(!ai::ChoiceRowAllowed("viper", "serpent"));
        assert(ai::ShouldNoCurseTriggerFire(""));
        assert(!ai::ShouldNoCurseTriggerFire("curse of agony"));
        assert(ai::DecideChoicePet("voidwalker", true, false, false, true, false) == ai::ChoicePetDecision::Summon);
        assert(ai::DecideChoicePet("voidwalker", true, true, false, true, false) == ai::ChoicePetDecision::LeaveAlone);
        assert(ai::DecideChoicePet("voidwalker", true, false, true, true, false) == ai::ChoicePetDecision::WaitCombat);
        assert(ai::DecideChoicePet("voidwalker", true, false, false, false, false) == ai::ChoicePetDecision::NoShard);
        assert(ai::DecideChoicePet("imp", true, false, false, false, false) == ai::ChoicePetDecision::Summon);
    }

    std::cout << "PASS: test_addon_choice_policy (all checks passed)\n";
    return 0;
}
