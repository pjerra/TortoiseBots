// Pure tests for the `professions` addon verb (TbAddonProfessionsPure.h + ProfessionSnapshotPolicy.h).
// Build & run:
//   g++ -std=c++17 -I. -Iforever/src forever/tools/test_addon_professions.cpp -o /tmp/test_addon_professions && /tmp/test_addon_professions
#include "host/TbAddonProfessionsPure.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace TortoiseBots::professions;
using namespace TortoiseBots::ProfessionSnapshotPolicy;

int main()
{
    int checks = 0;
    auto ok = [&](bool cond)
    {
        assert(cond);
        ++checks;
    };

    // 1. Argument parsing
    {
        ParsedProfessions p = ParseProfessionsArgs("  Aelindra  ");
        ok(p.ok);
        ok(p.botNameLower == "aelindra");
    }
    {
        ok(!ParseProfessionsArgs("").ok);
        ok(!ParseProfessionsArgs("   ").ok);
        ok(!ParseProfessionsArgs("Aelindra extra").ok);
    }

    // 2. Profession skill filter (primary + First Aid/Cooking/Fishing, no Tauren Racial)
    {
        ok(IsProfessionSkill(kCategoryProfession, 197));
        ok(IsProfessionSkill(kCategorySecondary, kSkillFirstAid));
        ok(IsProfessionSkill(kCategorySecondary, kSkillCooking));
        ok(IsProfessionSkill(kCategorySecondary, kSkillFishing));
        ok(!IsProfessionSkill(kCategorySecondary, 125)); // non-profession secondary
    }

    // 3. Known recipe & missing recipe filtering
    {
        AbilityRow knownCraft;
        knownCraft.skillId = 197;
        knownCraft.spellId = 2387; // Linen Cloak
        knownCraft.reqSkillValue = 1;
        knownCraft.minValue = 15;
        knownCraft.maxValue = 35;
        knownCraft.known = true;
        knownCraft.createsItem = true;
        knownCraft.createdItemId = 2570;
        ok(IsKnownRecipe(knownCraft));

        AbilityRow fishingCast;
        fishingCast.skillId = kSkillFishing;
        fishingCast.spellId = 7620;
        fishingCast.known = true;
        ok(!IsKnownRecipe(fishingCast));

        AbilityRow skillStep;
        skillStep.skillId = 197;
        skillStep.spellId = 3908;
        skillStep.known = true;
        skillStep.isSkillStep = true;
        ok(!IsKnownRecipe(skillStep));

        AbilityRow missingTrainer;
        missingTrainer.skillId = 197;
        missingTrainer.spellId = 2393;
        missingTrainer.reqSkillValue = 30;
        missingTrainer.createsItem = true;
        missingTrainer.createdItemId = 2572;
        ok(IsMissingRecipe(missingTrainer, 75, true, 0));
        ok(!IsMissingRecipe(missingTrainer, 20, true, 0));   // above rank max
        ok(!IsMissingRecipe(missingTrainer, 75, false, 0));  // unobtainable

        missingTrainer.learnOnGetSkill = true;
        ok(!IsMissingRecipe(missingTrainer, 75, true, 0));   // auto-learned
    }

    // 4. Full snapshot wire formatting
    {
        std::vector<ProfessionRow> profs = {
            { 197, 45, 75 },
            { 129, 20, 75 },
        };
        AbilityRow r1;
        r1.skillId = 197;
        r1.spellId = 2387;
        r1.reqSkillValue = 1;
        r1.minValue = 15;
        r1.maxValue = 35;
        r1.known = true;
        r1.createsItem = true;
        r1.createdItemId = 2570;

        std::vector<RecipeSnapshot> recipes = {
            { r1, "ok" },
        };
        std::vector<MissingRow> missing = {
            { 197, 2393, 2572, 30, 0 },
        };

        std::vector<std::string> lines = BuildSnapshotLines("Ael|indra", profs, recipes, missing);
        ok(lines.size() == 6);
        ok(lines[0] == "TBM:PROF_BEGIN|Ael indra");
        ok(lines[1] == "TBM:PROF|197|45|75");
        ok(lines[2] == "TBM:PROF|129|20|75");
        ok(lines[3] == "TBM:RECIPE|197|2387|2570|1|15|25|35|ok");
        ok(lines[4] == "TBM:RECIPE_MISSING|197|2393|2572|30|0");
        ok(lines[5] == "TBM:PROF_END|Ael indra|1|1");
    }

    std::cout << "test_addon_professions: " << checks << " checks passed\n";
    return 0;
}
