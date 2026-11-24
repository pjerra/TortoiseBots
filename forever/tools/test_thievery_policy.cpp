// Standalone pure test for the thievery gates:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_thievery_policy.cpp -o /tmp/test_thievery_policy && /tmp/test_thievery_policy
#include "ai/playerbot/policy/profession/ThieveryPolicy.h"

#include <cstdio>
#include <cstring>
#include <string>
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

static prof::LockboxFacts Openable()
{
    prof::LockboxFacts f;
    f.hasLock = true;
    f.locked = true;
    f.hasLoot = true;
    f.lockpickingLock = true;
    f.hasPickLockSpell = true;
    f.hasLockpicking = true;
    f.botSkill = 150;
    f.requiredSkill = 100;
    return f;
}

int main()
{
    // 1. Lockbox gate: a rogue with enough Lockpicking opens the box.
    {
        prof::LockboxGate const gate = prof::EvaluateLockbox(Openable());
        CHECK(gate == prof::LockboxGate::Ok, "skill 150 vs req 100 is ok");
        CHECK(prof::LockboxOpenable(gate), "ok is openable");
        CHECK(!prof::LockboxNeedsARogue(gate), "ok needs no other rogue");
        CHECK(!prof::LockboxNeedsABetterRogue(gate), "ok needs no better rogue");
        CHECK(std::strcmp(prof::LockboxGateToken(gate), "ok") == 0, "ok token");
    }

    // 2. Low skill: the rogue mails it to a better one, never opens it.
    {
        prof::LockboxFacts f = Openable();
        f.botSkill = 99;
        prof::LockboxGate const gate = prof::EvaluateLockbox(f);
        CHECK(gate == prof::LockboxGate::LowSkill, "skill 99 vs req 100 is low");
        CHECK(!prof::LockboxOpenable(gate), "low skill is not openable");
        CHECK(prof::LockboxNeedsABetterRogue(gate), "low skill needs a better rogue");
        CHECK(!prof::LockboxNeedsARogue(gate), "low skill is not a no-rogue box");
        CHECK(std::strcmp(prof::LockboxGateToken(gate), "low-skill") == 0, "low-skill token");
    }

    // 3. A non-rogue (or a rogue without Pick Lock) mails it: no spell, then no skill.
    {
        prof::LockboxFacts f = Openable();
        f.hasPickLockSpell = false;
        prof::LockboxGate const noSpell = prof::EvaluateLockbox(f);
        CHECK(noSpell == prof::LockboxGate::NoPickLockSpell, "no Pick Lock spell");
        CHECK(prof::LockboxNeedsARogue(noSpell), "no spell needs a rogue");

        f = Openable();
        f.hasLockpicking = false;
        f.botSkill = 0;
        prof::LockboxGate const noSkill = prof::EvaluateLockbox(f);
        CHECK(noSkill == prof::LockboxGate::NoSkill, "no Lockpicking skill");
        CHECK(prof::LockboxNeedsARogue(noSkill), "no skill needs a rogue");
    }

    // 4. Shapes that are not this service's business.
    {
        prof::LockboxFacts f = Openable();
        f.hasLock = false;
        CHECK(prof::EvaluateLockbox(f) == prof::LockboxGate::NoLock, "no lock id");

        f = Openable();
        f.hasLoot = false;
        CHECK(prof::EvaluateLockbox(f) == prof::LockboxGate::NotAContainer, "locked non-container");

        f = Openable();
        f.lockpickingLock = false;
        CHECK(prof::EvaluateLockbox(f) == prof::LockboxGate::NotLockpicking, "key/spell lock");

        f = Openable();
        f.locked = false;
        prof::LockboxGate const unlocked = prof::EvaluateLockbox(f);
        CHECK(unlocked == prof::LockboxGate::Unlocked, "unlocked container");
        CHECK(prof::LockboxOpenable(unlocked), "unlocked container is still looted");
        CHECK(!prof::LockboxNeedsARogue(unlocked), "unlocked box needs no rogue");
    }

    // 5. Pickpocket bot gate: only a rogue with both spells, alive, out of combat,
    //    and not owned by another errand.
    {
        prof::PickpocketBotFacts f;
        f.isRogue = true;
        f.hasStealthSpell = true;
        f.hasPickPocketSpell = true;
        f.alive = true;
        f.inWorld = true;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::Ready, "ready rogue");

        f.isRogue = false;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::NotRogue, "non-rogue");
        f.isRogue = true;

        f.hasPickPocketSpell = false;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::NoSpell, "no Pick Pocket");
        f.hasPickPocketSpell = true;

        f.inCombat = true;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::InCombat, "in combat");
        f.inCombat = false;

        f.busy = true;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::Busy, "busy");
        f.busy = false;

        f.alive = false;
        CHECK(prof::EvaluatePickpocketBot(f) == prof::PickpocketBotGate::NotAlive, "dead");
    }

    // 6. Pickpocket target gate.
    {
        prof::PickpocketTargetFacts f;
        f.alive = true;
        f.humanoid = true;
        f.canHaveLoot = true;
        f.hasPockets = true;
        f.pocketsReady = true;
        f.distanceYd = 4.0f;
        f.maxDistanceYd = 20.0f;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::Ok, "live humanoid with pockets");

        f.inCombat = true;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::InCombat, "engaged target");
        f.inCombat = false;

        f.humanoid = false;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::NotHumanoid, "beast");
        f.humanoid = true;

        f.hasPockets = false;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::NoPockets, "no pickpocket loot id");
        f.hasPockets = true;

        f.pocketsReady = false;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::PocketsLooted, "refill timer running");
        f.pocketsReady = true;

        f.distanceYd = 25.0f;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::TooFar, "out of the search range");

        f.distanceYd = 4.0f;
        f.alive = false;
        CHECK(prof::EvaluatePickpocketTarget(f) == prof::PickpocketTargetGate::Dead, "corpse");
    }

    // 7. Mail recipient: the best Lockpicking, online breaks a tie, the lowest
    //    guid breaks the rest. Non-bots, non-rogues, the sender and rogues
    //    below the requirement are never picked.
    {
        std::vector<prof::LockboxRogueCandidate> candidates(6);
        candidates[0].guidLow = 10;
        candidates[0].name = "Realplayer";
        candidates[0].isBot = false;
        candidates[0].isRogue = true;
        candidates[0].online = true;
        candidates[0].lockpickingSkill = 300;

        candidates[1].guidLow = 20;
        candidates[1].name = "Warriorbot";
        candidates[1].isBot = true;
        candidates[1].isRogue = false;
        candidates[1].online = true;
        candidates[1].lockpickingSkill = 300;

        candidates[2].guidLow = 30;
        candidates[2].name = "Sender";
        candidates[2].isBot = true;
        candidates[2].isRogue = true;
        candidates[2].online = true;
        candidates[2].lockpickingSkill = 300;

        candidates[3].guidLow = 40;
        candidates[3].name = "Weakrogue";
        candidates[3].isBot = true;
        candidates[3].isRogue = true;
        candidates[3].online = true;
        candidates[3].lockpickingSkill = 75;

        candidates[4].guidLow = 50;
        candidates[4].name = "Offlinehealthy";
        candidates[4].isBot = true;
        candidates[4].isRogue = true;
        candidates[4].online = false;
        candidates[4].lockpickingSkill = 150;

        candidates[5].guidLow = 60;
        candidates[5].name = "Onlinehealthy";
        candidates[5].isBot = true;
        candidates[5].isRogue = true;
        candidates[5].online = true;
        candidates[5].lockpickingSkill = 150;

        prof::LockboxRogueCandidate const* best = prof::PickLockboxRogue(30, 100, candidates);
        CHECK(best != nullptr, "a rogue is picked");
        CHECK(best && best->guidLow == 60, "equal skill: the online rogue wins");
        CHECK(best && best->name == "Onlinehealthy", "name follows the pick");

        // A tighter requirement rules the healthy pair out.
        best = prof::PickLockboxRogue(30, 160, candidates);
        CHECK(best == nullptr, "nobody can open a 160 lock");

        // With the online healthy rogue gone, the offline one is used.
        candidates[5].lockpickingSkill = 0;
        best = prof::PickLockboxRogue(30, 100, candidates);
        CHECK(best && best->guidLow == 50, "offline rogue is served");
    }

    // 8. Postage and the return coins: the box's own coins pay it, never the
    //    other way round, and the service never sends more than the box held.
    {
        CHECK(prof::MailPostageCopper(0) == 30, "a letter costs 30");
        CHECK(prof::MailPostageCopper(1) == 30, "one attachment costs 30");
        CHECK(prof::MailPostageCopper(3) == 90, "three attachments cost 90");
        CHECK(prof::LockboxReturnCopper(1000, 30) == 970, "coins minus postage");
        CHECK(prof::LockboxReturnCopper(30, 30) == 0, "exactly the postage leaves nothing");
        CHECK(prof::LockboxReturnCopper(10, 30) == 0, "a short box returns no coins");
        CHECK(prof::LockboxReturnCopper(0, 30) == 0, "an empty box returns no coins");
        CHECK(prof::LockboxReturnCopper(500, 30) < 500, "the sender always pays the postage");
    }

    // 9. Service subject: trimmed, case-insensitive, nothing else matches.
    {
        CHECK(prof::IsLockboxServiceSubject("lockbox"), "plain subject");
        CHECK(prof::IsLockboxServiceSubject("  LockBox \t"), "padded and mixed case");
        CHECK(!prof::IsLockboxServiceSubject("lockbox opened: Strong Iron Lockbox"), "a result mail is not intake");
        CHECK(!prof::IsLockboxServiceSubject("disenchant"), "another service");
        CHECK(!prof::IsLockboxServiceSubject(""), "empty subject");
        CHECK(std::strcmp(prof::kLockboxServiceSubject, "lockbox") == 0, "subject constant");
    }

    // 10. Ids are the classic ones the core spells/scripts carry.
    {
        CHECK(prof::kPickLockSpellId == 1804, "Pick Lock is 1804");
        CHECK(prof::kStealthSpellId == 1784, "Stealth is 1784");
        CHECK(prof::kPickPocketSpellId == 921, "Pick Pocket is 921");
        CHECK(prof::kLockpickingSkillId == 633, "Lockpicking skill line is 633");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
