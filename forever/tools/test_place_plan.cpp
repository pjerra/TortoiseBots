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
    probe.liquid = [](P::Vec2 const&, float) { return false; };
    probe.reachable = [](size_t, P::Vec2 const&, float) { return P::PathVerdict::Ok; };
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
    CHECK(c.code == "bad-argument");
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
    CHECK(c.kind == P::CommandKind::RallyShow && c.code.empty());
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
    probe.reachable = [](size_t bot, P::Vec2 const&, float) { return bot != 1 ? P::PathVerdict::Ok : P::PathVerdict::NoPath; };   // Mrogue cannot walk anywhere
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

    // A fall-back slot on the commander's spot is dropped, and nobody stands on it; the refused bot still disengages to the centre.
    P::PlanResult const underCommander = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 2.0f }, { 0.0f, 1.0f }, true, Party(), FlatWorld());
    CHECK(underCommander.ok && underCommander.placed == 4 && underCommander.refused == 0);
    // The tank's front row is the commander's own spot: a fall back nudges it off the spot instead of walking the bot onto the commander.
    CHECK(underCommander.steps[0].placed && underCommander.steps[0].slot == 0 && underCommander.steps[0].to.empty());
    CHECK(underCommander.steps[0].nudged > 1.0f);
    CHECK(Dist(underCommander.steps[0].pos, { 0.0f, 2.0f }) >= P::kCommanderClearYd - 1e-3f);
    CHECK(underCommander.steps[1].placed && std::fabs(underCommander.steps[1].pos.y + 2.0f) < 1e-3f);
    for (P::Step const& step : underCommander.steps)
        if (step.placed && step.to.empty())
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
    CHECK(P::ReleaseOn(P::RowEnd::StayGone, true) == P::Release::ClearStay);

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

// #316 (1): a whispered `follow` runs in the engine's chat path. It drops the stay and advances no order number, so the row's `come` is still
// the bot's last order: the old rule (`last.seq > row.seq && no stay`) never fired, the row survived, and across one walk-back interval it
// queued another `come` that pulled the bot back to the old slot.
static void TestWhisperFollowEndsTheHold()
{
    P::HoldRow row;
    row.seq = 11u;
    P::StartRowClock(row, 1000u);
    uint32_t const lastSeq = 11u;                 // the row's own `come`; a whisper numbers nothing
    bool const stayActive = false;                // the chat shortcut dropped the stay
    CHECK(!P::NewerOrderOwns(lastSeq, row.seq));  // the old rule's whole condition: no newer order...
    CHECK(P::RowStayGone(true, stayActive, false));

    // Across one walk-back interval: Update's own order of checks, once a second. The row must end on the first pass, before any walk back
    // is queued (the walk back is due 5 s after the row's clock).
    uint32_t seq = row.seq, walks = 0, endedAt = 0;
    for (uint32_t now = 1000u; now <= 1000u + 3u * P::kWalkBackEveryMs && !endedAt; now += 1000u)
    {
        if (P::RowStayGone(true, stayActive, false))
        {
            endedAt = now;
            break;
        }
        if (now - row.walkMs >= P::kWalkBackEveryMs)
        {
            P::AfterWalkBack(row, ++seq, now);
            ++walks;
        }
    }
    CHECK(endedAt == 1000u);
    CHECK(walks == 0u);
    CHECK(row.seq == 11u);

    // The row a placement's `come` still stands on: the stay is active, so it is served as before.
    CHECK(!P::RowStayGone(true, true, false));
    // The row's own `come` is still waiting in the queue: its stay has not been written yet, so the engine being late is no lost hold.
    CHECK(!P::RowStayGone(true, false, true));
    CHECK(!P::RowStayGone(false, false, false));  // no engine to read the strategy from: nothing to end on
}

// #316 (2): a fall back's walk-back queued a `come` that the engine never ran. The walk back moved the movement number (`row.seq`), while the
// passive's record stays on the fall back's own `come`: the 10 s cap released with the new number, which found no record, answered "nothing to
// release" and cleared the row's flag with the bot still passive. The nominal test is the pure state: the owner never moves, and the flag only
// clears on a release that really ran.
static void TestWalkBackPassiveOwner()
{
    P::HoldRow row;
    row.seq = 7u;
    row.disengage = true;
    P::StartRowClock(row, 50000u);
    CHECK(row.passiveSeq == 7u);                  // the fall back's own `come` owns the passive
    P::AfterWalkBack(row, 9u, 55000u);            // the walk-back `come`, never run
    CHECK(row.seq == 9u);
    CHECK(row.passiveSeq == 7u);                  // the owner is unchanged by the walk back
    CHECK(P::PassiveOwner(row) == 7u);
    // The cap still fires on the fall back's own clock.
    CHECK(P::RowDisengageEnds(row, 8.0f, false, true, 50000u + P::kDisengageMaxMs));
    // The release ran with the owner's number: the row stops disengaging.
    CHECK(P::DisengageSettled(row, true));
    CHECK(!row.disengage && row.passiveSeq == 0u);

    // A release that could not run (the bot's engine is away) leaves the row to retry: no flag cleared over a passive still on the bot.
    P::HoldRow away;
    away.seq = 7u;
    away.disengage = true;
    P::StartRowClock(away, 1000u);
    P::AfterWalkBack(away, 9u, 6000u);
    CHECK(!P::DisengageSettled(away, false));
    CHECK(away.disengage && P::PassiveOwner(away) == 7u);
    CHECK(P::RowDisengageEnds(away, 8.0f, false, true, 1000u + P::kDisengageMaxMs));   // the next pass asks again
    CHECK(P::DisengageSettled(away, true));
    CHECK(!away.disengage && away.passiveSeq == 0u);
    // A row that is not a fall back owns no passive at all.
    P::HoldRow placed;
    placed.seq = 3u;
    P::StartRowClock(placed, 1000u);
    CHECK(placed.passiveSeq == 0u);
    CHECK(P::PassiveOwner(placed) == 3u);
}

