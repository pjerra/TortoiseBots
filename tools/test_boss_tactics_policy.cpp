// Standalone check for ai/playerbot/BossTacticsPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_tactics_policy.cpp -o /tmp/test_boss_tactics_policy && /tmp/test_boss_tactics_policy
#include "../ai/playerbot/BossTacticsPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static BossTacticRow Row(std::uint32_t map, std::uint32_t boss, BossTacticKind kind, std::uint32_t target = 0,
                         std::uint32_t spell = 0, std::uint8_t priority = 0, float param = 0.0f)
{
    BossTacticRow row;
    row.map = map;
    row.bossEntry = boss;
    row.kind = kind;
    row.targetEntry = target;
    row.spellId = spell;
    row.priority = priority;
    row.param = param;
    return row;
}

static ActiveBossRows Active(const std::vector<BossTacticRow>& rows)
{
    ActiveBossRows out;
    for (const BossTacticRow& row : rows)
        out.push_back(&row);
    return out;
}

static TacticCandidate Cand(std::uint32_t entry, float distance, bool pickable = true)
{
    TacticCandidate c;
    c.entry = entry;
    c.distance = distance;
    c.pickable = pickable;
    return c;
}

static CastDangerZone Zone(float x, float y, float z, float radius)
{
    CastDangerZone zone;
    zone.x = x;
    zone.y = y;
    zone.z = z;
    zone.radius = radius;
    return zone;
}

