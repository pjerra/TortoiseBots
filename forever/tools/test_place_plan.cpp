// Behaviour test of the place / rally / fall back command: the words as `.tb cmd` hands them over, the plan (who moves, to which slot) and the hold's
// rules (host/cmd/TbPlacePlan.h; issue #15, forever/docs/PLACE-RALLY-NOTES.md). Pure: no core header, a fake world answers the two probes.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_place_plan.cpp -o /tmp/test_place_plan && /tmp/test_place_plan
#include "forever/src/host/cmd/TbPlacePlan.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace P = TortoiseBots::place;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static float Dist(P::Vec2 a, P::Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

namespace
{
std::vector<P::BotFacts> Party()
{
    return {
        { "Mtank", "warrior", 1, P::Rank::Tank },
        { "Mrogue", "rogue", 1, P::Rank::Melee },
        { "Mmage", "mage", 1, P::Rank::Ranged },
        { "Mpriest", "priest", 1, P::Rank::Healer },
    };
}

P::Probe FlatWorld()
{
    P::Probe probe;
    probe.ground = [](P::Vec2 const&, float& z) { z = 0.0f; return true; };
    probe.reachable = [](size_t, P::Vec2 const&, float) { return true; };
    return probe;
}

P::AnchorFacts TargetAnchor(P::Vec2 player, P::Vec2 target, float targetHalf)
{
    P::AnchorFacts anchor;
    anchor.kind = P::Anchor::Target;
    anchor.player = player;
    anchor.playerFront = { 0.0f, 1.0f };
    anchor.target = target;
    anchor.targetHalf = targetHalf;
    return anchor;
}

const P::Step* StepOfBot(P::PlanResult const& plan, size_t bot)
{
    for (P::Step const& step : plan.steps)
        if (step.bot == bot)
            return &step;
    return nullptr;
}
}