static void TestExecutedWalkBackPassiveOwner()
{
    // #316 follow-up (review): the ordinary case is the walk back that RUNS. The engine re-queues the movement `come` under a new number and
    // executes it, so the record's owner must not move: the row and the order bookkeeping are driven here as the host drives them, the row
    // (TbPlacePlan.h) and the bot's record (PassiveBook, the functions TbOrders.cpp calls).
    P::HoldRow row;
    row.seq = 7u;
    row.disengage = true;
    P::StartRowClock(row, 1000u);
    P::PassiveBook book;
    // The fall back's own `come` runs on a bot that was not passive: it owns the passive it adds, and is the last one that set it.
    book.owner = P::RanComePassiveOwner(book, false, 7u);
    P::NoteRanPassive(book, "come", "", true, 7u);
    CHECK(book.owner == 7u && book.last == 7u);
    CHECK(book.owner == P::PassiveOwner(row));

    // 5 s later the walk back runs: a new order number, the same row's passive.
    P::AfterWalkBack(row, 12u, 6000u);
    book.owner = P::RanComePassiveOwner(book, true, 12u);
    P::NoteRanPassive(book, "come", "", true, 12u);
    CHECK(row.seq == 12u);
    CHECK(P::PassiveOwner(row) == 7u);
    CHECK(book.owner == 7u);                  // the walk-back `come` is not a newer owner
    CHECK(book.last == 7u);                   // ... and not a newer order that set `passive` itself
    CHECK(P::RowDisengageEnds(row, 8.0f, false, true, 1000u + P::kDisengageMaxMs));
    // The cap fires: the passive is dropped once, and the row settles.
    CHECK(P::TakeOwnerPassive(book, P::PassiveOwner(row)));
    CHECK(book.owner == 0u);
    CHECK(P::DisengageSettled(row, true));
    CHECK(!row.disengage && row.passiveSeq == 0u);
    CHECK(!P::TakeOwnerPassive(book, P::PassiveOwner(row)));   // a second pass drops nothing more

    // The walk back that stays queued: the same record, the same single release.
    P::HoldRow queued;
    queued.seq = 21u;
    queued.disengage = true;
    P::StartRowClock(queued, 1000u);
    P::PassiveBook qbook;
    qbook.owner = P::RanComePassiveOwner(qbook, false, 21u);
    P::NoteRanPassive(qbook, "come", "", true, 21u);
    P::AfterWalkBack(queued, 30u, 6000u);
    CHECK(P::TakeOwnerPassive(qbook, P::PassiveOwner(queued)));
    CHECK(P::DisengageSettled(queued, true));
    CHECK(!P::TakeOwnerPassive(qbook, 30u));   // asking with the walk-back's number drops nothing (and leaves nothing to drop)

    // No walk back at all: same.
    P::HoldRow plain;
    plain.seq = 41u;
    plain.disengage = true;
    P::StartRowClock(plain, 1000u);
    P::PassiveBook pbook;
    pbook.owner = P::RanComePassiveOwner(pbook, false, 41u);
    P::NoteRanPassive(pbook, "come", "", true, 41u);
    CHECK(P::TakeOwnerPassive(pbook, P::PassiveOwner(plain)));
    CHECK(P::DisengageSettled(plain, true));

    // A fall back on a bot the owner already held passive owns nothing: its walk back must not create a record either.
    P::HoldRow kept;
    kept.seq = 55u;
    kept.disengage = true;
    P::StartRowClock(kept, 1000u);
    P::PassiveBook kbook;
    kbook.owner = P::RanComePassiveOwner(kbook, true, 55u);
    P::NoteRanPassive(kbook, "come", "", true, 55u);
    CHECK(kbook.owner == 0u && kbook.last == 0u);
    P::AfterWalkBack(kept, 60u, 6000u);
    kbook.owner = P::RanComePassiveOwner(kbook, true, 60u);
    P::NoteRanPassive(kbook, "come", "", true, 60u);
    CHECK(kbook.owner == 0u);
    CHECK(!P::TakeOwnerPassive(kbook, P::PassiveOwner(kept)));
    CHECK(P::DisengageSettled(kept, true));    // the row served its record (none): the owner keeps the passive

    // A `flee` after the executed walk back takes the record over: the cap drops nothing, the bot keeps the flee's passive.
    P::HoldRow fleeRow;
    fleeRow.seq = 71u;
    fleeRow.disengage = true;
    P::StartRowClock(fleeRow, 1000u);
    P::PassiveBook fbook;
    fbook.owner = P::RanComePassiveOwner(fbook, false, 71u);
    P::NoteRanPassive(fbook, "come", "", true, 71u);
    P::AfterWalkBack(fleeRow, 80u, 6000u);
    fbook.owner = P::RanComePassiveOwner(fbook, true, 80u);
    P::NoteRanPassive(fbook, "come", "", true, 80u);
    P::NoteRanPassive(fbook, "flee", "", false, 90u);
    CHECK(fbook.owner == 0u && fbook.last == 90u);
    CHECK(!P::TakeOwnerPassive(fbook, P::PassiveOwner(fleeRow)));

    // A `hold` between the fall back and its walk back: the walk back runs without recreating a record the hold now owns.
    P::HoldRow heldRow;
    heldRow.seq = 101u;
    heldRow.disengage = true;
    P::StartRowClock(heldRow, 1000u);
    P::PassiveBook hbook;
    hbook.owner = P::RanComePassiveOwner(hbook, false, 101u);
    P::NoteRanPassive(hbook, "come", "", true, 101u);
    P::NoteRanPassive(hbook, "hold", "", false, 105u);
    CHECK(hbook.owner == 0u && hbook.last == 105u);
    P::AfterWalkBack(heldRow, 112u, 6000u);
    hbook.owner = P::RanComePassiveOwner(hbook, true, 112u);
    P::NoteRanPassive(hbook, "come", "", true, 112u);
    CHECK(hbook.owner == 0u);
    CHECK(!P::TakeOwnerPassive(hbook, P::PassiveOwner(heldRow)));
}