int main()
{
    std::cout << "Starting TortoiseBots boss tactics policy tests...\n";

    // Kind text: exactly the four lowercase spellings the table holds.
    CHECK(ParseBossTacticKind("interrupt") == BossTacticKind::Interrupt);
    CHECK(ParseBossTacticKind("kill_first") == BossTacticKind::KillFirst);
    CHECK(ParseBossTacticKind("behind") == BossTacticKind::Behind);
    CHECK(ParseBossTacticKind("air_target") == BossTacticKind::AirTarget);
    CHECK(ParseBossTacticKind("Interrupt") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("dispel") == BossTacticKind::None);

    // Loader: nothing is visible before the seal, and the first row of a key wins.
    {
        BossTacticTable table;
        CHECK(table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006)));
        CHECK(table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 15117)));
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006, 9)));  // same key
        CHECK(table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, 10.0f)));
        CHECK(table.Size() == 3);
        CHECK(!table.HasMap(70));                  // not sealed yet
        CHECK(table.Rows(70, 6910) == nullptr);
        table.Seal();
        CHECK(table.Sealed());
        CHECK(!table.Add(Row(70, 7023, BossTacticKind::KillFirst, 7209, 0, 1)));  // refused after the seal
        CHECK(table.Size() == 3);
        CHECK(table.HasMap(70));
        CHECK(!table.HasMap(1));
        const std::vector<BossTacticRow>* rows = table.Rows(70, 6910);
        CHECK(rows && rows->size() == 2);
        CHECK((*rows)[0].spellId == 16006);
        CHECK(table.Rows(70, 6911) == nullptr);
        CHECK(table.Rows(71, 6910) == nullptr);    // same entry on another map
    }

    // Loader: unusable rows are refused.
    {
        BossTacticTable table;
        CHECK(!table.Add(Row(0, 6910, BossTacticKind::Interrupt, 0, 16006)));          // no map
        CHECK(!table.Add(Row(70, 0, BossTacticKind::Interrupt, 0, 16006)));            // no boss
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::None, 0, 16006)));              // unknown kind
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 0)));             // interrupt without a spell
        CHECK(!table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, 0.0f)));    // behind without a radius
        CHECK(!table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, -3.0f)));
        CHECK(!table.Add(Row(249, 10184, BossTacticKind::AirTarget, 0, 0)));           // air_target without an aura
        CHECK(!table.Add(Row(249, 10184, BossTacticKind::AirTarget, 10185, 17131)));   // air_target with a target: never looked up
        CHECK(table.Add(Row(249, 10184, BossTacticKind::AirTarget, 0, 17131)));        // the aura marks the phase
        CHECK(!table.Add(Row(249, 10184, BossTacticKind::AirTarget, 0, 17131)));       // same key
        CHECK(table.Size() == 1);
        table.Seal();
        CHECK(table.HasMap(249));
        CHECK(!table.HasMap(70));  // an empty sealed table has no map: the tactics stay off
    }

    // Activation gate: needs rows on the map, then the opt-in or a commander binding.
    CHECK(!BossTacticsWanted(false, false, true));   // default install, normal player's bots: unchanged
    CHECK(BossTacticsWanted(true, false, true));     // AiPlayerbot.DungeonTactics = 1
    CHECK(BossTacticsWanted(false, true, true));     // commander-bound group
    CHECK(BossTacticsWanted(true, true, true));
    CHECK(!BossTacticsWanted(true, true, false));    // map without rows
    CHECK(!BossTacticsWanted(false, true, false));

    // Subject: the row's target, or the boss itself.
    CHECK(TacticSubjectEntry(Row(70, 7023, BossTacticKind::KillFirst, 7209)) == 7209);
    CHECK(TacticSubjectEntry(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006)) == 6910);

    // Air target: kept while the row matches, its aura is up and she is not
    // evading. The core's "cannot path to her" flag is not a reason to drop
    // her - a boss chased in flight has no floor path and may carry it.
    CHECK(AirTargetKeep(true, true, false, false));
    CHECK(!AirTargetKeep(true, false, false, false));    // aura down: landed or not airborne yet
    CHECK(!AirTargetKeep(false, true, false, false));    // no row, or the row is another entry
    CHECK(!AirTargetKeep(true, true, true, false));      // evade mode: the reset is underway
    CHECK(AirTargetKeep(true, true, false, true));       // unreachable is ignored ...
    CHECK(!AirTargetKeep(true, true, true, true));       // ... but not an evade mode
    CHECK(AirTargetKeep(true, true, false, true));

    // Air target reach distance: the spell's range less the 2-yd margin, not
    // the generic 75 % chase distance. A 30-yd cast under a 20-yd hover stands
    // ~19.6 yd out (sqrt(28^2 - 20^2)), not ~9 yd (sqrt(22.5^2 - 20^2) - contact)
    // where every caster bunches in Fireball's 8-yd splash.
    CHECK(std::fabs(AirTargetReachDistance(30.0f) - 28.0f) < 0.001f);
    CHECK(std::fabs(AirTargetGroundDistance(AirTargetReachDistance(30.0f), 20.0f) - std::sqrt(28.0f * 28.0f - 20.0f * 20.0f)) < 0.01f);
    CHECK(AirTargetGroundDistance(AirTargetReachDistance(30.0f), 20.0f) > 19.5f);
    CHECK(AirTargetGroundDistance(28.0f, 20.0f) > 19.5f && AirTargetGroundDistance(28.0f, 20.0f) < 19.7f);
    CHECK(AirTargetReachDistance(2.0f) <= 0.0f);   // no range left: the guard answers NotAirTarget

    // Air target ground stop: the 2D leg of a 3D cast from the floor under a
    // flying boss, never further than kAirTargetGroundRange out, and nothing
    // when the flight height alone is beyond the cast.
    CHECK(std::fabs(AirTargetGroundDistance(30.0f, 19.0f) - std::sqrt(30.0f * 30.0f - 19.0f * 19.0f)) < 0.01f);
    CHECK(AirTargetGroundDistance(30.0f, 19.0f) < kAirTargetGroundRange);
    CHECK(std::fabs(AirTargetGroundDistance(40.0f, 19.0f) - kAirTargetGroundRange) < 0.01f);  // capped
    CHECK(std::fabs(AirTargetGroundDistance(30.0f, 0.0f) - kAirTargetGroundRange) < 0.01f);   // capped on the ground
    CHECK(AirTargetGroundDistance(20.0f, 20.0f) == 0.0f);   // the height alone is out of range
    CHECK(AirTargetGroundDistance(15.0f, 19.0f) == 0.0f);
    CHECK(AirTargetGroundDistance(0.0f, 0.0f) == 0.0f);

    // Air target reach: a target the tactic does not cover leaves the decision
    // to the generic reach; every kept one either has a floor point or is left
    // alone. In particular a flight height at or beyond the cast range is
    // NoGroundPoint, never the generic chase that aims at the flight height.
    {
        float x = -1, y = -1;

        // Not an air target: the generic reach decides.
        CHECK(AirTargetReachPlan(false, true, 30.0f, 10.0f, 0, 0, 20, 0, x, y) == AirTargetReach::NotAirTarget);
        CHECK(AirTargetReachPlan(false, true, 0.0f, 99.0f, 0, 0, 20, 0, x, y) == AirTargetReach::NotAirTarget);

        // Kept but the height alone is beyond the cast: stay put, and the base
        // reach must not be chosen.
        CHECK(AirTargetReachPlan(true, true, 30.0f, 30.0f, 0, 0, 40, 0, x, y) == AirTargetReach::NoGroundPoint);
        CHECK(AirTargetReachPlan(true, true, 30.0f, 45.0f, 0, 0, 10, 0, x, y) == AirTargetReach::NoGroundPoint);
        CHECK(AirTargetReachPlan(true, true, 0.0f, 0.0f, 0, 0, 10, 0, x, y) == AirTargetReach::NoGroundPoint);
        CHECK(!AirTargetGenericReachApplies(AirTargetReach::NoGroundPoint));
        CHECK(!AirTargetGenericReachApplies(AirTargetReach::Positioned));
        CHECK(AirTargetGenericReachApplies(AirTargetReach::NotAirTarget));

        // Below the cast: a ground point on the line to the footprint, at the
        // ground leg whose 3D cast still lands inside the spell range.
        CHECK(AirTargetReachPlan(true, true, 30.0f, 10.0f, 0, 0, 40, 0, x, y) == AirTargetReach::Positioned);
        float const leg = AirTargetGroundDistance(30.0f, 10.0f);
        CHECK(x > 0.0f && x < 40.0f && std::fabs(y) < 0.01f);
        CHECK(std::fabs((40.0f - x) - leg) < 0.01f);  // exactly `leg` short of the footprint
        CHECK(std::sqrt((40.0f - x) * (40.0f - x) + 10.0f * 10.0f) <= 30.0f + 0.01f);

        // The point follows the bot's own line to the footprint.
        CHECK(AirTargetReachPlan(true, true, 30.0f, 10.0f, 0, 40, 0, 0, x, y) == AirTargetReach::Positioned);
        CHECK(y > 0.0f && y < 40.0f && std::fabs(x) < 0.01f);

        // Already inside the ground reach: no walk, the bot's own position.
        CHECK(AirTargetReachPlan(true, true, 30.0f, 10.0f, 35, 0, 40, 0, x, y) == AirTargetReach::Positioned);
        CHECK(std::fabs(x - 35.0f) < 0.01f && std::fabs(y) < 0.01f);
    }

    // Air target out of sight: a floor point would land the cast, but the
    // line to the footprint is blocked, so the plan asks for a point that sees
    // the target - never the generic chase, and never a stay while one exists.
    // A flight height at or beyond the cast stays NoGroundPoint out of sight too.
    {
        float x = -1, y = -1;
        CHECK(AirTargetReachPlan(true, false, 30.0f, 10.0f, 0, 0, 40, 0, x, y) == AirTargetReach::NeedLosPoint);
        CHECK(AirTargetReachPlan(true, false, 30.0f, 10.0f, 35, 0, 40, 0, x, y) == AirTargetReach::NeedLosPoint);  // inside the leg, still blind
        CHECK(!AirTargetGenericReachApplies(AirTargetReach::NeedLosPoint));
        CHECK(AirTargetReachPlan(true, false, 30.0f, 30.0f, 0, 0, 40, 0, x, y) == AirTargetReach::NoGroundPoint);
        CHECK(AirTargetReachPlan(true, false, 30.0f, 45.0f, 0, 0, 10, 0, x, y) == AirTargetReach::NoGroundPoint);
        CHECK(AirTargetReachPlan(false, false, 30.0f, 10.0f, 0, 0, 40, 0, x, y) == AirTargetReach::NotAirTarget);
    }

    // LoS candidates: the footprint and two rings, every point inside the
    // ground leg, so a cast from any of them reaches the flight height in 3D.
    // No candidate when the height alone is out of range.
    {
        std::vector<AirTargetGroundPoint> const points = AirTargetLosCandidates(30.0f, 10.0f, 100.0f, -50.0f);
        CHECK(static_cast<int>(points.size()) == kAirTargetLosCandidateCount);
        CHECK(points[0].x == 100.0f && points[0].y == -50.0f);
        float const leg = AirTargetGroundDistance(30.0f, 10.0f);
        bool anyOnOuterRing = false;
        for (AirTargetGroundPoint const& p : points)
        {
            float const dx = p.x - 100.0f;
            float const dy = p.y + 50.0f;
            float const ground = std::sqrt(dx * dx + dy * dy);
            CHECK(ground <= leg + 0.01f);
            CHECK(std::sqrt(ground * ground + 10.0f * 10.0f) <= 30.0f + 0.01f);
            if (ground > leg * 0.8f)
                anyOnOuterRing = true;
        }
        CHECK(anyOnOuterRing);
        CHECK(AirTargetLosCandidates(30.0f, 30.0f, 0, 0).empty());
        CHECK(AirTargetLosCandidates(30.0f, 45.0f, 0, 0).empty());
        CHECK(AirTargetLosCandidates(0.0f, 0.0f, 0, 0).empty());
    }

    // LoS point selection: nearest usable and reachable candidate to the bot;
    // a blocked sight line or no path skips the point; path searches are
    // bounded; nothing usable is -1 (stay).
    {
        std::vector<AirTargetGroundPoint> pts(4);
        pts[0].x = 0;  pts[0].y = 0;    // footprint, 20 yd from the bot
        pts[1].x = 15; pts[1].y = 0;    // 5 yd from the bot
        pts[2].x = 10; pts[2].y = 0;    // 10 yd
        pts[3].x = -10; pts[3].y = 0;   // 30 yd
        auto const all = [](int) { return true; };
        auto const none = [](int) { return false; };

        // Everything passes: the nearest walk wins.
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, kAirTargetLosPathProbes, all, all) == 1);
        // The nearest is blind (the obstacle): the next nearest that sees it.
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, kAirTargetLosPathProbes, [](int i) { return i != 1; }, all) == 2);
        // Sees it but no path: skipped as well.
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, kAirTargetLosPathProbes, all, [](int i) { return i == 0; }) == 0);
        // No usable point, or no path anywhere: stay.
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, kAirTargetLosPathProbes, none, all) == -1);
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, kAirTargetLosPathProbes, all, none) == -1);
        CHECK(SelectAirTargetLosPoint(std::vector<AirTargetGroundPoint>(), 20, 0, kAirTargetLosPathProbes, all, all) == -1);

        // Path searches are bounded: only the nearest `maxPathProbes` usable
        // points pay one; blind points do not count against the bound.
        int searches = 0;
        auto const countingNoPath = [&searches](int) { ++searches; return false; };
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, 2, all, countingNoPath) == -1);
        CHECK(searches == 2);
        searches = 0;
        auto const lastReachable = [&searches](int i) { ++searches; return i == 3; };
        CHECK(SelectAirTargetLosPoint(pts, 20, 0, 2, [](int i) { return i != 1 && i != 2; }, lastReachable) == 3);
        CHECK(searches == 2);
    }

    // LoS recovery budget: kAirTargetLosAttempts tries, then none until the
    // retry window has passed since the last; a reset gives the full budget.
    {
        AirTargetLosBudget budget;
        std::uint32_t now = 1000;
        for (int i = 0; i < kAirTargetLosAttempts; ++i)
            CHECK(AirTargetLosTryAttempt(budget, now + i * 100));
        std::uint32_t const last = now + (kAirTargetLosAttempts - 1) * 100;
        CHECK(!AirTargetLosTryAttempt(budget, last + 1));
        CHECK(!AirTargetLosTryAttempt(budget, last + kAirTargetLosRetryMs - 1));
        CHECK(AirTargetLosTryAttempt(budget, last + kAirTargetLosRetryMs));   // window passed: a fresh round
        CHECK(budget.attempts == 1);
        AirTargetLosBudgetReset(budget);
        for (int i = 0; i < kAirTargetLosAttempts; ++i)
            CHECK(AirTargetLosTryAttempt(budget, 50));
        CHECK(!AirTargetLosTryAttempt(budget, 60));
    }

    // Sight that flickers (a pillar edge, a boss turning at its hover point)
    // does not refill the budget: in sight one tick, out the next, for 10 s,
    // still at most kAirTargetLosAttempts recovery rounds. Sight held without
    // a break for kAirTargetLosVisibleResetMs does refill it.
    {
        AirTargetLosBudget budget;
        int granted = 0;
        for (std::uint32_t now = 1000; now < 1000 + kAirTargetLosRetryMs - 1; now += 100)
        {
            if ((now / 100) % 2 == 0)
                AirTargetLosSeen(budget, now);
            else if (AirTargetLosTryAttempt(budget, now))
                ++granted;
        }
        CHECK(granted == kAirTargetLosAttempts);

        // Seen for a moment only, then hidden again: still spent.
        AirTargetLosBudget held;
        for (int i = 0; i < kAirTargetLosAttempts; ++i)
            CHECK(AirTargetLosTryAttempt(held, 100 + i));
        AirTargetLosSeen(held, 200);
        AirTargetLosSeen(held, 200 + kAirTargetLosVisibleResetMs - 1);
        CHECK(!AirTargetLosTryAttempt(held, 200 + kAirTargetLosVisibleResetMs));
        // Sight held the full window: the tries come back.
        AirTargetLosSeen(held, 5000);
        AirTargetLosSeen(held, 5000 + kAirTargetLosVisibleResetMs);
        CHECK(AirTargetLosTryAttempt(held, 5000 + kAirTargetLosVisibleResetMs + 1));
        // A break in sight restarts the window.
        AirTargetLosBudget broken;
        for (int i = 0; i < kAirTargetLosAttempts; ++i)
            CHECK(AirTargetLosTryAttempt(broken, 100 + i));
        AirTargetLosSeen(broken, 200);
        CHECK(!AirTargetLosTryAttempt(broken, 1000));
        AirTargetLosSeen(broken, 1100);
        AirTargetLosSeen(broken, 200 + kAirTargetLosVisibleResetMs + 100);  // 3.1 s after the first sight, 2.2 s after the break
        CHECK(!AirTargetLosTryAttempt(broken, 200 + kAirTargetLosVisibleResetMs + 200));
    }

    // Stacked floors: the ring is laid out from the flight height over the
    // bot's floor (z 0, boss at z 20, cast 30 yd), and every candidate is
    // re-checked at the ground z it actually resolves to.
    {
        float const range = 30.0f;
        float const leg = AirTargetGroundDistance(range, 20.0f);
        // On the bot's floor at the outer ring: fits.
        CHECK(AirTargetLosPointFits(range, 0.0f, leg * 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 20.0f));
        // The same x/y resolved onto a platform 12 yd up: another floor.
        CHECK(!AirTargetLosPointFits(range, 0.0f, leg * 0.9f, 0.0f, 12.0f, 0.0f, 0.0f, 20.0f));
        // A pit 12 yd down: another floor as well.
        CHECK(!AirTargetLosPointFits(range, 0.0f, leg * 0.9f, 0.0f, -12.0f, 0.0f, 0.0f, 20.0f));
        // A dip 6 yd down at the outer ring: the same floor, but the 3D cast
        // from there (26 yd up, ~20 yd across) is out of range.
        CHECK(!AirTargetLosPointFits(range, 0.0f, leg * 0.9f, 0.0f, -6.0f, 0.0f, 0.0f, 20.0f));
        // ... while the inner ring in the same dip still lands.
        CHECK(AirTargetLosPointFits(range, 0.0f, leg * 0.5f, 0.0f, -6.0f, 0.0f, 0.0f, 20.0f));
        // A rise of 5 yd: closer to the boss, fits.
        CHECK(AirTargetLosPointFits(range, 0.0f, leg * 0.9f, 0.0f, 5.0f, 0.0f, 0.0f, 20.0f));
        // Exactly at the range: fits; a hair beyond: not.
        CHECK(AirTargetLosPointFits(range, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30.0f));
        CHECK(!AirTargetLosPointFits(range, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 30.1f));
    }

    // Interrupt matching: spell and caster both have to match; only interrupt rows count.
    {
        const std::vector<BossTacticRow> rows = {
            Row(70, 6910, BossTacticKind::Interrupt, 0, 16006),
            Row(70, 6910, BossTacticKind::Interrupt, 6911, 5000),
            Row(70, 6910, BossTacticKind::KillFirst, 6911, 0, 1),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(IsInterruptListed(active, 6910, 16006));
        CHECK(!IsInterruptListed(active, 6910, 15801));   // the filler bolt is not listed
        CHECK(!IsInterruptListed(active, 6911, 16006));   // another caster
        CHECK(IsInterruptListed(active, 6911, 5000));     // a listed add
        CHECK(!IsInterruptListed(active, 6911, 0));
        CHECK(!IsInterruptListed(ActiveBossRows(), 6910, 16006));
    }

    // Kill first: lowest priority wins, nearer breaks a tie, unlisted/unpickable/far ones are skipped.
    {
        const std::vector<BossTacticRow> rows = {
            Row(70, 6906, BossTacticKind::KillFirst, 6906, 0, 1),
            Row(70, 6906, BossTacticKind::KillFirst, 6907, 0, 2),
            Row(70, 6906, BossTacticKind::KillFirst, 6908, 0, 3),
            Row(70, 6907, BossTacticKind::KillFirst, 6906, 0, 1),  // duplicated under a second dwarf
            Row(70, 7023, BossTacticKind::KillFirst, 7209, 0, 1),
        };
        const ActiveBossRows active = Active(rows);

        // The three dwarves in the order they were found: Olaf, Eric, Baelog.
        std::vector<TacticCandidate> dwarves = { Cand(6908, 5), Cand(6907, 5), Cand(6906, 30) };
        CHECK(PickKillFirst(active, dwarves) == 2);          // Baelog, even from further away
        dwarves[2].pickable = false;                         // dead or not attackable
        CHECK(PickKillFirst(active, dwarves) == 1);          // Eric
        dwarves[1].distance = 41.0f;                         // out of engage range
        CHECK(PickKillFirst(active, dwarves) == 0);          // Olaf
        dwarves[0].pickable = false;
        CHECK(PickKillFirst(active, dwarves) == -1);

        // Same priority: the nearer one.
        const std::vector<TacticCandidate> shards = { Cand(7209, 20), Cand(7209, 8), Cand(7023, 3) };
        CHECK(PickKillFirst(active, shards) == 1);           // the boss itself is not listed

        // Not in this fight's rows: no pick.
        CHECK(PickKillFirst(active, { Cand(1234, 3) }) == -1);
        CHECK(PickKillFirst(active, {}) == -1);
        CHECK(PickKillFirst(ActiveBossRows(), shards) == -1);

        // Interrupt and behind rows never pick a target.
        const std::vector<BossTacticRow> other = { Row(70, 6910, BossTacticKind::Interrupt, 0, 16006) };
        CHECK(PickKillFirst(Active(other), { Cand(6910, 3) }) == -1);
    }

    // Boss front: inside the radius and in the frontal half-plane only.
    {
        const float pi = 3.1415926536f;
        // Boss at the origin facing +x (0 rad).
        CHECK(InsideBossFront(0, 0, 0.0f, 5, 0, 10));         // dead ahead
        CHECK(InsideBossFront(0, 0, 0.0f, 4, 4, 10));         // front, 45 degrees off
        CHECK(!InsideBossFront(0, 0, 0.0f, 5 * std::cos(1.75f), 5 * std::sin(1.75f), 10));  // 100 degrees off: beside it, out
        CHECK(!InsideBossFront(0, 0, 0.0f, -5, 0, 10));       // behind
        CHECK(!InsideBossFront(0, 0, 0.0f, 12, 0, 10));       // ahead but outside the radius
        // Facing -x (pi rad): the same test with the heading wrapped.
        CHECK(InsideBossFront(0, 0, pi, -5, 0, 10));
        CHECK(!InsideBossFront(0, 0, pi, 5, 0, 10));
        // Facing 5.5 rad (equivalent to -0.78): a bot at angle -0.5 is in front.
        CHECK(InsideBossFront(0, 0, 5.5f, 5 * std::cos(-0.5f), 5 * std::sin(-0.5f), 10));
    }

    // Flank destination: lands outside the front, at the bot's own distance, on the nearer side.
    {
        float x = 0, y = 0;
        BossFlankDestination(0, 0, 0.0f, 6, 1, 3.0f, x, y);   // bot ahead and a little to +y
        CHECK(!InsideBossFront(0, 0, 0.0f, x, y, 20));
        CHECK(y > 0.0f);                                      // the +y side is nearer
        CHECK(std::fabs(std::sqrt(x * x + y * y) - std::sqrt(37.0f)) < 0.01f);
        BossFlankDestination(0, 0, 0.0f, 6, -1, 3.0f, x, y);
        CHECK(y < 0.0f);
        BossFlankDestination(0, 0, 0.0f, 0.5f, 0.0f, 3.0f, x, y);  // inside the minimum: pushed out to it
        CHECK(std::fabs(std::sqrt(x * x + y * y) - 3.0f) < 0.01f);
        CHECK(!InsideBossFront(0, 0, 0.0f, x, y, 20));
    }

    // avoid_cast: the text form, and a row needs the spell and a radius.
    {
        CHECK(ParseBossTacticKind("avoid_cast") == BossTacticKind::AvoidCast);
        BossTacticTable table;
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 0, 0, 13.0f)));      // no spell
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 0.0f)));   // no radius
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, -1.0f)));
        CHECK(table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f)));
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 15.0f)));  // same key
        table.Seal();
        CHECK(table.HasMap(309));
    }

    // avoid_cast radius: only the listed spell of the listed caster; the widest row wins.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f),
            Row(309, 14750, BossTacticKind::Interrupt, 0, 16006),
            Row(309, 14517, BossTacticKind::AvoidCast, 14965, 23970, 0, 8.0f),
            Row(309, 14518, BossTacticKind::AvoidCast, 14965, 23970, 0, 11.0f),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(AvoidCastRadius(active, 14750, 24024) == 13.0f);
        CHECK(AvoidCastRadius(active, 14750, 23511) == 0.0f);   // another spell of the same caster
        CHECK(AvoidCastRadius(active, 14751, 24024) == 0.0f);   // the same spell from another caster
        CHECK(AvoidCastRadius(active, 14750, 16006) == 0.0f);   // an interrupt row is not an avoid row
        CHECK(AvoidCastRadius(active, 14965, 23970) == 11.0f);  // the add named by two rows: the wider one
        CHECK(AvoidCastRadius(ActiveBossRows(), 14750, 24024) == 0.0f);
    }

    // Who leaves: everyone inside the radius except a tank.
    {
        CHECK(MustLeaveCast(false, 13.0f, 2.0f));
        CHECK(MustLeaveCast(false, 13.0f, 12.9f));
        CHECK(!MustLeaveCast(false, 13.0f, 13.0f));   // on the edge is out
        CHECK(!MustLeaveCast(false, 13.0f, 20.0f));
        CHECK(!MustLeaveCast(true, 13.0f, 2.0f));     // the tank holding the caster
        CHECK(!MustLeaveCast(false, 0.0f, 2.0f));     // no row
    }

    // Escape points: on a circle a step past the radius around the caster, the bot's own bearing first,
    // then alternating sides; every point is outside the radius.
    {
        AvoidCastPoint points[kAvoidCastPoints];
        const int n = AvoidCastDestinations(10.0f, 20.0f, 14.0f, 20.0f, 13.0f, 0.0f, points);
        CHECK(n == kAvoidCastPoints);
        const float ring = 13.0f + kAvoidCastStepOut;
        CHECK(std::fabs(points[0].x - (10.0f + ring)) < 0.01f);   // straight away from the caster (+x)
        CHECK(std::fabs(points[0].y - 20.0f) < 0.01f);
        for (int i = 0; i < n; ++i)
        {
            const float dx = points[i].x - 10.0f;
            const float dy = points[i].y - 20.0f;
            CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - ring) < 0.01f);
            CHECK(!MustLeaveCast(false, 13.0f, std::sqrt(dx * dx + dy * dy)));
        }
        CHECK(points[1].y > 20.0f && points[2].y < 20.0f);        // 45 degrees to one side, then the other
        CHECK(std::fabs(points[1].y - 20.0f + (points[2].y - 20.0f)) < 0.01f);
        // A bot on top of the caster has no bearing: the fallback angle is used.
        AvoidCastDestinations(10.0f, 20.0f, 10.0f, 20.0f, 13.0f, 3.14159265f, points);
        CHECK(points[0].x < 10.0f - ring + 0.01f);
        CHECK(std::fabs(points[0].y - 20.0f) < 0.01f);
    }

    // One log line per bot per cast: the first walk-out of a cast is new, the ticks that follow within the gap are
    // not, and another caster, another spell or a long silence start a new cast.
    {
        AvoidCastLogState state;
        CHECK(AvoidCastIsNewCast(state, 0x1111, 24024, 100, 1000));                    // first sight
        CHECK(!AvoidCastIsNewCast(state, 0x1111, 24024, 100, 1500));                   // the next tick of the cast
        CHECK(!AvoidCastIsNewCast(state, 0x1111, 24024, 100, 3400));                   // a 3 s blast kept alive tick by tick
        CHECK(AvoidCastIsNewCast(state, 0x2222, 24024, 100, 3500));                    // another caster, same spell
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600));                    // same caster, another spell
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600 + kAvoidCastLogGapMs));  // the cast ended, a new one
        CHECK(!AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600 + kAvoidCastLogGapMs + 10));
        // Same caster and spell but a new Spell object (a recast inside the gap) is a new cast.
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 200, 3600 + kAvoidCastLogGapMs + 20));
        // Two casters at once, the nearest one alternating between ticks: one row each.
        AvoidCastLogState two;
        CHECK(AvoidCastIsNewCast(two, 0xA, 24024, 1, 10000));
        CHECK(AvoidCastIsNewCast(two, 0xB, 24024, 2, 10100));
        CHECK(!AvoidCastIsNewCast(two, 0xA, 24024, 1, 10200));
        CHECK(!AvoidCastIsNewCast(two, 0xB, 24024, 2, 10300));
        CHECK(!AvoidCastIsNewCast(two, 0xA, 24024, 1, 10400));
        // More live casts than slots evicts the one seen longest ago.
        AvoidCastLogState many;
        for (std::uint64_t g = 1; g <= kAvoidCastLogCasts; ++g)
            CHECK(AvoidCastIsNewCast(many, g, 24024, g, 20000 + 10 * static_cast<std::uint32_t>(g)));
        CHECK(AvoidCastIsNewCast(many, 99, 24024, 99, 20100));
        CHECK(AvoidCastIsNewCast(many, 1, 24024, 1, 20110));   // caster 1 was the evicted one
        // The millisecond clock wraps: 4294967000 -> 100 is 396 ms, still the same cast.
        AvoidCastLogState wrap;
        CHECK(AvoidCastIsNewCast(wrap, 7, 1, 100, 4294967000u));
        CHECK(!AvoidCastIsNewCast(wrap, 7, 1, 100, 100u));
    }

    // Who holds a caster: only a tank the caster is on. A caster on nobody keeps nobody in, and a healer it turned to
    // leaves.
    {
        CHECK(HoldsCaster(true, true));
        CHECK(!HoldsCaster(true, false));    // an off-tank, or a tank before the caster picked anyone
        CHECK(!HoldsCaster(false, true));    // a healer the caster is on
        CHECK(!HoldsCaster(false, false));
    }

    // The avoid_cast map flag: only a map with an avoid_cast row, and only after the seal.
    {
        BossTacticTable flags;
        CHECK(flags.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 8292, 0, 0.0f)));
        CHECK(flags.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f)));
        CHECK(!flags.HasAvoidCastMap(309));   // not before the seal
        flags.Seal();
        CHECK(flags.HasAvoidCastMap(309));
        CHECK(!flags.HasAvoidCastMap(70));    // rows, but no avoid_cast row
        CHECK(flags.HasMap(70));
        CHECK(!flags.HasAvoidCastMap(1));
    }

    // Zones: 3D, centre to centre, the edge is out.
    {
        std::vector<CastDangerZone> zones = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(InsideAnyCastZone(zones, 12.9f, 0.0f, 0.0f));
        CHECK(!InsideAnyCastZone(zones, 13.0f, 0.0f, 0.0f));
        CHECK(!InsideAnyCastZone(zones, 5.0f, 0.0f, 12.5f));   // 13.5 yd in 3D: a ledge above is out
        CHECK(!InsideAnyCastZone(std::vector<CastDangerZone>(), 0.0f, 0.0f, 0.0f));
    }

    // The walk-out's straight line: never deeper into its own blast, never through another one.
    {
        std::vector<CastDangerZone> one = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        // A bot 10 yd east: straight out, 45 and 90 degrees are clear; 135 degrees would cross the caster.
        AvoidCastPoint ring[kAvoidCastPoints];
        AvoidCastDestinations(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, ring);
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[0].x, ring[0].y, one));
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[1].x, ring[1].y, one));
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[3].x, ring[3].y, one) ==
              (SegmentPointDistance2D(10.0f, 0.0f, ring[3].x, ring[3].y, 0.0f, 0.0f) >= 10.0f - kAvoidCastChordSlack));
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[5].x, ring[5].y, one));
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[6].x, ring[6].y, one));
        // A second rider 25 yd east of the first: straight out (east) runs through its blast.
        std::vector<CastDangerZone> two = {Zone(0.0f, 0.0f, 0.0f, 13.0f), Zone(25.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[0].x, ring[0].y, two));
    }

    // The escapes: only clear chords, shortest walk first.
    {
        std::vector<CastDangerZone> one = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        AvoidCastPoint out[kAvoidCastPoints];
        const int n = AvoidCastEscapes(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, one, out);
        CHECK(n >= 3 && n < kAvoidCastPoints);
        CHECK(std::fabs(out[0].x - (13.0f + kAvoidCastStepOut)) < 0.01f && std::fabs(out[0].y) < 0.01f);
        float last = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float d = std::sqrt((out[i].x - 10.0f) * (out[i].x - 10.0f) + out[i].y * out[i].y);
            CHECK(d + 0.001f >= last);
            last = d;
            CHECK(out[i].x > -0.01f);   // nothing on the far side of the caster
        }
        // Boxed in by a second blast on the straight-out side: straight out is gone, the turns stay.
        std::vector<CastDangerZone> two = {Zone(0.0f, 0.0f, 0.0f, 13.0f), Zone(25.0f, 0.0f, 0.0f, 13.0f)};
        const int m = AvoidCastEscapes(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, two, out);
        CHECK(m > 0 && m < n);
        for (int i = 0; i < m; ++i)
            CHECK(std::fabs(out[i].y) > 1.0f);
    }

    // The cast hold: a move whose destination stands in a blast is held; not the walk-out, not a move to the bot
    // itself, not a move with no destination, not one outside every blast.
    {
        std::vector<CastDangerZone> zones = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(CastHoldsMove(false, false, true, 3.0f, 0.0f, 0.0f, zones));       // a reach to the caster or beside it
        CHECK(CastHoldsMove(false, false, true, 0.0f, 12.0f, 0.0f, zones));      // a heal walk to the tank, a stay spot
        CHECK(!CastHoldsMove(false, false, true, 0.0f, 20.0f, 0.0f, zones));     // a follow to a master outside
        CHECK(!CastHoldsMove(true, false, true, 3.0f, 0.0f, 0.0f, zones));       // the walk-out itself
        CHECK(!CastHoldsMove(false, true, true, 3.0f, 0.0f, 0.0f, zones));       // a move to the bot itself
        CHECK(!CastHoldsMove(false, false, false, 3.0f, 0.0f, 0.0f, zones));
        CHECK(!CastHoldsMove(false, false, true, 3.0f, 0.0f, 0.0f, std::vector<CastDangerZone>()));  // the tank's held caster
    }

    // The walk names in the log row.
    {
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::Started)) == "started");
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::Deferred)) == "deferred");
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::None)) == "none");
    }

    std::cout << "All boss tactics policy tests passed.\n";
    return 0;
}
