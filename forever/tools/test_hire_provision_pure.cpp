// The hire-provision decisions without the core (forever/src/host/hire/TbHireProvisionPure.h).
//   g++ -std=c++17 -Wall -Wextra -I. -Iforever/src forever/tools/test_hire_provision_pure.cpp runtime/HireCost.cpp -o /tmp/t && timeout 30 /tmp/t
#include "forever/src/host/hire/TbHireProvisionPure.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots;
using namespace TortoiseBots::hire;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

namespace
{

HireGateFacts AllowedFacts()
{
    HireGateFacts facts;
    facts.hasRecruiter = true;
    facts.hireLevel = 30;
    facts.maxHires = 4;
    facts.groupMembers = 1;
    facts.highestUnlockedTier = 2;
    return facts;
}

} // namespace

int main()
{
    // 1. Gate order: each refusal comes with its own verdict, the cheapest first.
    {
        HireGateFacts facts = AllowedFacts();
        CHECK(DecideHireGates(facts).verdict == HireVerdict::Ok);

        HireGateFacts outOfWorld = AllowedFacts();
        outOfWorld.realPlayer = false;
        outOfWorld.enabled = false;
        outOfWorld.accountAllowed = false;
        CHECK(DecideHireGates(outOfWorld).verdict == HireVerdict::Failed);

        HireGateFacts disabled = AllowedFacts();
        disabled.enabled = false;
        disabled.accountAllowed = false;
        CHECK(DecideHireGates(disabled).verdict == HireVerdict::Disabled);

        HireGateFacts noAccount = AllowedFacts();
        noAccount.accountAllowed = false;
        CHECK(DecideHireGates(noAccount).verdict == HireVerdict::NoPermission);

        HireGateFacts queued = AllowedFacts();
        queued.pvpAllowedNow = false;
        queued.hasRecruiter = false;
        CHECK(DecideHireGates(queued).verdict == HireVerdict::Failed);

        HireGateFacts noRecruiter = AllowedFacts();
        noRecruiter.hasRecruiter = false;
        CHECK(DecideHireGates(noRecruiter).verdict == HireVerdict::RecruiterRequired);
        noRecruiter.hireAnywhere = true;
        CHECK(DecideHireGates(noRecruiter).verdict == HireVerdict::Ok);

        HireGateFacts capped = AllowedFacts();
        capped.activeHires = 4;
        CHECK(DecideHireGates(capped).verdict == HireVerdict::CapReached);
        capped.hireAnywhere = true;
        CHECK(DecideHireGates(capped).verdict == HireVerdict::CapReached);

        HireGateFacts full = AllowedFacts();
        full.groupMembers = 5;
        CHECK(DecideHireGates(full).verdict == HireVerdict::GroupFull);

        HireGateFacts raid = AllowedFacts();
        raid.groupIsRaid = true;
        CHECK(DecideHireGates(raid).verdict == HireVerdict::GroupFull);

        HireGateFacts locked = AllowedFacts();
        locked.hireLevel = 60;
        locked.requestedTier = 3;
        locked.highestUnlockedTier = 1;
        HireGateResult const tier = DecideHireGates(locked);
        CHECK(tier.verdict == HireVerdict::TierLocked);
        CHECK(tier.message.find("tier 1") != std::string::npos);

        HireGateFacts poor = AllowedFacts();
        poor.canAfford = false;
        CHECK(DecideHireGates(poor).verdict == HireVerdict::Poor);
    }

    // 2. The tier only counts at 60 with tiers on, and above the maximum clamps.
    {
        CHECK(EffectiveGearTier(true, 60, 3) == 3);
        CHECK(EffectiveGearTier(false, 60, 3) == 0);
        CHECK(EffectiveGearTier(true, 59, 3) == 0);
        CHECK(EffectiveGearTier(true, 60, 99) == 7);
        CHECK(EffectiveGearTier(true, 60, 0) == 0);
    }

    // 3. The fee: party escalation on hires 1-4, level scaling, tier multiplier, then the staff waiver.
    {
        HireFeeConfig fee;
        fee.cost.baseCopper = 15000;
        fee.cost.mult2 = 1.66f;
        fee.cost.mult3 = 2.66f;
        fee.cost.mult4 = 4.66f;
        fee.cost.raidFlatCopper = 10000;
        CHECK(QuoteHireFee(fee, 0, 60, 0) == 15000);
        CHECK(QuoteHireFee(fee, 1, 60, 0) == 24899);   // 15000 x 1.66f, truncated as HireCost.cpp does
        CHECK(QuoteHireFee(fee, 0, 30, 0) == 7500);
        CHECK(QuoteHireFee(fee, 0, 60, 1) == 22500);
        CHECK(QuoteHireFee(fee, 0, 0, 0) == 250);        // level 0 reads as 1
        // AiPlayerbot.HireFreeMinSecurity: 0 = off, then the account's level decides.
        CHECK(ChargeableHireFee(15000, 0, 0) == 15000);
        CHECK(ChargeableHireFee(15000, 0, 3) == 15000);
        CHECK(ChargeableHireFee(15000, 3, 3) == 0);
        CHECK(ChargeableHireFee(15000, 4, 3) == 0);
    }

    // 4. Rollback: the ledger is what makes the guarded deletion possible.
    {
        CHECK(DecideHireRollback(false) == HireRollback::RefundAndDeleteUnclaimed);
        CHECK(DecideHireRollback(true) == HireRollback::RefundAndDismiss);
    }

    // 5. `.bot hire` grammar: words in any order, defaults, and the two ambiguous druid shapes.
    {
        HireCommandParse const mage = ParseHireCommand("mage", 40, true);
        CHECK(mage.ok && mage.classId == kHireClassMage);
        CHECK(mage.role == kHireRoleDps);
        CHECK(mage.race == 0 && mage.gender == 0);
        CHECK(mage.specIndex == -1 && mage.specTab == -1);

        HireCommandParse const frost = ParseHireCommand("frost mage", 40, true);
        CHECK(frost.ok && frost.classId == kHireClassMage);
        CHECK(frost.specTab == 2 && frost.specIndex == 0 && frost.role == kHireRoleDps);

        HireCommandParse const prot = ParseHireCommand("warrior tank", 40, true);
        CHECK(prot.ok && prot.role == kHireRoleTank);
        CHECK(prot.specTab == -1 && prot.specIndex == -1);   // role-driven, as TortoiseBots leaves it

        HireCommandParse const protByName = ParseHireCommand("human female protection warrior", 40, true);
        CHECK(protByName.ok && protByName.race == 1 && protByName.gender == 1);
        CHECK(protByName.role == kHireRoleTank && protByName.specTab == 2);

        HireCommandParse const bear = ParseHireCommand("druid bear", 40, true);
        CHECK(bear.ok && bear.role == kHireRoleTank && bear.specTab == 1 && bear.specIndex == 0);
        HireCommandParse const cat = ParseHireCommand("cat druid", 40, true);
        CHECK(cat.ok && cat.role == kHireRoleDps && cat.specTab == 1 && cat.specIndex == 3);
        HireCommandParse const druidDps = ParseHireCommand("druid dps", 40, true);
        CHECK(druidDps.ok && druidDps.specIndex == 3 && druidDps.specTab == 1);

        HireCommandParse const holy = ParseHireCommand("priest holy", 40, true);
        CHECK(holy.ok && holy.role == kHireRoleHealer && holy.specTab == 1 && holy.specIndex == 0);

        HireCommandParse const shamanTank = ParseHireCommand("shaman tank", 40, true);
        CHECK(shamanTank.ok && shamanTank.role == kHireRoleTank);
        CHECK(shamanTank.specTab == 1 && shamanTank.specIndex == 3);
        CHECK(!ParseHireCommand("shaman tank", 40, false).ok);
        CHECK(!ParseHireCommand("shaman tank", 9, true).ok);

        HireCommandParse const tiered = ParseHireCommand("mage mc level=42", 40, true);
        CHECK(tiered.ok && tiered.gearTier == 3 && tiered.requestedLevel == 42);
        HireCommandParse const tierWord = ParseHireCommand("mage t2", 40, true);
        CHECK(tierWord.ok && tierWord.gearTier == 2);
        HireCommandParse const levelPrefix = ParseHireCommand("mage level 35", 40, true);
        CHECK(levelPrefix.ok && levelPrefix.requestedLevel == 35);
        HireCommandParse const bareLevel = ParseHireCommand("mage 35", 40, true);
        CHECK(bareLevel.ok && bareLevel.requestedLevel == 35);

        // Numbers: a low bare number is a race/class id, not a level; a misplaced one refuses.
        HireCommandParse const lowNumber = ParseHireCommand("mage 5", 40, true);
        CHECK(lowNumber.ok && lowNumber.race == 5 && lowNumber.requestedLevel == 0);
        CHECK(!ParseHireCommand("42 mage", 40, true).ok);
        CHECK(!ParseHireCommand("mage level=0", 40, true).ok);
        CHECK(!ParseHireCommand("mage level=x", 40, true).ok);
        CHECK(!ParseHireCommand("", 40, true).ok);

        // A role the class cannot fill refuses.
        HireCommandParse const badRole = ParseHireCommand("warrior healer", 40, true);
        CHECK(!badRole.ok);

        // Shaman tank is the one class with four recruiter rows.
        HireCommandParse const resto = ParseHireCommand("shaman resto", 40, true);
        CHECK(resto.ok && resto.role == kHireRoleHealer && resto.specTab == 2 && resto.specIndex == 0);
    }

    // 6. The gear builder's spec words: the weight scales are named beast/marks/surv, feraldps/feraltank, ...
    {
        CHECK(GearSpecWord(kHireClassHunter, 0, kHireRoleDps) == "beast");
        CHECK(GearSpecWord(kHireClassHunter, 1, kHireRoleDps) == "marks");
        CHECK(GearSpecWord(kHireClassPaladin, 2, kHireRoleDps) == "retrib");
        CHECK(GearSpecWord(kHireClassDruid, 1, kHireRoleTank) == "bear");
        CHECK(GearSpecWord(kHireClassDruid, 1, kHireRoleDps) == "cat");
        CHECK(GearSpecWord(kHireClassShaman, 1, kHireRoleTank) == "enhance");
        CHECK(GearSpecWord(kHireClassMage, -1, kHireRoleDps).empty());
        for (uint8_t classId : { kHireClassWarrior, kHireClassPaladin, kHireClassHunter, kHireClassRogue,
                 kHireClassPriest, kHireClassShaman, kHireClassMage, kHireClassWarlock, kHireClassDruid })
            for (int tab = 0; tab <= 2; ++tab)
                CHECK(!GearSpecWord(classId, tab, kHireRoleDps).empty());
    }

    // 7. Every recruiter menu row maps to a tab and back (the gossip path passes only the index).
    {
        uint8_t const classes[] = { kHireClassWarrior, kHireClassPaladin, kHireClassHunter, kHireClassRogue,
            kHireClassPriest, kHireClassShaman, kHireClassMage, kHireClassWarlock, kHireClassDruid };
        for (uint8_t classId : classes)
        {
            uint32_t count = 0;
            HireSpecOption const* options = HireSpecOptions(classId, count);
            CHECK(options && count >= 3);
            for (uint32_t index = 0; index < count; ++index)
            {
                int const tab = SpecTabForIndex(classId, int(index));
                CHECK(tab >= 0 && tab <= 2);
                int const back = HireSpecIndexForTab(classId, tab, options[index].role);
                // Bear and cat share the feral tab: both roles round-trip to a row of that class.
                uint32_t backCount = 0;
                HireSpecOption const* backOptions = HireSpecOptions(classId, backCount);
                CHECK(back >= 0 && backOptions && uint32_t(back) < backCount);
                CHECK(backOptions[back].role == options[index].role);
            }
        }
    }

    // 8. The mustering login: FindBot has no player until the bot reaches the world, so a live session is what says
    // the login started; only a missing session or the bound ends the hire.
    {
        CHECK(DecideLoginWait(true, true, false) == LoginWait::InWorld);
        CHECK(DecideLoginWait(true, true, true) == LoginWait::InWorld);
        CHECK(DecideLoginWait(false, true, false) == LoginWait::Loading);
        CHECK(DecideLoginWait(false, true, true) == LoginWait::TimedOut);
        CHECK(DecideLoginWait(false, false, false) == LoginWait::Failed);
        CHECK(DecideLoginWait(false, false, true) == LoginWait::Failed);
    }

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: hire provision decisions\n");
    return 0;
}