static void TestCommandWords()
{
    // `.tb cmd` hands the words over unchanged, verb first: `place circle` must be a place, never a `circle` order.
    P::Command c = P::ParseCommand({ "place", "circle" });
    CHECK(c.kind == P::CommandKind::Place && c.shape == P::Shape::Circle && c.anchor == P::Anchor::Here && c.gap == P::Gap::Normal && c.code.empty());
    c = P::ParseCommand({ "place", "circle", "target" });
    CHECK(c.kind == P::CommandKind::Place && c.shape == P::Shape::Circle && c.anchor == P::Anchor::Target && c.code.empty());
    c = P::ParseCommand({ "place", "line", "wide", "here", "tanks" });
    CHECK(c.kind == P::CommandKind::Place && c.shape == P::Shape::Line && c.gap == P::Gap::Wide && c.anchor == P::Anchor::Here);
    CHECK(c.who.size() == 1 && c.who[0].kind == P::WhoKind::Tanks);
    c = P::ParseCommand({ "place", "follow", "Mtank" });
    CHECK(c.kind == P::CommandKind::PlaceFollow && c.who.size() == 1 && c.who[0].kind == P::WhoKind::Name);
    c = P::ParseCommand({ "place" });
    CHECK(c.kind == P::CommandKind::Place && c.code == "missing-argument");
    c = P::ParseCommand({ "place", "blob" });
    CHECK(c.code == "bad-argument");
    c = P::ParseCommand({ "place", "close", "tight", "wide" });
    CHECK(c.code == "duplicate");
    c = P::ParseCommand({ "place", "close", "here", "target" });
    CHECK(c.code == "duplicate");

    // `fall back` is one fall back, and `fall` on its own is not one (and not `back` by accident).
    c = P::ParseCommand({ "fall", "back" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code.empty());
    c = P::ParseCommand({ "fall" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code == "missing-argument");
    c = P::ParseCommand({ "fall", "sideways" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code == "bad-argument");
    c = P::ParseCommand({ "fall", "back", "now" });
    CHECK(c.code == "unexpected-argument");
    c = P::ParseCommand({ "fallback" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code.empty());
    c = P::ParseCommand({ "back" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code.empty());
    c = P::ParseCommand({ "back", "off" });
    CHECK(c.code == "unexpected-argument");

    c = P::ParseCommand({ "rally", "here" });
    CHECK(c.kind == P::CommandKind::RallyHere && c.code.empty());
    c = P::ParseCommand({ "rally", "there" });
    CHECK(c.code == "bad-argument");
    c = P::ParseCommand({ "rally" });
    CHECK(c.code == "missing-argument");
    c = P::ParseCommand({ "rally", "here", "now" });
    CHECK(c.code == "unexpected-argument");
    c = P::ParseCommand({ "resume" });
    CHECK(c.kind == P::CommandKind::Resume && c.code.empty());
    c = P::ParseCommand({ "resume", "now" });
    CHECK(c.code == "unexpected-argument");
    CHECK(P::ParseCommand({ "move", "1", "2", "3" }).kind == P::CommandKind::None);

    // Chat: a bare alias only when the whole message is the command.
    CHECK(P::ChatWordsAreCommand({ "back" }));
    CHECK(P::ChatWordsAreCommand({ "fall", "back" }));
    CHECK(P::ChatWordsAreCommand({ "rally", "here" }));
    CHECK(P::ChatWordsAreCommand({ "place", "circle" }));
    CHECK(!P::ChatWordsAreCommand({ "back", "off", "pull" }));
    CHECK(!P::ChatWordsAreCommand({ "rally", "the", "pack" }));
    CHECK(!P::ChatWordsAreCommand({ "fallback", "after", "that" }));
    CHECK(!P::ChatWordsAreCommand({ "move", "1", "2", "3" }));
}

static void TestPlaceCircle()
{
    std::vector<P::BotFacts> const bots = Party();
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal, TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f),
        bots, {}, FlatWorld());
    CHECK(plan.ok);
    CHECK(plan.selected == 4 && plan.placed == 4 && plan.refused == 0);
    CHECK(plan.steps.size() == 4);
    // Slot order: tank first, healer last; the tank's slot is on the player's side of the ring.
    CHECK(plan.steps[0].bot == 0 && plan.steps[1].bot == 1 && plan.steps[2].bot == 2 && plan.steps[3].bot == 3);
    CHECK(plan.steps[0].slot == 0 && plan.steps[3].slot == 3);
    for (P::Step const& step : plan.steps)
    {
        CHECK(step.placed);
        CHECK(std::fabs(Dist(step.pos, { 0.0f, 0.0f }) - plan.layout.radius) < 0.05f);
    }
    CHECK(plan.steps[0].pos.y > 0.0f);                                   // the player's side
    // The whole ring clears the target: the radius is at least its size plus one gap.
    CHECK(plan.layout.radius >= 2.0f + P::GapYd(P::Gap::Normal) - 1e-3f);
}

static void TestPlaceRefusalsArePerBot()
{
    std::vector<P::BotFacts> bots = Party();
    bots[2].alive = false;                                               // Mmage dead
    bots[3].sameMap = false;                                             // Mpriest elsewhere
    P::Probe probe = FlatWorld();
    probe.reachable = [](size_t bot, P::Vec2 const&, float) { return bot != 1; };   // Mrogue cannot walk anywhere
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal, TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f),
        bots, {}, probe);
    CHECK(plan.ok);
    CHECK(plan.placed == 1);                                             // only the tank: the other three are refused
    P::Step const* tank = StepOfBot(plan, 0);
    P::Step const* rogue = StepOfBot(plan, 1);
    P::Step const* mage = StepOfBot(plan, 2);
    P::Step const* priest = StepOfBot(plan, 3);
    CHECK(tank && tank->placed);
    CHECK(rogue && !rogue->placed && rogue->code == "unreachable");
    CHECK(mage && !mage->placed && mage->code == "dead");
    CHECK(priest && !priest->placed && priest->code == "other-map");
    CHECK(plan.refused == 3);

    // No floor under the ring: every bot is refused, and no bot moves.
    P::Probe noGround = FlatWorld();
    noGround.ground = [](P::Vec2 const&, float&) { return false; };
    P::PlanResult const air = P::PlanPlace(P::Shape::Circle, P::Gap::Normal, TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f),
        Party(), {}, noGround);
    CHECK(air.ok && air.placed == 0 && air.refused == 4 && air.steps[0].code == "no-ground");

    // The who words select a subset, and a name that is not one of the bots is the whole command's refusal.
    std::vector<P::WhoToken> who;
    P::WhoToken token;
    P::ParseWho("tanks", token);
    who.push_back(token);
    P::PlanResult const tanksOnly = P::PlanPlace(P::Shape::Line, P::Gap::Normal, TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f),
        Party(), who, FlatWorld());
    CHECK(tanksOnly.ok && tanksOnly.selected == 1 && tanksOnly.placed == 1 && tanksOnly.steps[0].bot == 0);
    P::ParseWho("Mpal", token);
    P::PlanResult const missing = P::PlanPlace(P::Shape::Line, P::Gap::Normal, TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f),
        Party(), { token }, FlatWorld());
    CHECK(!missing.ok && missing.code == "bad-target");
}