// #321 review: the order lives 15 s (OrderMaxAgeMs), the fall back's cap is 10 s, so a walk back can run AFTER the cap released the passive. The
// row is over then and nobody owns the passive: the queued `come` must move the bot and add nothing (before, it claimed the record with its own
// newer number and turned the bot passive for good). The same end comes through an owner's `co +passive`, `co -passive`: the record is no
// longer the row's token either.
struct FakeEngine
{
    bool passive = false;
    std::string events;                       // 'r' = asked for the passive now, 'a' = added it
    bool PassiveOn()
    {
        events.push_back('r');
        return passive;
    }
    void AddPassive()
    {
        events.push_back('a');
        passive = true;
    }
};

static void TestLateWalkBackIsNotPassive()
{
    P::HoldRow row;
    row.seq = 7u;
    row.disengage = true;
    P::StartRowClock(row, 1000u);
    P::PassiveBook book;
    book.owner = P::RanComePassiveOwner(book, false, 7u);
    P::NoteRanPassive(book, "come", "", true, 7u);
    P::AfterWalkBack(row, 30u, 9000u);            // the walk back queued and never run: the order waits
    // The cap fires at 10 s while the order still waits: the passive is dropped once and the row settles.
    CHECK(P::TakeOwnerPassive(book, P::PassiveOwner(row)));
    CHECK(P::DisengageSettled(row, true));
    CHECK(book.owner == 0u);
    // The walk back now runs (it may live to 15 s): the bot moves, no passive comes back, and nobody owns one.
    FakeEngine engine;
    P::ApplyComePassive(engine, &book, 30u, P::PassiveOwner(row));
    CHECK(!engine.passive);
    CHECK(engine.events.empty());                 // not even a look at the strategies: the row is over
    CHECK(book.owner == 0u);

    // The owner's own `co +passive`, then `co -passive`, then the walk back runs: passive stays off.
    P::HoldRow typed;
    typed.seq = 41u;
    typed.disengage = true;
    P::StartRowClock(typed, 1000u);
    P::PassiveBook tbook;
    tbook.owner = P::RanComePassiveOwner(tbook, false, 41u);
    P::NoteRanPassive(tbook, "come", "", true, 41u);
    P::NoteRanPassive(tbook, "co", "+passive", false, 42u);
    P::NoteRanPassive(tbook, "co", "-passive", false, 43u);
    CHECK(tbook.owner == 0u);
    FakeEngine typedEngine;
    P::ApplyComePassive(typedEngine, &tbook, 44u, P::PassiveOwner(typed));
    CHECK(!typedEngine.passive);
    CHECK(typedEngine.events.empty());
    CHECK(tbook.owner == 0u);

    // The ordinary walk back, before any cap: the token still owns the record, so the come keeps the passive and the record unchanged.
    P::HoldRow early;
    early.seq = 51u;
    early.disengage = true;
    P::StartRowClock(early, 1000u);
    P::PassiveBook ebook;
    ebook.owner = P::RanComePassiveOwner(ebook, false, 51u);
    P::NoteRanPassive(ebook, "come", "", true, 51u);
    FakeEngine earlyEngine;
    earlyEngine.passive = true;
    P::ApplyComePassive(earlyEngine, &ebook, 60u, P::PassiveOwner(early));
    CHECK(earlyEngine.passive);
    CHECK(ebook.owner == 51u);                    // the walk back's newer number never takes the record over
}

// #321 review follow-up: the come reads the strategies BEFORE it adds `passive`. Read after the three `+passive` calls, every come would see
// "already passive", claim nothing and leave a passive nobody releases; the read is inside the helper so this order is testable.
static void TestComePassiveReadsBeforeAdding()
{
    FakeEngine engine;                            // not passive
    P::PassiveBook book;
    P::ApplyComePassive(engine, &book, 11u, 0u);
    CHECK(engine.events == "ra");                 // asked first, added second
    CHECK(engine.passive && book.owner == 11u);   // it added the passive: it owns it

    FakeEngine held;                              // already passive (a `hold`, a `flee`, an owner's `+passive`)
    held.passive = true;
    P::PassiveBook hbook;
    P::ApplyComePassive(held, &hbook, 12u, 0u);
    CHECK(held.events == "ra");
    CHECK(hbook.owner == 0u);                     // the owner keeps that passive; the come claims nothing

    FakeEngine bare;                              // no order record: the passive is still added, just never managed
    P::ApplyComePassive(bare, nullptr, 13u, 0u);
    CHECK(bare.events == "ra" && bare.passive);
}

static void TestLedgeSlotIsRefused()
{
    P::PlanResult const base = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, FlatWorld());
    CHECK(base.ok && base.placed == 4);
    P::Vec2 const slot2 = base.steps[2].pos;
    P::Probe probe = FlatWorld();
    // A floor 20 yd down under slot 2 and every nudge towards the centre.
    probe.ground = [slot2](P::Vec2 const& point, float& z)
    {
        z = Dist(point, slot2) <= 3.5f ? -20.0f : 0.0f;
        return true;
    };
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, probe);
    CHECK(plan.ok);
    P::Step const* mage = StepOfBot(plan, 2);
    CHECK(mage && !mage->placed && mage->code == "ledge");
    CHECK(plan.placed == 3 && plan.refused == 1);
    for (P::Step const& step : plan.steps)
        if (step.bot != 2)
            CHECK(step.placed);
}

