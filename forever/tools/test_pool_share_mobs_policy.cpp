// Standalone test of #24's pool-share rule (ai/playerbot/policy/world/PoolShareMobsPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_pool_share_mobs_policy.cpp -o /tmp/test_pool_share_mobs_policy && /tmp/test_pool_share_mobs_policy
//
// The scene is an Elwynn camp: one real player working Kobold Camp Cleanup (entry 6 is its MONSTER objective), one quest item
// dropper, forward and side spots, and the bots' own quest site. The rule only ever answers "may this pool bot start this fight"
// and "who is ahead of me at this quest spot"; self-defence is the attackers loop of FindTargetForGrinding and is not
// represented here.
#include "ai/playerbot/policy/world/PoolShareMobsPolicy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
w::ShareMobsConfig Defaults()
{
    w::ShareMobsConfig config;
    config.enabled = true;
    return config;
}

w::PlayerRow Player(std::uint64_t guid, float x, float y)
{
    w::PlayerRow row;
    row.guid = guid;
    row.mapId = 0;
    row.instanceId = 0;
    row.x = x;
    row.y = y;
    row.z = 0.0f;
    return row;
}

w::MobPoint Mob(float x, float y, std::uint32_t entry = 299)
{
    w::MobPoint mob;
    mob.mapId = 0;
    mob.instanceId = 0;
    mob.x = x;
    mob.y = y;
    mob.z = 0.0f;
    mob.entry = entry;
    return mob;
}

w::BotFacts PoolBot(w::ShareMobsConfig const& config)
{
    w::BotFacts facts;
    facts.switchOn = config.enabled;
    facts.poolBot = true;
    facts.freeBot = true;
    return facts;
}

bool Holds(std::vector<std::uint32_t> const& ids, std::uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}