static void TestCommanderSpot()
{
    // Target ring: the commander stands exactly on where slot 0 would be.
    P::PlanResult const onTarget = P::PlanPlace(P::Shape::Circle, P::Gap::Normal, TargetAnchor({ 0.0f, 8.0f }, { 0.0f, 2.0f }, 2.0f),
        Party(), {}, FlatWorld());
    CHECK(onTarget.ok && onTarget.placed == 4);
    for (P::Step const& step : onTarget.steps)
        CHECK(Dist(step.pos, { 0.0f, 8.0f }) >= P::kCommanderClearYd - 1e-3f);

    // Cursor: the ping lands on a point whose ring slot 2 would be the commander's feet.
    P::AnchorFacts cursor;
    cursor.kind = P::Anchor::Cursor;
    cursor.player = { 0.0f, 0.0f };
    cursor.playerFront = { 0.0f, 1.0f };
    cursor.cursor = { 0.0f, 4.0f };
    P::PlanResult const onCursor = P::PlanPlace(P::Shape::Circle, P::Gap::Normal, cursor, Party(), {}, FlatWorld());
    CHECK(onCursor.ok && onCursor.placed == 4);
    for (P::Step const& step : onCursor.steps)
        CHECK(Dist(step.pos, { 0.0f, 0.0f }) >= P::kCommanderClearYd - 1e-3f);

    // Here: the shape stands clear by construction, every anchor holds the rule.
    P::AnchorFacts here;
    here.kind = P::Anchor::Here;
    here.player = { 0.0f, 0.0f };
    here.playerFront = { 0.0f, 1.0f };
    P::PlanResult const atHere = P::PlanPlace(P::Shape::Line, P::Gap::Tight, here, Party(), {}, FlatWorld());
    CHECK(atHere.ok);
    for (P::Step const& step : atHere.steps)
        CHECK(Dist(step.pos, { 0.0f, 0.0f }) >= P::kCommanderClearYd - 1e-3f);
}

static void TestFallbackFront()
{
    // A stored rally point: the front runs from the point towards the commander, so the tanks are on the side the group came from.
    P::PlanResult const stored = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 1.0f, 0.0f }, true, Party(), FlatWorld());
    CHECK(stored.ok && stored.placed == 4);
    CHECK(std::fabs(stored.frame.front.x) < 1e-3f && stored.frame.front.y > 0.99f);
    CHECK(stored.steps[0].pos.y > stored.steps[1].pos.y);                    // the tank in front of the melee
    for (P::Step const& step : stored.steps)
        CHECK(Dist(step.pos, { 0.0f, 0.0f }) > 0.5f);                         // nobody on the rally point

    // Without a rally point the commander's facing is the front.
    P::PlanResult const facing = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f }, false, Party(), FlatWorld());
    CHECK(facing.ok);
    CHECK(facing.frame.front.x > 0.99f);
    CHECK(facing.steps[0].pos.x > facing.steps[1].pos.x);
    // A commander on the point cannot give a direction: the facing stands in.
    P::PlanResult const onPoint = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f }, true, Party(), FlatWorld());
    CHECK(onPoint.ok && onPoint.frame.front.y > 0.99f);

    // A fall-back slot on the commander's spot is dropped, and nobody stands on it.
    P::PlanResult const underCommander = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 2.0f }, { 0.0f, 1.0f }, true, Party(), FlatWorld());
    CHECK(underCommander.ok && underCommander.placed == 3);
    CHECK(!underCommander.steps[0].placed && underCommander.steps[0].code == "no-slot");   // the tank's front row is the commander's own spot
    CHECK(underCommander.steps[1].placed && std::fabs(underCommander.steps[1].pos.y + 2.0f) < 1e-3f);
    for (P::Step const& step : underCommander.steps)
        if (step.placed)
            CHECK(Dist(step.pos, { 0.0f, 2.0f }) >= P::kCommanderClearYd - 1e-3f);
}

static P::HoldRow Row(uint64_t low, float y, uint32_t seq = 1)
{
    P::HoldRow row;
    row.bot.low = low;
    row.y = y;
    row.seq = seq;
    row.slot = 0;
    return row;
}