static void TestLiquidSlotIsNudged()
{
    P::PlanResult const base = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, FlatWorld());
    P::Vec2 const slot1 = base.steps[1].pos;
    P::Probe probe = FlatWorld();
    probe.liquid = [slot1](P::Vec2 const& point, float) { return Dist(point, slot1) <= 0.6f; };
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, probe);
    P::Step const* rogue = StepOfBot(plan, 1);
    CHECK(rogue && rogue->placed && std::fabs(rogue->nudged - 1.0f) < 1e-3f);
    CHECK(Dist(rogue->pos, slot1) > 0.5f);                    // moved off the wet point
    CHECK(Dist(rogue->pos, base.frame.centre) < Dist(slot1, base.frame.centre));   // inward

    P::Probe wet = FlatWorld();
    wet.liquid = [](P::Vec2 const&, float) { return true; };
    P::PlanResult const allWet = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, wet);
    P::Step const* refused = StepOfBot(allWet, 1);
    CHECK(refused && !refused->placed && refused->code == "liquid");
}

static void TestNudgeGoesInward()
{
    P::PlanResult const base = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, FlatWorld());
    P::Vec2 const slot1 = base.steps[1].pos;
    std::vector<P::Vec2> asked;
    P::Probe probe = FlatWorld();
    probe.liquid = [&asked, slot1](P::Vec2 const& point, float)
    {
        if (Dist(point, slot1) <= 4.0f)
            asked.push_back(point);
        return false;
    };
    probe.reachable = [slot1](size_t bot, P::Vec2 const& point, float)
    {
        // The bot cannot walk to the slot or its 1 yd nudge; the 2 yd nudge is reachable.
        if (bot == 1 && Dist(point, slot1) <= 1.5f)
            return P::PathVerdict::NoPath;
        return P::PathVerdict::Ok;
    };
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, probe);
    P::Step const* rogue = StepOfBot(plan, 1);
    CHECK(rogue && rogue->placed && std::fabs(rogue->nudged - 2.0f) < 1e-3f);
    // The candidates are asked in order, each one step nearer the frame centre.
    CHECK(asked.size() >= 3);
    if (asked.size() >= 3)
    {
        float const d0 = Dist(asked[0], base.frame.centre);
        float const d1 = Dist(asked[1], base.frame.centre);
        float const d2 = Dist(asked[2], base.frame.centre);
        CHECK(d1 < d0 && d2 < d1);
    }
}

static void TestPathVerdicts()
{
    CHECK(!P::IsDetour(34.9f, 10.0f));
    CHECK(P::IsDetour(35.1f, 10.0f));
    P::PlanResult const base = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, FlatWorld());
    P::Vec2 const slot1 = base.steps[1].pos;
    struct Case { P::PathVerdict verdict; char const* code; };
    for (Case const& c : { Case{ P::PathVerdict::NoPath, "unreachable" },
                           Case{ P::PathVerdict::LiquidPath, "liquid-path" },
                           Case{ P::PathVerdict::Detour, "detour" } })
    {
        P::Probe probe = FlatWorld();
        probe.reachable = [slot1, c](size_t bot, P::Vec2 const& point, float)
        {
            return bot == 1 && Dist(point, slot1) <= 4.0f ? c.verdict : P::PathVerdict::Ok;
        };
        P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
            TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), Party(), {}, probe);
        P::Step const* rogue = StepOfBot(plan, 1);
        CHECK(rogue && !rogue->placed && rogue->code == c.code);
    }
}

static void TestTankExcluded()
{
    // The plan itself removes the anchor tank from a party that includes it: tanks-only leaves nobody, so the command is no-bots.
    std::vector<P::BotFacts> const party = Party();
    P::WhoToken tanks;
    P::ParseWho("tanks", tanks);
    P::PlanResult const empty = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), party, { tanks }, FlatWorld(), "Mtank");
    CHECK(!empty.ok && empty.code == "no-bots");

    // Naming the tank alone is the same: the tank is a bot the words may name (never bad-target), but it takes no slot.
    P::WhoToken byName;
    P::ParseWho("Mtank", byName);
    P::PlanResult const named = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), party, { byName }, FlatWorld(), "Mtank");
    CHECK(!named.ok && named.code == "no-bots");

    // `all` leaves the three other bots; there is no step for the tank.
    P::WhoToken all;
    P::ParseWho("all", all);
    P::PlanResult const rest = P::PlanPlace(P::Shape::Circle, P::Gap::Normal,
        TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 2.0f), party, { all }, FlatWorld(), "Mtank");
    CHECK(rest.ok && rest.selected == 3 && rest.steps.size() == 3 && rest.placed == 3);
    for (P::Step const& step : rest.steps)
        CHECK(step.bot != 0);
}