int main()
{
    // ---- the config and the gate ---------------------------------------------------------------------------------------
    w::ShareMobsConfig const config = Defaults();
    CHECK(!w::ShareMobsConfig{}.enabled);                      // the switch is OFF until the live rows prove it
    CHECK(w::ShareMobsConfig{}.nearYd == 30.0f);
    CHECK(w::ShareMobsConfig{}.farYd == 60.0f);
    CHECK(w::ShareMobsConfig{}.questSpotCap == 3);
    CHECK(w::ShareMobsConfig{}.giveUpSeconds == 600);

    // DisabledNeverApplies: the switch alone turns every decision off.
    {
        w::ShareMobsConfig off = config;
        off.enabled = false;
        w::BotFacts facts = PoolBot(off);
        CHECK(!w::ShareMobsApplies(facts));
        CHECK(w::JudgeMob(off, Player(1, 0.0f, 0.0f), 0, Mob(5.0f, 0.0f), false) == w::MobVerdict::None);
        std::vector<w::PlayerRow> rows{ Player(1, 0.0f, 0.0f) };
        CHECK(!w::SpotWorkedByPlayer(off, rows, 0, 0, 0.0f, 0.0f, 0.0f));
    }

    CHECK(w::ShareMobsApplies(PoolBot(config)));

    // OwnedHiredGuildGroupedNeverApply: each of the four kinds of bot the rule never touches, one at a time.
    {
        w::BotFacts hired = PoolBot(config);
        hired.hasRealPlayerMaster = true;
        CHECK(!w::ShareMobsApplies(hired));
        w::BotFacts owned = PoolBot(config);
        owned.owned = true;
        CHECK(!w::ShareMobsApplies(owned));
        w::BotFacts guildOrder = PoolBot(config);
        guildOrder.openGuildOrder = true;
        CHECK(!w::ShareMobsApplies(guildOrder));
        w::BotFacts grouped = PoolBot(config);
        grouped.groupHasRealPlayer = true;
        CHECK(!w::ShareMobsApplies(grouped));
        w::BotFacts notPool = PoolBot(config);
        notPool.poolBot = false;
        CHECK(!w::ShareMobsApplies(notPool));
        w::BotFacts notFree = PoolBot(config);
        notFree.freeBot = false;
        CHECK(!w::ShareMobsApplies(notFree));
    }

    // InstanceNeverApplies: dungeons, raids, battlegrounds and arenas.
    {
        w::BotFacts instance = PoolBot(config);
        instance.mapInstanceable = true;
        CHECK(!w::ShareMobsApplies(instance));
    }

    // ---- the row's quest needs ------------------------------------------------------------------------------------------
    // Only an objective a kill or a drop can serve, and only while it is still open, enters the lists.
    {
        w::PlayerRow row;
        w::AddQuestObjective(row, w::QuestNeedKind::Monster, 6, 10, false);
        w::AddQuestObjective(row, w::QuestNeedKind::Item, 12345, 4, false);
        CHECK(Holds(row.monsterEntries, 6));
        CHECK(Holds(row.itemIds, 12345));

        // CompleteObjectiveNotKept: an objective the player has already finished never keeps a bot away from the mob.
        w::AddQuestObjective(row, w::QuestNeedKind::Monster, 400, 10, true);
        w::AddQuestObjective(row, w::QuestNeedKind::Item, 777, 1, true);
        CHECK(!Holds(row.monsterEntries, 400));
        CHECK(!Holds(row.itemIds, 777));

        // OtherObjectivesNotKept: talk-to, gameobject and currency objectives, a zero count and an empty object id add
        // nothing either.
        w::AddQuestObjective(row, w::QuestNeedKind::None, 6, 10, false);
        w::AddQuestObjective(row, w::QuestNeedKind::Monster, 6, 0, false);
        w::AddQuestObjective(row, w::QuestNeedKind::Item, 0, 3, false);
        CHECK(row.monsterEntries.size() == 1);
        CHECK(row.itemIds.size() == 1);
    }

    // ---- the mob rule ---------------------------------------------------------------------------------------------------
    // A quest player at the camp corner: entry 6 is a kobold, item 12345 is the candle.
    w::PlayerRow questPlayer = Player(1000, 100.0f, 100.0f);
    questPlayer.monsterEntries = { 6, 299, 400 };
    questPlayer.itemIds = { 12345 };
    CHECK(questPlayer.needsMonster(6));
    CHECK(questPlayer.needsMonster(400));
    CHECK(!questPlayer.needsMonster(7));
    CHECK(questPlayer.needsItem(12345));
    CHECK(!questPlayer.needsItem(12346));
    std::uint64_t const otherGroup = 0x777;

    // UntappedMobNextToPlayerIsSkipped: any mob, any faction, 10 yd away and no quest need at all.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(110.0f, 100.0f, 1), false) == w::MobVerdict::NearPlayer);
    // The near ring includes its own edge.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(130.0f, 100.0f, 1), false) == w::MobVerdict::NearPlayer);

    // MobBeyondNearRadiusIsPicked: a plain mob in the ring between the radii.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(145.0f, 100.0f, 1), false) == w::MobVerdict::None);

    // QuestMobWithinFarRadiusIsSkipped: the entry the player's open quest names, between the radii.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(150.0f, 100.0f, 299), false) == w::MobVerdict::QuestNearPlayer);
    // ... and at the far edge exactly.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(160.0f, 100.0f, 299), false) == w::MobVerdict::QuestNearPlayer);

    // QuestItemDropperWithinFarRadiusIsSkipped: the entry is unknown to the list, its loot is not.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(140.0f, 100.0f, 77), true) == w::MobVerdict::QuestNearPlayer);

    // QuestMobBeyondFarRadiusIsPicked: one yard past the far ring is another fight.
    CHECK(w::JudgeMob(config, questPlayer, 0, Mob(160.5f, 100.0f, 299), true) == w::MobVerdict::None);

    // GroupmatePlayerIgnored: a real player of the bot's own party never makes it step aside, not even at 5 yd.
    w::PlayerRow mate = questPlayer;
    mate.groupGuid = otherGroup;
    CHECK(w::JudgeMob(config, mate, otherGroup, Mob(105.0f, 100.0f, 1), true) == w::MobVerdict::None);
    CHECK(w::JudgeMob(config, mate, 0, Mob(105.0f, 100.0f, 1), true) == w::MobVerdict::NearPlayer);

    // OtherMapIgnored: the same camp, another continent (and another instance of the same map).
    {
        w::MobPoint elsewhere = Mob(110.0f, 100.0f, 1);
        elsewhere.mapId = 1;
        CHECK(w::JudgeMob(config, questPlayer, 0, elsewhere, true) == w::MobVerdict::None);
        w::PlayerRow otherInstance = questPlayer;
        otherInstance.instanceId = 5;
        CHECK(w::JudgeMob(config, otherInstance, 0, Mob(110.0f, 100.0f, 1), true) == w::MobVerdict::None);
    }

    // Height counts as distance: a mob 50 yd straight down is not next to a player at ground level in the far ring sense.
    {
        w::MobPoint below = Mob(100.0f, 100.0f, 1);
        below.z = -50.0f;
        CHECK(w::JudgeMob(config, questPlayer, 0, below, false) == w::MobVerdict::None);
        below.z = -10.0f;
        CHECK(w::JudgeMob(config, questPlayer, 0, below, false) == w::MobVerdict::NearPlayer);
    }

    // ---- the grind spot -------------------------------------------------------------------------------------------------
    // SpotNearPlayerDropped: the camp the player stands in, and nothing else.
    {
        std::vector<w::PlayerRow> rows{ questPlayer };
        CHECK(w::SpotWorkedByPlayer(config, rows, 0, 0, 120.0f, 100.0f, 0.0f));
        CHECK(!w::SpotWorkedByPlayer(config, rows, 0, 0, 200.0f, 100.0f, 0.0f));
        CHECK(!w::SpotWorkedByPlayer(config, rows, 1, 0, 120.0f, 100.0f, 0.0f));       // another map
        CHECK(!w::SpotWorkedByPlayer(config, rows, 0, 3, 120.0f, 100.0f, 0.0f));       // another instance
        std::vector<w::PlayerRow> none;
        CHECK(!w::SpotWorkedByPlayer(config, none, 0, 0, 120.0f, 100.0f, 0.0f));
    }

    // ---- the quest spot cap ---------------------------------------------------------------------------------------------
    // CapOrdersRealCompanionPoolById: a real player, then companions, then pool bots, each by guid.
    {
        std::vector<w::Worker> workers{
            { w::WorkerRank::PoolBot, 9 },
            { w::WorkerRank::PoolBot, 5 },
            { w::WorkerRank::Companion, 3 },
            { w::WorkerRank::RealPlayer, 100 },
        };
        w::SortWorkers(workers);
        CHECK(workers[0].rank == w::WorkerRank::RealPlayer && workers[0].guid == 100);
        CHECK(workers[1].rank == w::WorkerRank::Companion && workers[1].guid == 3);
        CHECK(workers[2].rank == w::WorkerRank::PoolBot && workers[2].guid == 5);
        CHECK(workers[3].rank == w::WorkerRank::PoolBot && workers[3].guid == 9);

        // Room for three: the second pool bot (guid 9) is the one past the cap and gives the objective up.
        CHECK(!w::PastQuestSpotCap(workers, 3, 100));
        CHECK(!w::PastQuestSpotCap(workers, 3, 3));
        CHECK(!w::PastQuestSpotCap(workers, 3, 5));
        CHECK(w::PastQuestSpotCap(workers, 3, 9));
        // A cap of one at a site nothing else works: the pool bot still works it.
        std::vector<w::Worker> alone{ { w::WorkerRank::PoolBot, 9 } };
        CHECK(!w::PastQuestSpotCap(alone, 1, 9));
        CHECK(!w::PastQuestSpotCap(alone, 1, 10));       // not at the site at all: never past the cap
    }

    // CapOrderIgnoresInputOrder: the same people in any order answer the same.
    {
        std::vector<w::Worker> ordered{
            { w::WorkerRank::RealPlayer, 100 }, { w::WorkerRank::Companion, 3 },
            { w::WorkerRank::PoolBot, 5 }, { w::WorkerRank::PoolBot, 9 },
        };
        std::vector<w::Worker> shuffled{
            { w::WorkerRank::PoolBot, 9 }, { w::WorkerRank::PoolBot, 5 },
            { w::WorkerRank::RealPlayer, 100 }, { w::WorkerRank::Companion, 3 },
        };
        std::vector<w::Worker> reversed(ordered.rbegin(), ordered.rend());
        for (std::uint64_t self : { std::uint64_t(100), std::uint64_t(3), std::uint64_t(5), std::uint64_t(9) })
        {
            bool const a = w::PastQuestSpotCap(ordered, 3, self);
            CHECK(a == w::PastQuestSpotCap(shuffled, 3, self));
            CHECK(a == w::PastQuestSpotCap(reversed, 3, self));
        }
        // Two pool bots and a companion, cap 2: the pool bot with the higher guid loses, whichever way the list came in.
        std::vector<w::Worker> pairA{ { w::WorkerRank::Companion, 1 }, { w::WorkerRank::PoolBot, 2 }, { w::WorkerRank::PoolBot, 3 } };
        std::vector<w::Worker> pairB{ { w::WorkerRank::PoolBot, 3 }, { w::WorkerRank::PoolBot, 2 }, { w::WorkerRank::Companion, 1 } };
        CHECK(!w::PastQuestSpotCap(pairA, 2, 2));
        CHECK(!w::PastQuestSpotCap(pairB, 2, 2));
        CHECK(w::PastQuestSpotCap(pairA, 2, 3));
        CHECK(w::PastQuestSpotCap(pairB, 2, 3));
    }

    // ---- the give-up book -----------------------------------------------------------------------------------------------
    // GiveUpExpiresAfterTenMinutes: the objective is off for ten minutes, then it may be picked again.
    {
        std::uint64_t const t0 = 1'000'000;
        w::GiveUpBook book;
        CHECK(!book.IsGivenUp(7, 55, t0));
        book.GiveUp(7, 55, t0, 600);
        CHECK(book.Size() == 1);
        CHECK(book.IsGivenUp(7, 55, t0));
        CHECK(book.IsGivenUp(7, 55, t0 + 599'000));
        CHECK(!book.IsGivenUp(7, 55, t0 + 600'000));      // ten minutes later exactly: expired
        CHECK(!book.IsGivenUp(7, 56, t0 + 1000));         // another objective of the same quest is untouched
        CHECK(!book.IsGivenUp(8, 55, t0 + 1000));         // another quest's same-numbered objective too
        // Booked again: the time restarts, one entry only.
        book.GiveUp(7, 55, t0 + 590'000, 600);
        CHECK(book.Size() == 1);
        CHECK(book.IsGivenUp(7, 55, t0 + 1'150'000));
        CHECK(!book.IsGivenUp(7, 55, t0 + 1'190'000 + 1000));
        // Two objectives of one quest, then the logout drops the whole book.
        book.GiveUp(7, 56, t0, 60);
        CHECK(book.Size() == 2);
        CHECK(book.IsGivenUp(7, 56, t0 + 59'000));
        CHECK(!book.IsGivenUp(7, 56, t0 + 60'000));
        book.Clear();
        CHECK(book.Size() == 0);
        CHECK(!book.IsGivenUp(7, 55, t0));
    }

    // ---- the clamps -----------------------------------------------------------------------------------------------------
    // ConfigClamps: every key pinned to its range, the far ring never tighter than the near one.
    {
        w::ShareMobsConfig low;
        low.enabled = true;
        low.nearYd = 1.0f;          // 5-100
        low.farYd = 10.0f;          // 5-150: in range above the clamped near radius
        low.questSpotCap = 0;       // 1-10
        low.giveUpSeconds = 1;      // 60-3600
        w::ShareMobsConfig const clampedLow = w::ClampShareMobsConfig(low);
        CHECK(clampedLow.enabled);
        CHECK(clampedLow.nearYd == 5.0f);
        CHECK(clampedLow.farYd == 10.0f);
        CHECK(clampedLow.questSpotCap == 1);
        CHECK(clampedLow.giveUpSeconds == 60);

        w::ShareMobsConfig high;
        high.enabled = true;
        high.nearYd = 9999.0f;      // 100
        high.farYd = 9999.0f;       // 150
        high.questSpotCap = 99;     // 10
        high.giveUpSeconds = 99'999; // 3600
        w::ShareMobsConfig const clampedHigh = w::ClampShareMobsConfig(high);
        CHECK(clampedHigh.nearYd == 100.0f);
        CHECK(clampedHigh.farYd == 150.0f);
        CHECK(clampedHigh.questSpotCap == 10);
        CHECK(clampedHigh.giveUpSeconds == 3600);

        // A far ring tighter than the near one reads as the near one; in range values are left alone.
        w::ShareMobsConfig crossed;
        crossed.enabled = true;
        crossed.nearYd = 40.0f;
        crossed.farYd = 35.0f;
        w::ShareMobsConfig const clampedCrossed = w::ClampShareMobsConfig(crossed);
        CHECK(clampedCrossed.nearYd == 40.0f);
        CHECK(clampedCrossed.farYd == 40.0f);
        w::ShareMobsConfig const inRange = w::ClampShareMobsConfig(config);
        CHECK(inRange.nearYd == 30.0f && inRange.farYd == 60.0f && inRange.questSpotCap == 3 && inRange.giveUpSeconds == 600);
    }

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
