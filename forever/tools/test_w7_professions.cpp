// Standalone pure test for W7 profession policies:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_w7_professions.cpp -o /tmp/test_w7_professions && /tmp/test_w7_professions

#include "ai/playerbot/policy/profession/ProfessionPickPolicy.h"
#include "ai/playerbot/policy/profession/GatherNodePolicy.h"
#include "ai/playerbot/policy/profession/CraftForSelfPolicy.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace prof = TortoiseBots::prof;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                      \
    do                                                                        \
    {                                                                         \
        ++g_checks;                                                           \
        if (!(cond))                                                          \
        {                                                                     \
            ++g_failed;                                                       \
            std::printf("FAIL: %s (line %d)\n", (msg), __LINE__);             \
        }                                                                     \
    } while (0)

int main()
{
    // 1. Failing case first: herb node needing skill 70 for a bot with skill 50 must skip with code=skill.
    {
        prof::GatherNodeFacts f;
        f.exists = true;
        f.spawned = true;
        f.ready = true;
        f.lockSkillId = prof::SkillHerbalism;
        f.reqSkill = 70;
        f.botSkill = 50;
        f.hasSkill = true;
        f.freeBagSlots = 10;
        f.distanceYd = 4.0f;

        prof::GatherDecision d = prof::EvaluateGatherNode(f);
        CHECK(!d.allow, "herb req 70 with skill 50 must be skipped");
        CHECK(std::strcmp(d.code, "skill") == 0, "herb req 70 with skill 50 must return code=skill");
    }

    // 2. Gather checks: ready/looted, zone, range, no-prof, tool, bags, danger, difficulty colors.
    {
        prof::GatherNodeFacts base;
        base.exists = true;
        base.spawned = true;
        base.ready = true;
        base.lockSkillId = prof::SkillMining;
        base.reqSkill = 65;
        base.botSkill = 70;
        base.hasSkill = true;
        base.freeBagSlots = 5;
        base.hasRequiredTool = true;
        base.dangerNearby = false;
        base.distanceYd = 10.0f;
        base.maxDistanceYd = 150.0f;
        base.sameZone = true;

        prof::GatherDecision ok = prof::EvaluateGatherNode(base);
        CHECK(ok.allow && std::strcmp(ok.code, "ok") == 0, "valid mining node allowed");
        CHECK(ok.difficulty == prof::GatherDifficulty::Orange, "delta 5 is orange");

        prof::GatherNodeFacts yellow = base;
        yellow.botSkill = 95;
        CHECK(prof::EvaluateGatherNode(yellow).difficulty == prof::GatherDifficulty::Yellow, "delta 30 is yellow");

        prof::GatherNodeFacts green = base;
        green.botSkill = 125;
        CHECK(prof::EvaluateGatherNode(green).difficulty == prof::GatherDifficulty::Green, "delta 60 is green");

        prof::GatherNodeFacts grey = base;
        grey.botSkill = 175;
        CHECK(prof::EvaluateGatherNode(grey).difficulty == prof::GatherDifficulty::Grey, "delta 110 is grey");

        prof::GatherNodeFacts looted = base;
        looted.ready = false;
        CHECK(std::strcmp(prof::EvaluateGatherNode(looted).code, "looted") == 0, "not ready -> looted");

        prof::GatherNodeFacts farNode = base;
        farNode.distanceYd = 151.0f;
        CHECK(std::strcmp(prof::EvaluateGatherNode(farNode).code, "range") == 0, "over 150yd -> range");

        prof::GatherNodeFacts otherZone = base;
        otherZone.sameZone = false;
        CHECK(std::strcmp(prof::EvaluateGatherNode(otherZone).code, "zone") == 0, "other zone -> zone");

        prof::GatherNodeFacts noProf = base;
        noProf.hasSkill = false;
        CHECK(std::strcmp(prof::EvaluateGatherNode(noProf).code, "no-prof") == 0, "no skill -> no-prof");

        prof::GatherNodeFacts noTool = base;
        noTool.hasRequiredTool = false;
        CHECK(std::strcmp(prof::EvaluateGatherNode(noTool).code, "tool") == 0, "missing tool -> tool");

        prof::GatherNodeFacts fullBags = base;
        fullBags.freeBagSlots = 0;
        CHECK(std::strcmp(prof::EvaluateGatherNode(fullBags).code, "bags") == 0, "0 bag slots -> bags");

        prof::GatherNodeFacts danger = base;
        danger.dangerNearby = true;
        CHECK(std::strcmp(prof::EvaluateGatherNode(danger).code, "danger") == 0, "danger nearby -> danger");
    }

    // 3. Pick policy: settled pairs per class, free points = 0 picks nothing, fishing rejected.
    {
        auto pWar = prof::PickProfessionsToLearn(prof::ClassWarrior, 2, {});
        CHECK(pWar.size() == 2 && pWar[0] == prof::SkillMining && pWar[1] == prof::SkillBlacksmithing, "warrior picks mining+bs");

        auto pPal = prof::PickProfessionsToLearn(prof::ClassPaladin, 2, {});
        CHECK(pPal.size() == 2 && pPal[0] == prof::SkillMining && pPal[1] == prof::SkillBlacksmithing, "paladin picks mining+bs");

        auto pHun = prof::PickProfessionsToLearn(prof::ClassHunter, 2, {});
        CHECK(pHun.size() == 2 && pHun[0] == prof::SkillSkinning && pHun[1] == prof::SkillLeatherworking, "hunter picks skin+lw");

        auto pRog = prof::PickProfessionsToLearn(prof::ClassRogue, 2, {});
        CHECK(pRog.size() == 2 && pRog[0] == prof::SkillSkinning && pRog[1] == prof::SkillLeatherworking, "rogue picks skin+lw");

        auto pDru = prof::PickProfessionsToLearn(prof::ClassDruid, 2, {});
        CHECK(pDru.size() == 2 && pDru[0] == prof::SkillSkinning && pDru[1] == prof::SkillLeatherworking, "druid picks skin+lw");

        auto pSha = prof::PickProfessionsToLearn(prof::ClassShaman, 2, {});
        CHECK(pSha.size() == 2 && pSha[0] == prof::SkillSkinning && pSha[1] == prof::SkillLeatherworking, "shaman picks skin+lw");

        auto pMag = prof::PickProfessionsToLearn(prof::ClassMage, 2, {});
        CHECK(pMag.size() == 2 && pMag[0] == prof::SkillHerbalism && pMag[1] == prof::SkillAlchemy, "mage picks herb+alch");

        auto pLock = prof::PickProfessionsToLearn(prof::ClassWarlock, 2, {});
        CHECK(pLock.size() == 2 && pLock[0] == prof::SkillHerbalism && pLock[1] == prof::SkillAlchemy, "warlock picks herb+alch");

        auto pPri = prof::PickProfessionsToLearn(prof::ClassPriest, 2, {});
        CHECK(pPri.size() == 2 && pPri[0] == prof::SkillTailoring && pPri[1] == prof::SkillEnchanting, "priest picks tailor+ench");

        auto noneFree = prof::PickProfessionsToLearn(prof::ClassMage, 0, {});
        CHECK(noneFree.empty(), "0 free primary points picks nothing");

        auto oneKnown = prof::PickProfessionsToLearn(prof::ClassMage, 1, { prof::SkillHerbalism });
        CHECK(oneKnown.size() == 1 && oneKnown[0] == prof::SkillAlchemy, "1 known picks remaining pair");

        CHECK(prof::SkillIdFromName("mining") == prof::SkillMining, "name lookup mining");
        CHECK(prof::SkillIdFromName("Fishing") == 0, "fishing is out");
    }

    // 4. Craft policy: Linen Bag only with cloth in bags + free bag slot to wear it; missing tool skipped.
    {
        prof::CraftFacts bag;
        bag.spellId = 3755; // Linen Bag
        bag.known = true;
        bag.skillId = prof::SkillTailoring;
        bag.reqSkill = 45;
        bag.botSkill = 50;
        bag.hasRequiredTool = true;
        bag.freeBagSlots = 4;
        bag.createsBag = true;
        bag.hasFreeEquipBagSlot = true;
        bag.purpose = prof::CraftPurpose::Bag;
        bag.reagents = { { 2996, 3, 3 }, { 2320, 3, 5 } }; // Bolt of Linen Cloth x3, Coarse Thread x3

        CHECK(prof::EvaluateCraftForSelf(bag).allow, "Linen Bag with cloth and free equip bag slot allowed");

        prof::CraftFacts noEquipSlot = bag;
        noEquipSlot.hasFreeEquipBagSlot = false;
        CHECK(std::strcmp(prof::EvaluateCraftForSelf(noEquipSlot).code, "bags") == 0, "Linen Bag without free bag slot to wear it -> bags");

        prof::CraftFacts noCloth = bag;
        noCloth.reagents[0].have = 2;
        CHECK(std::strcmp(prof::EvaluateCraftForSelf(noCloth).code, "reagents") == 0, "Linen Bag missing cloth -> reagents");

        prof::CraftFacts noTool = bag;
        noTool.hasRequiredTool = false;
        CHECK(std::strcmp(prof::EvaluateCraftForSelf(noTool).code, "tool") == 0, "recipe missing tool -> tool");

        prof::CraftFacts lowSkill = bag;
        lowSkill.botSkill = 30;
        CHECK(std::strcmp(prof::EvaluateCraftForSelf(lowSkill).code, "skill") == 0, "recipe low skill -> skill");
    }

    // 5. Multi-tick gather errand: never ok before loot; node-gone, timeout, and no-loot handled.
    {
        prof::GatherErrandFacts f;
        f.inWorld = true;
        f.alive = true;
        f.phase = prof::GatherPhase::Walking;
        f.nodeExists = true;
        f.nodeReady = true;
        f.inInteractRange = false;
        f.isMoving = false;
        auto s1 = prof::StepGatherErrand(f);
        CHECK(s1.action == prof::GatherErrandAction::MoveToNode, "walking idle bot moves to node");
        CHECK(std::strcmp(s1.stateLabel, "pending:walking") == 0, "walking reports pending:walking, never ok");

        prof::GatherErrandFacts taken = f;
        taken.nodeReady = false;
        auto sTaken = prof::StepGatherErrand(taken);
        CHECK(sTaken.action == prof::GatherErrandAction::FinishFail, "node taken while walking fails");
        CHECK(std::strcmp(sTaken.failCode, "node-gone") == 0, "node taken -> code=node-gone");

        prof::GatherErrandFacts walkTo = f;
        walkTo.phaseElapsedMs = 20000;
        auto sWalkTo = prof::StepGatherErrand(walkTo);
        CHECK(sWalkTo.action == prof::GatherErrandAction::FinishFail && std::strcmp(sWalkTo.failCode, "timeout") == 0, "walk timeout -> code=timeout");

        prof::GatherErrandFacts inRange = f;
        inRange.inInteractRange = true;
        auto s2 = prof::StepGatherErrand(inRange);
        CHECK(s2.nextPhase == prof::GatherPhase::Casting && s2.action == prof::GatherErrandAction::CastGather, "in range -> CastGather");
        CHECK(std::strcmp(s2.stateLabel, "pending:casting") == 0, "casting reports pending:casting");

        prof::GatherErrandFacts casting = f;
        casting.phase = prof::GatherPhase::Casting;
        casting.isCasting = true;
        casting.phaseElapsedMs = 1500;
        auto sCastWait = prof::StepGatherErrand(casting);
        CHECK(sCastWait.action == prof::GatherErrandAction::Wait && std::strcmp(sCastWait.stateLabel, "pending:casting") == 0, "mid-cast waits as pending:casting");

        prof::GatherErrandFacts castLoot = casting;
        castLoot.isCasting = false;
        castLoot.lootAvailable = true;
        auto sLoot = prof::StepGatherErrand(castLoot);
        CHECK(sLoot.nextPhase == prof::GatherPhase::Looting && sLoot.action == prof::GatherErrandAction::LootNode, "loot ready -> LootNode");

        prof::GatherErrandFacts lootEmpty = f;
        lootEmpty.phase = prof::GatherPhase::Looting;
        lootEmpty.lootAttempted = true;
        lootEmpty.itemsGained = 0;
        auto sNoLoot = prof::StepGatherErrand(lootEmpty);
        CHECK(sNoLoot.action == prof::GatherErrandAction::FinishFail && std::strcmp(sNoLoot.failCode, "no-loot") == 0, "looted 0 items -> fail:no-loot");

        prof::GatherErrandFacts lootOk = lootEmpty;
        lootOk.itemsGained = 1;
        auto sOk = prof::StepGatherErrand(lootOk);
        CHECK(sOk.nextPhase == prof::GatherPhase::Done && sOk.action == prof::GatherErrandAction::FinishOk, "itemsGained > 0 -> FinishOk");
        CHECK(std::strcmp(sOk.stateLabel, "ok") == 0, "FinishOk reports ok");

        prof::GatherErrandFacts castTo = casting;
        castTo.phaseElapsedMs = 10000;
        auto sCastTo = prof::StepGatherErrand(castTo);
        CHECK(sCastTo.action == prof::GatherErrandAction::FinishFail && std::strcmp(sCastTo.failCode, "timeout") == 0, "cast timeout -> code=timeout");
    }

    // 6. Multi-tick craft errand: ok only when crafted item count rose.
    {
        prof::CraftErrandFacts cf;
        cf.inWorld = true;
        cf.alive = true;
        cf.phase = prof::CraftPhase::Casting;
        cf.tracksOutputItem = true;
        cf.targetRuns = 2;
        cf.completedRuns = 0;
        cf.isCasting = true;
        cf.phaseElapsedMs = 1000;
        auto cWait = prof::StepCraftErrand(cf);
        CHECK(cWait.action == prof::CraftErrandAction::Wait && std::strcmp(cWait.stateLabel, "pending:casting") == 0, "crafting mid-cast reports pending:casting");

        prof::CraftErrandFacts cNoItem = cf;
        cNoItem.isCasting = false;
        cNoItem.phaseElapsedMs = 1200;
        auto cFail = prof::StepCraftErrand(cNoItem);
        CHECK(cFail.action == prof::CraftErrandAction::FinishFail && std::strcmp(cFail.failCode, "craft-no-item") == 0, "craft cast ended without item -> craft-no-item");

        prof::CraftErrandFacts cRun1 = cf;
        cRun1.isCasting = false;
        cRun1.itemsGainedThisRun = 1;
        auto cNext = prof::StepCraftErrand(cRun1);
        CHECK(cNext.action == prof::CraftErrandAction::CastNext && std::strcmp(cNext.stateLabel, "pending:casting") == 0, "first of 2 crafts -> CastNext");

        prof::CraftErrandFacts cRun2 = cRun1;
        cRun2.completedRuns = 1;
        auto cDone = prof::StepCraftErrand(cRun2);
        CHECK(cDone.action == prof::CraftErrandAction::FinishOk && std::strcmp(cDone.stateLabel, "ok") == 0, "final craft with item gain -> FinishOk");
    }

    std::printf("PASS: test_w7_professions (%d checks, %d failed)\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