static void TestFallbackRefusedStillDisengages()
{
    P::PlanResult const base = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 0.0f, 1.0f }, true,
        Party(), FlatWorld());
    P::Vec2 const slot2 = StepOfBot(base, 2)->pos;   // a ranged slot on the arc: the centre is more than a nudge away
    P::Probe blocked = FlatWorld();
    blocked.reachable = [slot2](size_t bot, P::Vec2 const& point, float)
    {
        return bot == 2 && Dist(point, slot2) <= 3.5f ? P::PathVerdict::NoPath : P::PathVerdict::Ok;
    };
    P::PlanResult const plan = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 0.0f, 1.0f }, true,
        Party(), blocked);
    CHECK(plan.ok && plan.placed == 4 && plan.refused == 0);
    P::Step const* mage = StepOfBot(plan, 2);
    CHECK(mage && mage->placed && mage->slot == -1 && mage->to == "centre" && mage->code == "unreachable");
    CHECK(Dist(mage->pos, { 0.0f, 0.0f }) < 1e-3f);

    // The centre itself is refused: the bot goes to the commander's own position.
    P::Probe wetCentre = blocked;
    wetCentre.liquid = [](P::Vec2 const& point, float) { return Dist(point, { 0.0f, 0.0f }) < 1.0f; };
    P::PlanResult const toCommander = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 0.0f, 1.0f }, true,
        Party(), wetCentre);
    P::Step const* hero = StepOfBot(toCommander, 2);
    CHECK(hero && hero->placed && hero->to == "commander" && hero->slot == -1);
    CHECK(std::fabs(hero->pos.y - 20.0f) < 1e-3f);

    // A dead bot is not queued at all.
    std::vector<P::BotFacts> bots = Party();
    bots[1].alive = false;
    P::PlanResult const withDead = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 0.0f, 1.0f }, true,
        bots, FlatWorld());
    P::Step const* dead = StepOfBot(withDead, 1);
    CHECK(dead && !dead->placed && dead->code == "dead");
}

