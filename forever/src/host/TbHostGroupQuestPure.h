#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tbhost
{
namespace group_quest
{

inline constexpr uint32_t kQuestInfoGroup = 1;
inline constexpr uint32_t kQuestInfoRaid = 62;
inline constexpr uint32_t kQuestInfoDungeon = 81;
inline constexpr uint32_t kQuestInfoRaid10 = 88;
inline constexpr uint32_t kQuestInfoRaid25 = 89;

inline constexpr float kMaxGroupDistYd = 120.0f;
inline constexpr int32_t kMaxLevelDiff = 5;
inline constexpr uint32_t kMaxPartySize = 5;
inline constexpr uint64_t kMaxGroupDurationMs = 15ULL * 60ULL * 1000ULL;

inline bool IsGroupOrEliteQuest(uint32_t questInfoId, uint32_t suggestedPlayers)
{
    if (questInfoId == kQuestInfoRaid || questInfoId == kQuestInfoRaid10 || questInfoId == kQuestInfoRaid25)
        return false;
    if (questInfoId == kQuestInfoGroup)
        return true;
    return suggestedPlayers > 1 && suggestedPlayers <= kMaxPartySize;
}

struct BotQuestCandidate
{
    uint32_t guidLow = 0;
    uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    uint8_t level = 0;
    bool alive = false;
    bool inCombat = false;
    bool inGroup = false;
    std::vector<uint32_t> incompleteGroupQuests;
};

struct QuestGroupPlan
{
    uint32_t leaderGuidLow = 0;
    std::vector<uint32_t> memberGuidLows;
    uint32_t questId = 0;
};

inline float DistSq2D(BotQuestCandidate const& a, BotQuestCandidate const& b)
{
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline bool SharesQuest(BotQuestCandidate const& b, uint32_t questId)
{
    return std::find(b.incompleteGroupQuests.begin(), b.incompleteGroupQuests.end(), questId) != b.incompleteGroupQuests.end();
}

// Form parties of nearby free bots (same map, <= 120 yd, <= 5 level diff, max 5 members)
// holding the same incomplete group/elite quest.
inline std::vector<QuestGroupPlan> PlanQuestGroups(std::vector<BotQuestCandidate> const& candidates)
{
    std::vector<QuestGroupPlan> plans;
    std::vector<bool> assigned(candidates.size(), false);
    float const maxDistSq = kMaxGroupDistYd * kMaxGroupDistYd;

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (assigned[i])
            continue;
        BotQuestCandidate const& leader = candidates[i];
        if (!leader.guidLow || !leader.alive || leader.inCombat || leader.inGroup || leader.incompleteGroupQuests.empty())
            continue;

        for (uint32_t qid : leader.incompleteGroupQuests)
        {
            if (!qid)
                continue;
            QuestGroupPlan plan;
            plan.leaderGuidLow = leader.guidLow;
            plan.questId = qid;
            std::vector<size_t> pickedIndices;

            for (size_t j = i + 1; j < candidates.size(); ++j)
            {
                if (assigned[j])
                    continue;
                BotQuestCandidate const& m = candidates[j];
                if (!m.guidLow || !m.alive || m.inCombat || m.inGroup)
                    continue;
                if (m.mapId != leader.mapId)
                    continue;
                int32_t const lvlDiff = std::abs(int32_t(m.level) - int32_t(leader.level));
                if (lvlDiff > kMaxLevelDiff)
                    continue;
                if (DistSq2D(leader, m) > maxDistSq)
                    continue;
                if (!SharesQuest(m, qid))
                    continue;

                plan.memberGuidLows.push_back(m.guidLow);
                pickedIndices.push_back(j);
                if (1 + plan.memberGuidLows.size() >= kMaxPartySize)
                    break;
            }

            if (!plan.memberGuidLows.empty())
            {
                assigned[i] = true;
                for (size_t idx : pickedIndices)
                    assigned[idx] = true;
                plans.push_back(std::move(plan));
                break;
            }
        }
    }
    return plans;
}

struct QuestGroupState
{
    uint32_t questId = 0;
    uint64_t formedAtMs = 0;
    uint32_t activeMemberCount = 0;
    uint32_t membersStillIncomplete = 0;
    bool hasRealPlayer = false;
    bool anyMemberLostFree = false;
};

inline bool ShouldDisbandQuestGroup(QuestGroupState const& s, uint64_t nowMs, char const** outReason = nullptr)
{
    auto setReason = [&](char const* r)
    {
        if (outReason)
            *outReason = r;
    };
    if (s.hasRealPlayer)
    {
        setReason("real-player-joined");
        return true;
    }
    if (s.anyMemberLostFree)
    {
        setReason("member-not-free");
        return true;
    }
    if (s.activeMemberCount < 2)
    {
        setReason("too-few-members");
        return true;
    }
    if (s.membersStillIncomplete == 0)
    {
        setReason("quest-done");
        return true;
    }
    if (nowMs >= s.formedAtMs && (nowMs - s.formedAtMs) >= kMaxGroupDurationMs)
    {
        setReason("timeout");
        return true;
    }
    return false;
}

} // namespace group_quest
} // namespace tbhost
