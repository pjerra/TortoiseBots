// Pure unit tests for free-bot group/elite quest grouping & disbanding (TbHostGroupQuestPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_group_quest.cpp -o /tmp/test_group_quest && /tmp/test_group_quest

#include "forever/src/host/TbHostGroupQuestPure.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using namespace tbhost::group_quest;
    int passed = 0;

    // 1. Quest classification: Group (1) and SuggestedPlayers 2..5 are group/elite; Raids and solo quests are not.
    {
        assert(IsGroupOrEliteQuest(kQuestInfoGroup, 0));
        assert(IsGroupOrEliteQuest(0, 3));
        assert(!IsGroupOrEliteQuest(0, 1));
        assert(!IsGroupOrEliteQuest(kQuestInfoRaid, 5));
        assert(!IsGroupOrEliteQuest(0, 10));
        ++passed;
    }

    // 2. Two nearby free bots holding the same group quest form a party.
    {
        BotQuestCandidate a{ 101, 0, 100.0f, 100.0f, 10, true, false, false, { 176 } };
        BotQuestCandidate b{ 102, 0, 130.0f, 110.0f, 11, true, false, false, { 176 } };
        auto plans = PlanQuestGroups({ a, b });
        assert(plans.size() == 1);
        assert(plans[0].leaderGuidLow == 101);
        assert(plans[0].questId == 176);
        assert(plans[0].memberGuidLows.size() == 1 && plans[0].memberGuidLows[0] == 102);
        ++passed;
    }

    // 3. Bots too far apart, different maps, too large level gap, or different quests do NOT group.
    {
        BotQuestCandidate a{ 101, 0, 100.0f, 100.0f, 10, true, false, false, { 176 } };
        BotQuestCandidate farBot{ 102, 0, 500.0f, 100.0f, 10, true, false, false, { 176 } };
        BotQuestCandidate diffMap{ 103, 1, 100.0f, 100.0f, 10, true, false, false, { 176 } };
        BotQuestCandidate highLvl{ 104, 0, 110.0f, 100.0f, 20, true, false, false, { 176 } };
        BotQuestCandidate diffQuest{ 105, 0, 110.0f, 100.0f, 10, true, false, false, { 999 } };
        assert(PlanQuestGroups({ a, farBot, diffMap, highLvl, diffQuest }).empty());
        ++passed;
    }

    // 4. Cap at MAX_GROUP_SIZE (5).
    {
        std::vector<BotQuestCandidate> bots;
        for (uint32_t i = 1; i <= 7; ++i)
            bots.push_back({ i, 0, 100.0f + float(i), 100.0f, 10, true, false, false, { 176 } });
        auto plans = PlanQuestGroups(bots);
        assert(plans.size() == 2);
        assert(plans[0].memberGuidLows.size() == 4);
        assert(plans[1].memberGuidLows.size() == 1);
        ++passed;
    }

    // 5. Disband conditions: quest done, too few members, timeout, real player joined, or active keeps running.
    {
        char const* reason = nullptr;
        QuestGroupState active{ 176, 1000, 2, 2, false, false };
        assert(!ShouldDisbandQuestGroup(active, 5000, &reason));

        QuestGroupState done = active;
        done.membersStillIncomplete = 0;
        assert(ShouldDisbandQuestGroup(done, 5000, &reason));
        assert(std::string(reason) == "quest-done");

        QuestGroupState solo = active;
        solo.activeMemberCount = 1;
        assert(ShouldDisbandQuestGroup(solo, 5000, &reason));
        assert(std::string(reason) == "too-few-members");

        assert(ShouldDisbandQuestGroup(active, 1000 + kMaxGroupDurationMs, &reason));
        assert(std::string(reason) == "timeout");
        ++passed;
    }

    std::cout << "PASS: test_group_quest (" << passed << " checks, 0 failed)\n";
    return 0;
}