static void TestRallyAndFallbackWords()
{
    P::Command c = P::ParseCommand({ "rally", "clear" });
    CHECK(c.kind == P::CommandKind::RallyClear && c.rallyWord == P::RallyWord::Clear && c.code.empty());
    c = P::ParseCommand({ "rally", "at:10,20" });
    CHECK(c.kind == P::CommandKind::RallyAt && c.rallyWord == P::RallyWord::At && c.anchor == P::Anchor::Here);
    CHECK(std::fabs(c.point.x - 10.0f) < 1e-4f && std::fabs(c.point.y - 20.0f) < 1e-4f && !c.pointHasZ);
    c = P::ParseCommand({ "rally", "at:1,2,3" });
    CHECK(c.kind == P::CommandKind::RallyAt && c.pointHasZ && std::fabs(c.pointZ - 3.0f) < 1e-4f);
    c = P::ParseCommand({ "rally", "here", "there" });
    CHECK(c.code == "unexpected-argument");
    c = P::ParseCommand({ "rally", "nowhere" });
    CHECK(c.code == "bad-argument");
    CHECK(P::ParseCommand({ "rally", "at:10" }).code == "bad-argument");

    c = P::ParseCommand({ "fallback", "last" });
    CHECK(c.kind == P::CommandKind::Fallback && c.fallbackTo == P::FallbackTo::Rally);
    c = P::ParseCommand({ "fallback", "rally" });
    CHECK(c.fallbackTo == P::FallbackTo::Rally);
    c = P::ParseCommand({ "fall", "back", "here" });
    CHECK(c.fallbackTo == P::FallbackTo::Here);
    c = P::ParseCommand({ "fallback", "at:5,6" });
    CHECK(c.fallbackTo == P::FallbackTo::At && std::fabs(c.point.y - 6.0f) < 1e-4f);
    c = P::ParseCommand({ "back", "here" });
    CHECK(c.code == "unexpected-argument");                    // back takes no option
    c = P::ParseCommand({ "fallback", "last", "now" });
    CHECK(c.code == "unexpected-argument");

    // Round-1 words still parse identically.
    c = P::ParseCommand({ "place", "circle" });
    CHECK(c.kind == P::CommandKind::Place && c.shape == P::Shape::Circle && c.anchor == P::Anchor::Here
        && c.gap == P::Gap::Normal && c.code.empty());
    c = P::ParseCommand({ "place", "follow", "Mtank" });
    CHECK(c.kind == P::CommandKind::PlaceFollow && c.who.size() == 1);
    c = P::ParseCommand({ "fall", "back" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code.empty());
    c = P::ParseCommand({ "back" });
    CHECK(c.kind == P::CommandKind::Fallback && c.code.empty());
    c = P::ParseCommand({ "rally", "here" });
    CHECK(c.kind == P::CommandKind::RallyHere);
    c = P::ParseCommand({ "resume" });
    CHECK(c.kind == P::CommandKind::Resume);

    // Where the fall back forms: `last` never falls through to the commander's position.
    P::Vec2 const storedPos{ 5.0f, 6.0f };
    P::Vec2 const commander{ 0.0f, 20.0f };
    P::FallbackChoice const none = P::ChooseFallback(P::FallbackTo::Rally, false, storedPos, 1.0f, commander, 2.0f, { 7.0f, 8.0f }, 3.0f);
    CHECK(!none.ok && none.code == "no-rally");
    P::FallbackChoice const memory = P::ChooseFallback(P::FallbackTo::Rally, true, storedPos, 1.0f, commander, 2.0f, { 7.0f, 8.0f }, 3.0f);
    CHECK(memory.ok && memory.from == "memory" && Dist(memory.centre, storedPos) < 1e-4f && memory.frontStored);
    P::FallbackChoice const fallback = P::ChooseFallback(P::FallbackTo::Default, false, storedPos, 1.0f, commander, 2.0f, {}, 0.0f);
    CHECK(fallback.ok && fallback.from == "commander" && Dist(fallback.centre, commander) < 1e-4f);
    P::FallbackChoice const here = P::ChooseFallback(P::FallbackTo::Here, true, storedPos, 1.0f, commander, 2.0f, {}, 0.0f);
    CHECK(here.ok && here.from == "commander" && !here.frontStored);
    P::FallbackChoice const at = P::ChooseFallback(P::FallbackTo::At, false, storedPos, 1.0f, commander, 2.0f, { 7.0f, 8.0f }, 3.0f);
    CHECK(at.ok && at.from == "point" && std::fabs(at.z - 3.0f) < 1e-4f);

    CHECK(P::ChatWordsAreCommand({ "rally" }));
    CHECK(P::ChatWordsAreCommand({ "rally", "clear" }));
    CHECK(!P::ChatWordsAreCommand({ "rally", "at:1,2" }));
    CHECK(!P::ChatWordsAreCommand({ "fallback", "here" }));
}

// Both fall-back destinations are judged before a bot is sent there: an unstandable centre AND an unstandable commander
// leave the bot refused instead of ordering it somewhere the feature promised to avoid.
static void TestFallbackCommanderValidated()
{
    P::Probe probe = FlatWorld();
    // Everything within 30 yd of the centre is wet (the centre, every slot and every nudge).
    probe.liquid = [](P::Vec2 const& point, float) { return Dist(point, { 0.0f, 0.0f }) < 30.0f; };
    // The commander's own spot has no floor.
    probe.ground = [](P::Vec2 const& point, float& z)
    {
        if (Dist(point, { 0.0f, 20.0f }) < 1.0f)
            return false;
        z = 0.0f;
        return true;
    };
    P::PlanResult const plan = P::PlanFallback(P::Gap::Normal, { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 0.0f, 1.0f }, true,
        Party(), probe);
    CHECK(plan.ok);
    CHECK(plan.placed == 0 && plan.refused == 4);
    for (P::Step const& step : plan.steps)
    {
        CHECK(!step.placed && step.to.empty());
        CHECK(step.code == "liquid" || step.code == "no-ground");
    }
}

// A ring nudge towards the target may not walk a bot inside the target's own combat reach.
static void TestTargetNudgeGuard()
{
    P::AnchorFacts anchor = TargetAnchor({ 0.0f, 10.0f }, { 0.0f, 0.0f }, 1.0f);
    std::vector<P::BotFacts> const one{ { "Mtank", "warrior", 1, P::Rank::Tank } };
    P::GapSpec gap;
    gap.preset = P::Gap::Normal;
    gap.yd = 1.5f;
    gap.custom = true;
    P::PlanResult const base = P::PlanPlace(P::Shape::Circle, gap, anchor, one, {}, FlatWorld());
    CHECK(base.ok && base.placed == 1);
    P::Vec2 const slot = base.steps[0].pos;
    CHECK(Dist(slot, { 0.0f, 0.0f }) >= 1.0f);
    P::Probe probe = FlatWorld();
    // The slot and its 1 yd nudge are wet; the 2 yd nudge is dry but would be inside the target (0.5 yd from its centre).
    probe.liquid = [slot](P::Vec2 const& point, float) { return Dist(point, slot) < 1.2f; };
    P::PlanResult const plan = P::PlanPlace(P::Shape::Circle, gap, anchor, one, {}, probe);
    P::Step const* tank = StepOfBot(plan, 0);
    CHECK(tank && !tank->placed && tank->code == "liquid");
}

// `rally at:` decides the point before any state changes: a refused point stores nothing and never relabels an older one.
static void TestRallyDecision()
{
    P::State state;
    P::RallyState old;
    old.set = true;
    old.x = 5.0f; old.y = 6.0f; old.z = 7.0f;
    old.map = 1; old.instance = 2;
    state.rallies[9] = old;

    bool asked = false;
    auto refusedAt = [&asked](P::Vec2&, float&, std::string& code, std::string& detail)
    {
        asked = true;
        code = "liquid";
        detail = "the point is in water or lava";
        return false;
    };
    P::RallyDecision const refused = P::DecideRally(P::RallyWord::At, { 1.0f, 2.0f }, 3.0f, 8, 9, { 10.0f, 20.0f }, refusedAt);
    CHECK(asked && !refused.ok && refused.code == "liquid" && !refused.value.set);
    CHECK(state.rallies[9].x == 5.0f && state.rallies[9].map == 1);      // the decision touched nothing

    auto goodAt = [](P::Vec2& point, float& z, std::string&, std::string&)
    {
        point.x = 30.0f;
        point.y = 40.0f;
        z = 50.0f;
        return true;
    };
    P::RallyDecision const at = P::DecideRally(P::RallyWord::At, { 1.0f, 2.0f }, 3.0f, 8, 9, { 10.0f, 20.0f }, goodAt);
    CHECK(at.ok && at.value.set && at.value.x == 30.0f && at.value.y == 40.0f && std::fabs(at.value.z - 50.0f) < 1e-4f);
    CHECK(at.value.map == 8 && at.value.instance == 9);

    P::RallyDecision const here = P::DecideRally(P::RallyWord::Here, { 1.0f, 2.0f }, 3.0f, 8, 9, {}, nullptr);
    CHECK(here.ok && here.value.x == 1.0f && here.value.y == 2.0f && std::fabs(here.value.z - 3.0f) < 1e-4f);
}

// A stored rally point only applies on its own map and instance; another map is no point at all.
static void TestStoredRallyFilter()
{
    P::State state;
    P::RallyState rally;
    rally.set = true;
    rally.x = 1.0f; rally.y = 2.0f; rally.z = 3.0f;
    rally.map = 1; rally.instance = 2;
    state.rallies[9] = rally;
    CHECK(P::StoredRallyFor(state, 9, 1, 2) != nullptr);
    CHECK(P::StoredRallyFor(state, 9, 1, 3) == nullptr);      // same map, another instance
    CHECK(P::StoredRallyFor(state, 9, 0, 2) == nullptr);      // another map
    CHECK(P::StoredRallyFor(state, 10, 1, 2) == nullptr);     // another commander
    state.rallies[9].set = false;
    CHECK(P::StoredRallyFor(state, 9, 1, 2) == nullptr);
}

static void TestCardHoldKeys()
{
    P::HoldView view;
    view.any = true;
    view.fallback = false;
    view.slots.push_back({ { 0, 11 }, { 1.0f, 2.0f, 3.0f } });
    view.slots.push_back({ { 0, 12 }, { 4.0f, 5.0f, 6.0f } });
    P::CardKeys const keys = P::CardHoldKeys(view);
    CHECK(keys.hold == "place");
    CHECK(keys.slots.size() == 2);
    P::Vec3f slot;
    CHECK(keys.SlotOf({ 0, 12 }, slot) && std::fabs(slot.z - 6.0f) < 1e-4f);
    CHECK(!keys.SlotOf({ 0, 13 }, slot));
    CHECK(!keys.hasRally);

    P::HoldView empty;
    P::CardKeys const none = P::CardHoldKeys(empty);
    CHECK(none.hold.empty() && none.slots.empty() && !none.hasRally);

    P::HoldView fallback;
    fallback.any = true;
    fallback.fallback = true;
    fallback.slots.push_back({ { 0, 11 }, { 0.0f, 0.0f, 0.0f } });
    fallback.rallySet = true;
    fallback.x = 10.0f; fallback.y = 20.0f; fallback.z = 30.0f;
    fallback.map = 1; fallback.inst = 2;
    P::CardKeys const held = P::CardHoldKeys(fallback);
    CHECK(held.hold == "fallback");
    CHECK(held.hasRally && std::fabs(held.rally.y - 20.0f) < 1e-4f && held.rmap == 1 && held.rinst == 2);
}

static void TestComePolicy()
{
    // A run's walk, a rally and a plain `come` are unchanged: the engine's per-fight state is reset, nothing holds the bot passive, and the
    // combat state keeps its `stay`, so the stay strategy's "return to stay position" still answers a pull.
    P::ComePlan const runWalk = P::PlanCome(false, false, true);
    CHECK(runWalk.reset && !runWalk.stopFight && !runWalk.passive && !runWalk.keepCombatStayOut);
    P::ComePlan const plain = P::PlanCome(false, false, false);
    CHECK(plain.reset && !plain.keepCombatStayOut);

    // A placement in a fight keeps its target (no reset) and keeps `stay` out of the combat state; out of combat it resets as usual.
    P::ComePlan const placedFight = P::PlanCome(true, false, true);
    CHECK(!placedFight.reset && placedFight.keepCombatStayOut && !placedFight.stopFight);
    P::ComePlan const placedIdle = P::PlanCome(true, false, false);
    CHECK(placedIdle.reset && placedIdle.keepCombatStayOut);

    // A fall back stops the fight and holds the bot passive; it is not a placement.
    P::ComePlan const fallback = P::PlanCome(false, true, true);
    CHECK(fallback.reset && fallback.stopFight && fallback.passive && !fallback.keepCombatStayOut);
}

static void TestDisengageEnds()
{
    // Arrival always ends the hold.
    CHECK(P::DisengageEnds(P::kSlotToleranceYd - 0.1f, false, false, 0));
    // A bot that stops short in a fight ends it after the grace; one still walking does not.
    CHECK(P::DisengageEnds(10.0f, true, true, P::kDisengageStoppedShortMs));
    CHECK(!P::DisengageEnds(10.0f, true, false, P::kDisengageStoppedShortMs));
    CHECK(!P::DisengageEnds(10.0f, true, true, P::kDisengageStoppedShortMs - 1));
    // The cap ends it even for a bot that never stops walking, and out of combat too.
    CHECK(P::DisengageEnds(10.0f, true, false, P::kDisengageMaxMs));
    CHECK(P::DisengageEnds(10.0f, false, true, P::kDisengageMaxMs));
    CHECK(!P::DisengageEnds(10.0f, true, false, P::kDisengageMaxMs - 1));
}

// F1b (live 11-24, #259): a plain fall back out of combat whose bot stands still off its slot (it does not walk there, or it is blocked)
// must still end at the cap. place::Update asks once a second; the walk back re-queues the row's `come` every kWalkBackEveryMs while the
// bot is idle off its slot. The cap counts from the fall back's start: a walk back (or a failed path check) does not restart it, or the
// passive would never end (the live row: still passive at 17 s and 40 s, `hold=fallback` on every row, only `resume` cleared it).
static void TestFallbackCapSurvivesWalkBacks()
{
    P::HoldRow row;
    row.seq = 7u;
    row.disengage = true;
    uint32_t const start = 50000u;
    P::StartRowClock(row, start);
    uint32_t seq = row.seq, endedAt = 0;
    for (uint32_t now = start; now <= start + 40000u && !endedAt; now += 1000u)
    {
        // Out of combat, 8 yd off the slot, not moving (Update's `idle`).
        if (P::RowDisengageEnds(row, 8.0f, false, true, now))
        {
            endedAt = now;
            row.disengage = false;
        }
        if (now - row.walkMs >= P::kWalkBackEveryMs)
            P::AfterWalkBack(row, ++seq, now);
    }
    CHECK(endedAt != 0u);
    CHECK(endedAt - start == P::kDisengageMaxMs);
    // The walk backs still ran on their own clock (they serve the slot after the fall back's passive ends).
    CHECK(seq > 7u);

    // A failed path check rests the walk back (Update writes row.walkMs = now): the cap is still the fall back's own.
    P::HoldRow blocked;
    blocked.disengage = true;
    P::StartRowClock(blocked, 1000u);
    blocked.walkMs = 9000u;
    CHECK(P::RowDisengageEnds(blocked, 8.0f, false, true, 1000u + P::kDisengageMaxMs));
    CHECK(!P::RowDisengageEnds(blocked, 8.0f, false, true, 1000u + P::kDisengageMaxMs - 1u));
    // The stopped-short grace in a fight counts from the start too, and a row that is not a fall back never "ends" a disengage.
    CHECK(P::RowDisengageEnds(blocked, 8.0f, true, true, 1000u + P::kDisengageStoppedShortMs));
    blocked.disengage = false;
    CHECK(!P::RowDisengageEnds(blocked, 1.0f, false, true, 1000u + P::kDisengageMaxMs));
    // Arrival ends it at once.
    P::HoldRow there;
    there.disengage = true;
    P::StartRowClock(there, 1000u);
    CHECK(P::RowDisengageEnds(there, P::kSlotToleranceYd - 0.1f, false, true, 1000u));
}

static void TestFallbackPassiveOwnership()
{
    // F1 (cold review): a fall back's `come` owns the passive only when it added it. A bot the owner already held passive keeps that
    // passive when the fall back ends; the row's own walk-back `come` carries the record on.
    CHECK(P::PlaceClaimsPassive(false, false));     // a fall back on a bot that was not passive: the row owns what it adds
    CHECK(!P::PlaceClaimsPassive(true, false));     // an owner's `+passive` (or a hold/flee) was already on: nothing to own
    CHECK(P::PlaceClaimsPassive(true, true));       // the row's walk-back: the record it already has carries over
    CHECK(P::PlaceClaimsPassive(false, true));

    // The orders that set `passive` themselves: `hold`, `flee` (its chat shortcut sets it in both states), an owner's `co/nc +passive`, and
    // a fall back's own `come` (the disengage one). A plain `come`, a `co -passive` or a `co +follow` does not.
    CHECK(P::OrderSetsPassive("hold", "", false));
    CHECK(P::OrderSetsPassive("flee", "", false));
    CHECK(P::OrderSetsPassive("co", "+passive", false));
    CHECK(P::OrderSetsPassive("nc", "+passive", false));
    CHECK(!P::OrderSetsPassive("co", "-passive", false));
    CHECK(!P::OrderSetsPassive("co", "+follow", false));
    CHECK(P::OrderSetsPassive("come", "", true));
    CHECK(!P::OrderSetsPassive("come", "", false));
    CHECK(!P::OrderSetsPassive("attack", "", false));
    CHECK(!P::OrderSetsPassive("follow", "", false));

    // The number the order record keeps for a ran order: its seq when it sets passive itself, else none (0).
    auto const lastPassive = [](char const* verb, char const* arg, bool disengage, uint32_t seq) -> uint32_t
    {
        return P::OrderSetsPassive(verb, arg, disengage) ? seq : 0u;
    };

    // Fall back then flee: at the cap (the bot is still walking) the hold ends, but the passive is the flee's own and stays.
    CHECK(P::DisengageEnds(10.0f, false, false, P::kDisengageMaxMs));
    CHECK(!P::PlacePassiveReleasable(true, 7, lastPassive("flee", "", false, 9)));
    // Fall back then hold: same.
    CHECK(P::DisengageEnds(1.0f, true, true, P::kDisengageStoppedShortMs));
    CHECK(!P::PlacePassiveReleasable(true, 7, lastPassive("hold", "", false, 9)));
    // `.tb order <bot> co +passive` after the fall back: same; a `co -passive` of the same shape never blocks the row's own release.
    CHECK(!P::PlacePassiveReleasable(true, 7, lastPassive("co", "+passive", false, 9)));
    CHECK(P::PlacePassiveReleasable(true, 7, lastPassive("co", "-passive", false, 9)));

    // The owner's passive before the fall back is never the row's, whatever order ran last.
    CHECK(!P::PlacePassiveReleasable(false, 7, 7));
    CHECK(!P::PlacePassiveReleasable(false, 7, 9));

    // A fall back alone releases its own passive on every exit path: the row's `come` is its last order (the walk-back keeps the row's
    // number), and an older passive-setting order (a hold before the fall back) does not block it.
    CHECK(P::PlacePassiveReleasable(true, 7, lastPassive("come", "", true, 7)));
    CHECK(P::PlacePassiveReleasable(true, 7, lastPassive("hold", "", false, 4)));
    CHECK(P::PlacePassiveReleasable(true, 9, lastPassive("come", "", true, 9)));   // after a walk-back (a new seq)
    CHECK(P::PlacePassiveReleasable(true, 7, 0));                                   // no passive-setting order ran at all
    // A newer order that does NOT set passive leaves the release to the row (attack, follow, free, a plain come).
    for (char const* verb : { "attack", "follow", "free", "stay", "stop" })
        CHECK(P::PlacePassiveReleasable(true, 7, lastPassive(verb, "", false, 9)));
    CHECK(P::PlacePassiveReleasable(true, 7, lastPassive("come", "", false, 9)));
}

int main()
{
    TestCommandWords();
    TestPlaceCircle();
    TestPlaceRefusalsArePerBot();
    TestCommanderSpot();
    TestFallbackFront();
    TestComePolicy();
    TestDisengageEnds();
    TestFallbackCapSurvivesWalkBacks();
    TestFallbackPassiveOwnership();
    TestHoldBookkeeping();
    TestClock();
    TestWalkBackKeepsTheRowsOrder();
    TestDroppedFollowYieldsToNewerOrder();
    TestWhisperFollowEndsTheHold();
    TestWalkBackPassiveOwner();
    TestExecutedWalkBackPassiveOwner();
    TestLateWalkBackIsNotPassive();
    TestComePassiveReadsBeforeAdding();
    TestLedgeSlotIsRefused();
    TestLiquidSlotIsNudged();
    TestNudgeGoesInward();
    TestPathVerdicts();
    TestTankExcluded();
    TestFallbackRefusedStillDisengages();
    TestRallyAndFallbackWords();
    TestTargetNudgeGuard();
    TestFallbackCommanderValidated();
    TestRallyDecision();
    TestStoredRallyFilter();
    TestCardHoldKeys();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}