static void TestHoldBookkeeping()
{
    P::State state;
    std::vector<P::GuidKey> const a{ { 0, 1 } }, b{ { 0, 2 } }, c{ { 0, 3 } };
    // First placement: A, B, C.
    P::StorePlace(state, 7, { Row(1, 1.0f), Row(2, 2.0f), Row(3, 3.0f) }, { a[0], b[0], c[0] }, true);
    CHECK(state.holds[7].rows.size() == 3);

    // A subset placement (A again, B refused): C keeps its row, A is replaced, B's old row is dropped for its release.
    std::vector<P::HoldRow> const dropped = P::StorePlace(state, 7, { Row(1, 10.0f) }, { a[0], b[0] }, true);
    CHECK(state.holds[7].rows.size() == 2);
    CHECK(state.holds[7].rows[0].bot.low == 3);                              // C survived
    CHECK(state.holds[7].rows[1].bot.low == 1 && std::fabs(state.holds[7].rows[1].y - 10.0f) < 1e-3f);
    CHECK(dropped.size() == 1 && dropped[0].bot.low == 2);                  // B's old row; A's was replaced by the new order

    // A place that put nobody leaves the old hold exactly as it was.
    std::vector<P::HoldRow> const before = state.holds[7].rows;
    std::vector<P::HoldRow> const none = P::StorePlace(state, 7, {}, { a[0], b[0], c[0] }, false);
    CHECK(none.empty());
    CHECK(state.holds[7].rows.size() == before.size());
    CHECK(state.holds[7].rows[0].bot.low == before[0].bot.low);

    // A fall back forms the group anew: the whole old hold is replaced.
    std::vector<P::HoldRow> const replaced = P::StoreFallback(state, 7, { Row(4, 4.0f) });
    CHECK(replaced.size() == 2);                                             // both old bots are released
    CHECK(state.holds[7].fallback);
    CHECK(state.holds[7].rows.size() == 1 && state.holds[7].rows[0].bot.low == 4);

    // The walk-back gives up after three failed checks, once.
    CHECK(!P::WalkGiveUp(0) && !P::WalkGiveUp(2) && P::WalkGiveUp(3));

    // Every terminal reason releases the bot: follow while it is ours, clear the placement's stay when it changed commander.
    CHECK(P::ReleaseOn(P::RowEnd::None, true) == P::Release::None);
    CHECK(P::ReleaseOn(P::RowEnd::Dead, true) == P::Release::Follow);
    CHECK(P::ReleaseOn(P::RowEnd::Unreachable, true) == P::Release::Follow);
    CHECK(P::ReleaseOn(P::RowEnd::Commander, false) == P::Release::ClearStay);
    CHECK(P::ReleaseOn(P::RowEnd::OtherMap, false) == P::Release::ClearStay);
    // A run or a newer order drives the bot: their driver must not be fought with a `follow`.
    CHECK(P::ReleaseOn(P::RowEnd::Run, true) == P::Release::ClearStay);
    CHECK(P::ReleaseOn(P::RowEnd::NewerOrder, true) == P::Release::ClearStay);

    // A later command ends the pending cursor request, and logout forgets rallies, holds and pendings.
    P::PendingRequest pending;
    pending.commander = 7;
    pending.words = { "place", "line", "cursor" };
    state.pendings[7] = pending;
    state.rallies[7].set = true;
    P::PendingRequest taken;
    CHECK(P::TakePending(state, 7, taken) && taken.words.size() == 3 && taken.commander == 7);
    CHECK(!P::TakePending(state, 7, taken));
    state.pendings[7] = pending;
    P::Forget(state, 7);
    CHECK(state.holds.empty() && state.rallies.empty() && state.pendings.empty());
}

static void TestClock()
{
    // A 32-bit clock: a target just before the wrap is in the past once the clock has wrapped, never "still in the future for 49 days".
    CHECK(P::TimeReached(16u, 0xFFFFFF00u));
    CHECK(!P::TimeReached(0xFFFFFF00u, 16u));
    CHECK(P::TimeReached(1000u, 999u));
    CHECK(P::TimeReached(1000u, 1000u));
    CHECK(!P::TimeReached(999u, 1000u));
}

// A walk back queues a new `come`: the row must carry its number, or ReleasePlaceStay(bot, row.seq) later finds no match and keeps the stay.
static void TestWalkBackKeepsTheRowsOrder()
{
    P::HoldRow row;
    row.seq = 5u;
    row.walkFails = 2u;
    row.walkMs = 100u;
    P::AfterWalkBack(row, 9u, 4000u);
    CHECK(row.seq == 9u);
    CHECK(row.walkMs == 4000u);
    CHECK(row.walkFails == 0u);
}

// A dropped row releases its bot with `follow` only while the row's own `come` is still the bot's last order.
static void TestDroppedFollowYieldsToNewerOrder()
{
    CHECK(!P::NewerOrderOwns(5u, 5u));
    CHECK(P::NewerOrderOwns(6u, 5u));
    CHECK(!P::NewerOrderOwns(4u, 5u));
    CHECK(!P::NewerOrderOwns(0u, 0u));
}

int main()
{
    TestCommandWords();
    TestPlaceCircle();
    TestPlaceRefusalsArePerBot();
    TestCommanderSpot();
    TestFallbackFront();
    TestHoldBookkeeping();
    TestClock();
    TestWalkBackKeepsTheRowsOrder();
    TestDroppedFollowYieldsToNewerOrder();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}
