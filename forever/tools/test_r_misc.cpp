// Pure unit tests for PR #97 review fixes (W2 mail Turtle compat, W7 gather order & timer, W6b gear swap cleanup & pre-filter).
// Build & run:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_r_misc.cpp -o /tmp/test_r_misc && /tmp/test_r_misc

#include "ai/playerbot/policy/item/GearSetPolicy.h"
#include "ai/playerbot/policy/profession/GatherNodePolicy.h"
#include "ai/playerbot/policy/town/TownErrandPolicy.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
int g_failed = 0;
int g_passed = 0;

void Check(bool cond, char const* label)
{
    if (cond)
    {
        ++g_passed;
    }
    else
    {
        ++g_failed;
        std::cout << "FAIL: " << label << "\n";
    }
}

TortoiseBots::prof::GatherNodeFacts MakeNodeFacts(std::uint32_t skillId, std::uint32_t reqSkill, std::uint32_t botSkill, bool hasSkill, float dist)
{
    TortoiseBots::prof::GatherNodeFacts f;
    f.exists = true;
    f.spawned = true;
    f.ready = true;
    f.sameZone = true;
    f.distanceYd = dist;
    f.maxDistanceYd = 150.0f;
    f.freeBagSlots = 4;
    f.hasRequiredTool = true;
    f.lockSkillId = skillId;
    f.reqSkill = reqSkill;
    f.botSkill = botSkill;
    f.hasSkill = hasSkill;
    return f;
}

ai::GearItemCandidate MakeGear(uint32_t id, uint32_t exp, uint8_t quality, uint16_t ilvl, uint8_t reqLvl, uint8_t invType, uint8_t itemClass, uint8_t subClass)
{
    ai::GearItemCandidate c;
    c.itemId = id;
    c.requiredExpansion = exp;
    c.allowableClass = 0xFFFFFFFFu;
    c.itemLevel = ilvl;
    c.requiredLevel = reqLvl;
    c.quality = quality;
    c.inventoryType = invType;
    c.itemClass = itemClass;
    c.subClass = subClass;
    return c;
}
} // namespace

int main()
{
    // Finding 11: Turtle keeps old text-only mail result (true) and does not abort on empty mailbox;
    // Forever keeps strict "no proof, no ok" rule (false when confirmedTakes == 0) and aborts on empty mailbox.
    Check(ai::MailTakeAfterResult(false, 0) == true, "mail-turtle-text-only-after-true");
    Check(ai::MailTakeAfterResult(true, 0) == false, "mail-forever-zero-takes-after-false");
    Check(ai::MailTakeAfterResult(true, 1) == true, "mail-forever-confirmed-take-after-true");
    Check(ai::MailProcessShouldAbortMissingMailbox(false, true) == false, "mail-turtle-empty-mailbox-continues");
    Check(ai::MailProcessShouldAbortMissingMailbox(true, true) == true, "mail-forever-empty-mailbox-aborts");

    // Finding 12a: Skill/readiness filter runs BEFORE danger check; danger check runs at most once on chosen node.
    {
        std::vector<TortoiseBots::prof::GatherCandidateInput> nodes = {
            { 101, 5.0f, MakeNodeFacts(186, 1, 0, false, 5.0f) },   // Mining node, bot has no Mining -> must NOT trigger danger check
            { 102, 10.0f, MakeNodeFacts(182, 100, 1, true, 10.0f) }, // Herb node, skill too low -> must NOT trigger danger check
            { 103, 15.0f, MakeNodeFacts(182, 1, 50, true, 15.0f) },  // Valid Herb node -> chosen, danger check runs ONCE
        };
        auto const sel = TortoiseBots::prof::SelectBestGatherNode(nodes, [](std::uint32_t) { return false; });
        Check(sel.found && sel.chosenId == 103, "gather-selects-valid-herb-node");
        Check(sel.dangerChecksRun == 1, "gather-danger-check-only-on-chosen-node");

        std::vector<TortoiseBots::prof::GatherCandidateInput> noneValid = {
            { 201, 5.0f, MakeNodeFacts(186, 1, 0, false, 5.0f) },
        };
        auto const selNone = TortoiseBots::prof::SelectBestGatherNode(noneValid, [](std::uint32_t) { return true; });
        Check(!selNone.found && selNone.dangerChecksRun == 0, "gather-zero-danger-checks-when-no-valid-node");
    }

    // Finding 12b: Free gather cooldown is per-bot (guid-keyed) and cleared on logout.
    {
        TortoiseBots::prof::FreeGatherCooldownTracker tracker;
        Check(!tracker.Advance(1001, 4000, 5000), "gather-timer-bot1-4000ms-not-ready");
        Check(!tracker.Advance(1002, 4000, 5000), "gather-timer-bot2-4000ms-independent");
        Check(tracker.Advance(1001, 1000, 5000), "gather-timer-bot1-5000ms-ready");
        Check(tracker.Advance(1002, 1000, 5000), "gather-timer-bot2-5000ms-ready");
        tracker.Advance(1001, 3000, 5000);
        tracker.Clear(1001);
        Check(!tracker.Advance(1001, 2500, 5000), "gather-timer-bot1-cleared-on-logout");
    }

    // Finding 13: Failed swap cleanup destroys only the exact created instance GUID, never by entry/count.
    {
        auto const cleanup = ai::DecideFailedSwapCleanup(99001ULL, 12587u);
        Check(cleanup.destroyByGuid && cleanup.destroyGuid == 99001ULL && cleanup.destroyEntryCount == 0,
              "gear-cleanup-destroys-by-created-guid-only");
        auto const noItem = ai::DecideFailedSwapCleanup(0ULL, 12587u);
        Check(!noItem.destroyByGuid && noItem.destroyEntryCount == 0,
              "gear-cleanup-noop-when-no-created-guid");
    }

    // Finding 13 (Low): Filter slot pool BEFORE scoring items.
    {
        std::vector<ai::GearItemCandidate> pool = {
            MakeGear(24000, 1, 3, 70, 60, 1, 4, 4), // TBC -> filtered out before scoring
            MakeGear(28000, 0, 3, 70, 60, 1, 4, 4), // >= 25000 -> filtered out before scoring
            MakeGear(12587, 0, 3, 63, 58, 1, 4, 4), // valid classic plate helm -> scored
            MakeGear(12588, 0, 3, 63, 58, 1, 4, 1), // cloth helm for warrior -> filtered out before scoring
        };
        auto filtered = ai::FilterCandidatesForSlot(pool, 1, "arms", 60, 0, 3, false, false);
        Check(filtered.size() == 1 && filtered[0].itemId == 12587, "gear-filter-before-scoring-size-1");
    }

    // Addendum 1: Gather/craft errand map cleared on logout so stale errand does not resume after relog.
    {
        TortoiseBots::prof::GatherErrandRegistry errands;
        errands.SetActive(1001, TortoiseBots::prof::GatherPhase::Walking);
        errands.Clear(1001);
        Check(!errands.HasActive(1001), "gather-errand-cleared-on-logout");
    }

    if (g_failed > 0)
    {
        std::cout << "FAIL: test_r_misc (" << g_passed << " passed, " << g_failed << " failed)\n";
        return 1;
    }
    std::cout << "PASS: test_r_misc (" << g_passed << " checks, 0 failed)\n";
    return 0;
}
