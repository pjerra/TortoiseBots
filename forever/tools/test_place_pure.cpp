// Standalone test of the PURE half of the place/rally command (forever/src/host/cmd/TbPlacePure.h; issue #15, forever/docs/PLACE-RALLY-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_place_pure.cpp -o /tmp/test_place_pure && /tmp/test_place_pure
// Every shape, the gap and ring rules, the slot order, the anchor frames, the who-filter words and the minimap-ping layout at the pinned core.
#include "forever/src/host/cmd/TbPlacePure.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace P = TortoiseBots::place;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static float Dist(P::Vec2 a, P::Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

static void TestWords()
{
    // The reply vocabulary and the parser are the same words: a client that reads `shape=circle` can type it back.
    CHECK(std::string(P::ShapeWord(P::Shape::Close)) == "close");
    CHECK(std::string(P::ShapeWord(P::Shape::Circle)) == "circle");
    CHECK(std::string(P::ShapeWord(P::Shape::Arc)) == "arc");
    CHECK(std::string(P::ShapeWord(P::Shape::Spread)) == "spread");
    CHECK(std::string(P::ShapeWord(P::Shape::Line)) == "line");
    CHECK(std::string(P::ShapeWord(P::Shape::Column)) == "column");
    CHECK(std::string(P::ShapeWord(P::Shape::Spear)) == "spear");
    CHECK(std::string(P::ShapeWord(P::Shape::Arrow)) == "arrow");
    CHECK(std::string(P::GapWord(P::Gap::Tight)) == "tight");
    CHECK(std::string(P::GapWord(P::Gap::Normal)) == "normal");
    CHECK(std::string(P::GapWord(P::Gap::Wide)) == "wide");
    CHECK(std::string(P::AnchorWord(P::Anchor::Here)) == "here");
    CHECK(std::string(P::AnchorWord(P::Anchor::Target)) == "target");
    CHECK(std::string(P::AnchorWord(P::Anchor::Cursor)) == "cursor");
    CHECK(std::string(P::RankWord(P::Rank::Tank)) == "tank");
    CHECK(std::string(P::RankWord(P::Rank::Melee)) == "melee");
    CHECK(std::string(P::RankWord(P::Rank::Ranged)) == "ranged");
    CHECK(std::string(P::RankWord(P::Rank::Healer)) == "healer");
    P::Shape s;
    for (char const* word : { "close", "circle", "arc", "spread", "line", "column", "spear", "arrow" })
    {
        CHECK(P::ParseShape(word, s));
        CHECK(std::string(P::ShapeWord(s)) == word);
    }
    CHECK(!P::ParseShape("bogus", s));
    CHECK(!P::ParseShape("", s));
    P::Gap g;
    CHECK(P::ParseGap("WIDE", g) && g == P::Gap::Wide);
    CHECK(!P::ParseGap("huge", g));
    P::Anchor a;
    CHECK(P::ParseAnchor("Cursor", a) && a == P::Anchor::Cursor);
    CHECK(!P::ParseAnchor("there", a));
    // The issue's gaps: tight 2.5 yd, normal 4 yd, wide 8 yd.
    CHECK(std::fabs(P::GapYd(P::Gap::Tight) - 2.5f) < 1e-4f);
    CHECK(std::fabs(P::GapYd(P::Gap::Normal) - 4.0f) < 1e-4f);
    CHECK(std::fabs(P::GapYd(P::Gap::Wide) - 8.0f) < 1e-4f);
}

static float PairwiseMin(std::vector<P::Slot> const& slots)
{
    float best = 1e9f;
    for (size_t i = 0; i < slots.size(); ++i)
        for (size_t j = i + 1; j < slots.size(); ++j)
            best = std::min(best, Dist({ slots[i].x, slots[i].y }, { slots[j].x, slots[j].y }));
    return slots.size() < 2 ? 0.0f : best;
}

static void TestLayouts()
{
    P::Shape const shapes[] = { P::Shape::Close, P::Shape::Circle, P::Shape::Arc, P::Shape::Spread,
        P::Shape::Line, P::Shape::Column, P::Shape::Spear, P::Shape::Arrow };
    float const gap = P::GapYd(P::Gap::Normal);
    for (P::Shape shape : shapes)
    {
        // Every shape answers exactly one slot per bot and never farther than the 35 yd ceiling.
        for (int count = 1; count <= 10; ++count)
        {
            P::Layout const l = P::LayoutShape(shape, P::Gap::Normal, count, 0.0f);
            CHECK(int(l.slots.size()) == count);
            CHECK(!l.tooFar);
            for (P::Slot const& slot : l.slots)
            {
                CHECK(std::sqrt(slot.x * slot.x + slot.y * slot.y) <= P::kMaxRadiusYd + 1e-3f);
                CHECK(std::isfinite(slot.x) && std::isfinite(slot.y));
            }
            // No two bots on one spot.
            if (count > 1)
                CHECK(PairwiseMin(l.slots) > 0.1f);
            // extent is the farthest slot from the origin.
            float extent = 0.0f;
            for (P::Slot const& slot : l.slots)
                extent = std::max(extent, std::sqrt(slot.x * slot.x + slot.y * slot.y));
            CHECK(std::fabs(l.extent - extent) < 1e-3f);
        }
    }

    // Slot order: the front slots come first (tanks front), and the row shapes only ever grow backwards.
    for (P::Shape shape : { P::Shape::Close, P::Shape::Spread, P::Shape::Column, P::Shape::Spear, P::Shape::Arrow })
    {
        P::Layout const l = P::LayoutShape(shape, P::Gap::Normal, 6, 0.0f);
        for (size_t i = 1; i < l.slots.size(); ++i)
            CHECK(l.slots[i].y <= l.slots[i - 1].y + 1e-3f);
    }

    // close: a block two wide, rows one gap apart.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Close, P::Gap::Normal, 5, 0.0f);
        CHECK(std::fabs(l.slots[0].y - l.slots[1].y) < 1e-3f);       // first two share the front row
        CHECK(std::fabs(std::fabs(l.slots[0].x - l.slots[1].x) - gap) < 1e-2f);
        CHECK(std::fabs(std::fabs(l.slots[0].y - l.slots[2].y) - gap) < 1e-2f);
    }
    // circle: every bot on one ring, the first at the front, the ring never smaller than a target or a gap.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Circle, P::Gap::Normal, 5, 0.0f);
        CHECK(l.radius >= gap - 1e-3f);
        for (P::Slot const& slot : l.slots)
            CHECK(std::fabs(std::sqrt(slot.x * slot.x + slot.y * slot.y) - l.radius) < 1e-2f);
        CHECK(std::fabs(l.slots[0].x) < 1e-3f && l.slots[0].y > 0.0f);
        CHECK(l.slots[1].x > 0.0f && l.slots[1].y < l.slots[0].y);   // clockwise from the front
        // A big target: the ring grows to clear it.
        P::Layout const big = P::LayoutShape(P::Shape::Circle, P::Gap::Normal, 3, 10.0f);
        CHECK(big.radius >= 10.0f + gap - 1e-3f);
        // More bots need more circumference.
        P::Layout const many = P::LayoutShape(P::Shape::Circle, P::Gap::Wide, 20, 0.0f);
        CHECK(many.radius >= 20.0f * P::GapYd(P::Gap::Wide) / (2.0f * 3.14159265f) - 1e-2f);
        // ... and past the ceiling the ring is refused, never silently shrunk.
        P::Layout const huge = P::LayoutShape(P::Shape::Circle, P::Gap::Wide, 60, 0.0f);
        CHECK(huge.tooFar);
        P::Layout const boss = P::LayoutShape(P::Shape::Circle, P::Gap::Wide, 4, 40.0f);
        CHECK(boss.tooFar);
    }
    // arc: a half ring in front of the anchor, the first slot at its centre.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Arc, P::Gap::Normal, 5, 0.0f);
        CHECK(l.radius >= gap - 1e-3f);
        for (P::Slot const& slot : l.slots)
        {
            CHECK(std::fabs(std::sqrt(slot.x * slot.x + slot.y * slot.y) - l.radius) < 1e-2f);
            CHECK(slot.y >= -1e-3f);
        }
        CHECK(std::fabs(l.slots[0].x) < 1e-3f && l.slots[0].y > 0.0f);
        // centre-out: the next slots are one to each side, the last two at the ends.
        CHECK(l.slots[1].x > 0.0f && l.slots[2].x < 0.0f);
        CHECK(std::fabs(std::fabs(l.slots[4].x) - l.radius) < 0.5f);
        P::Layout const big = P::LayoutShape(P::Shape::Arc, P::Gap::Normal, 3, 8.0f);
        CHECK(big.radius >= 8.0f + gap - 1e-3f);
    }
    // spread: a grid 2.5 gaps apart, so nobody shares a splash.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Spread, P::Gap::Normal, 9, 0.0f);
        CHECK(PairwiseMin(l.slots) >= 2.5f * gap - 1e-2f);
        P::Layout const wide = P::LayoutShape(P::Shape::Spread, P::Gap::Wide, 4, 0.0f);
        CHECK(PairwiseMin(wide.slots) >= 2.5f * P::GapYd(P::Gap::Wide) - 1e-2f);
    }
    // line: one row across, one gap apart, the first slot in the middle.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Line, P::Gap::Normal, 5, 0.0f);
        for (P::Slot const& slot : l.slots)
            CHECK(std::fabs(slot.y) < 1e-3f);
        CHECK(std::fabs(l.slots[0].x) < 1e-3f);
        CHECK(std::fabs(std::fabs(l.slots[1].x) - gap) < 1e-2f);
        CHECK(std::fabs(std::fabs(l.slots[4].x) - 2.0f * gap) < 1e-2f);
    }
    // column: one file behind the anchor, one gap apart.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Column, P::Gap::Normal, 4, 0.0f);
        for (P::Slot const& slot : l.slots)
            CHECK(std::fabs(slot.x) < 1e-3f);
        CHECK(std::fabs(l.slots[0].y - l.slots[1].y - gap) < 1e-2f);
    }
    // spear: a wedge, one more bot in every row, the tip first.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Spear, P::Gap::Normal, 6, 0.0f);
        CHECK(std::fabs(l.slots[0].y - l.slots[1].y) > gap * 0.9f);          // row 0 (the tip) alone
        CHECK(std::fabs(l.slots[1].y - l.slots[2].y) < 1e-3f);               // row 1 holds two
        CHECK(std::fabs(std::fabs(l.slots[1].x - l.slots[2].x) - gap) < 1e-2f);
    }
    // arrow: a V, tip first, the arms symmetric.
    {
        P::Layout const l = P::LayoutShape(P::Shape::Arrow, P::Gap::Normal, 5, 0.0f);
        CHECK(std::fabs(l.slots[0].x) < 1e-3f);
        CHECK(l.slots[0].y > l.slots[1].y);
        CHECK(std::fabs(l.slots[1].x + l.slots[2].x) < 1e-2f);
        CHECK(std::fabs(l.slots[1].y - l.slots[2].y) < 1e-2f);
    }
    // No shape ever needs more than the ceiling for a real party.
    for (P::Shape shape : shapes)
        CHECK(!P::LayoutShape(shape, P::Gap::Wide, 5, 3.0f).tooFar);
}

static void TestFrames()
{
    float const gap = P::GapYd(P::Gap::Normal);
    P::Vec2 const player{ 10.0f, 20.0f };
    P::Vec2 const front{ 0.0f, 1.0f };      // the player faces north
    P::Vec2 const target{ 10.0f, 40.0f };   // 20 yd north: the front vector for `target` is (0,-1)
    P::Vec2 const cursor{ 10.0f, 80.0f };
    P::Shape const shapes[] = { P::Shape::Close, P::Shape::Circle, P::Shape::Arc, P::Shape::Spread,
        P::Shape::Line, P::Shape::Column, P::Shape::Spear, P::Shape::Arrow };

    for (P::Shape shape : shapes)
    {
        P::Layout const l = P::LayoutShape(shape, P::Gap::Normal, 5, 3.0f);
        // here: the whole shape in front of the player, never on the player's own spot.
        P::Frame const here = P::FrameFor(P::Anchor::Here, l, gap, player, front, target, 3.0f, cursor);
        float nearestPlayer = 1e9f, farthestPlayer = 0.0f;
        for (P::Slot const& slot : l.slots)
        {
            P::Vec2 const p = P::Place(here, slot);
            nearestPlayer = std::min(nearestPlayer, Dist(p, player));
            farthestPlayer = std::max(farthestPlayer, Dist(p, player));
            CHECK(p.y > player.y);                                     // along the facing
        }
        CHECK(nearestPlayer >= P::kHereClearYd - 1e-2f);
        CHECK(farthestPlayer <= P::kHereClearYd + 3.0f + 2.0f * l.extent + 1.0f);

        // target: the ring centred on it (never inside), or the rows between the player and it (never on it).
        P::Frame const onTarget = P::FrameFor(P::Anchor::Target, l, gap, player, front, target, 3.0f, cursor);
        float nearestTarget = 1e9f;
        float slotZero = 1e9f;
        for (size_t s = 0; s < l.slots.size(); ++s)
        {
            P::Vec2 const p = P::Place(onTarget, l.slots[s]);
            float const d = Dist(p, target);
            nearestTarget = std::min(nearestTarget, d);
            if (s == 0)
                slotZero = d;
            if (!P::IsRing(shape))
                CHECK(p.y <= target.y + 1e-2f);
        }
        CHECK(nearestTarget >= 3.0f + gap - 1e-2f);
        // The tank's slot stands in the front row nearest the target (the tank row holds the mob, the group behind it): in a grid the front row
        // is a row, so allow one spread spacing (2.5 gaps) of slack.
        CHECK(slotZero <= nearestTarget + P::kSpreadGapFactor * gap + 1e-2f);

        // cursor: the shape is centred on the ping and faces away from the player.
        P::Frame const atCursor = P::FrameFor(P::Anchor::Cursor, l, gap, player, front, target, 3.0f, cursor);
        CHECK(std::fabs(atCursor.front.x) < 1e-3f && std::fabs(atCursor.front.y - 1.0f) < 1e-3f);
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (P::Slot const& slot : l.slots)
        {
            P::Vec2 const p = P::Place(atCursor, slot);
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        CHECK(minX <= cursor.x + 1e-2f && maxX >= cursor.x - 1e-2f);
        CHECK(minY <= cursor.y + 1e-2f && maxY >= cursor.y - 1e-2f);
    }

    // target: the front points from the target back to the player.
    P::Layout const circle = P::LayoutShape(P::Shape::Circle, P::Gap::Normal, 4, 1.0f);
    P::Frame const f = P::FrameFor(P::Anchor::Target, circle, gap, player, front, target, 1.0f, cursor);
    CHECK(std::fabs(f.front.x) < 1e-3f && f.front.y < -0.99f);
    // A ping on the player's own spot cannot face away from the player: the player's facing stands in.
    P::Frame const same = P::FrameFor(P::Anchor::Cursor, circle, gap, player, front, target, 1.0f, player);
    CHECK(std::fabs(same.front.y - 1.0f) < 1e-3f);
}

static void TestRanksAndWho()
{
    std::vector<P::BotRank> bots = {
        { P::Rank::Healer, "Mpriest" }, { P::Rank::Ranged, "Mmage" }, { P::Rank::Tank, "Mtank" },
        { P::Rank::Melee, "Mrogue" }, { P::Rank::Healer, "Mpala" },
    };
    P::SortByRank(bots);
    CHECK(bots[0].rank == P::Rank::Tank);
    CHECK(bots[1].rank == P::Rank::Melee);
    CHECK(bots[2].rank == P::Rank::Ranged);
    CHECK(bots[3].name == "Mpala" && bots[4].name == "Mpriest");   // stable inside a rank: by name
    CHECK(P::RankBefore(P::Rank::Tank, "b", P::Rank::Melee, "a"));
    CHECK(P::RankBefore(P::Rank::Melee, "a", P::Rank::Melee, "b"));
    CHECK(!P::RankBefore(P::Rank::Healer, "a", P::Rank::Tank, "z"));

    P::WhoToken t;
    CHECK(P::ParseWho("all", t) && t.kind == P::WhoKind::All);
    CHECK(P::ParseWho("tanks", t) && t.kind == P::WhoKind::Tanks);
    CHECK(P::ParseWho("healers", t) && t.kind == P::WhoKind::Healers);
    CHECK(P::ParseWho("melee", t) && t.kind == P::WhoKind::Melee);
    CHECK(P::ParseWho("ranged", t) && t.kind == P::WhoKind::Ranged);
    CHECK(P::ParseWho("dps", t) && t.kind == P::WhoKind::Dps);
    CHECK(P::ParseWho("warrior", t) && t.kind == P::WhoKind::Class && t.word == "warrior");
    CHECK(P::ParseWho("group3", t) && t.kind == P::WhoKind::Group && t.group == 3);
    CHECK(P::ParseWho("group9", t) && t.kind == P::WhoKind::Name);   // 1..8 only
    CHECK(P::ParseWho("Mtank", t) && t.kind == P::WhoKind::Name && t.word == "Mtank");
    CHECK(P::ParseWho("", t) && t.kind == P::WhoKind::All);   // no word at all: every bot

    P::BotInfo const tank{ "Mtank", "warrior", P::Rank::Tank, 1 };
    P::BotInfo const priest{ "Mpriest", "priest", P::Rank::Healer, 2 };
    P::BotInfo const rogue{ "Mrogue", "rogue", P::Rank::Melee, 2 };
    P::BotInfo const mage{ "Mmage", "mage", P::Rank::Ranged, 3 };
    auto const selects = [](char const* word, P::BotInfo const& bot)
    {
        P::WhoToken token;
        P::ParseWho(word, token);
        return P::Selected(token, bot);
    };
    CHECK(selects("all", priest) && selects("all", tank));
    CHECK(selects("tanks", tank) && !selects("tanks", mage));
    CHECK(selects("healers", priest) && !selects("healers", rogue));
    CHECK(selects("melee", rogue) && !selects("melee", mage));
    CHECK(selects("ranged", mage) && !selects("ranged", tank));
    CHECK(selects("dps", rogue) && selects("dps", mage) && !selects("dps", tank) && !selects("dps", priest));
    CHECK(selects("Mage", mage) && !selects("Mage", rogue));
    CHECK(selects("group2", priest) && !selects("group2", mage));
    CHECK(selects("Mtank", tank) && !selects("Mtank", priest));
    CHECK(selects("mtank", tank));      // names are case-insensitive
    CHECK(!selects("Mpal", tank));
    // No who words at all: every bot.
    CHECK(P::Selected(std::vector<P::WhoToken>{}, tank));
}

static void TestFallback()
{
    float const gap = P::GapYd(P::Gap::Normal);
    P::FallbackCounts counts{ 1, 2, 1, 2 };
    std::vector<P::Slot> const slots = P::FallbackSlots(counts, P::Gap::Normal);
    CHECK(slots.size() == 6);
    // Tanks in front, melee just behind them, the arc last.
    CHECK(slots[0].y > slots[1].y);                       // tank row in front of the melee
    CHECK(std::fabs(slots[0].y - slots[1].y) >= gap - 1e-3f);
    CHECK(std::fabs(slots[1].y - slots[2].y) < 1e-3f);    // the two melee share a row
    for (size_t i = 3; i < slots.size(); ++i)
        CHECK(slots[i].y < slots[2].y - gap * 0.5f);      // ranged and healers on the arc behind
    CHECK(std::isfinite(slots[5].x));
    // The arc is a half ring: the healers take its ends, the ranged the middle.
    CHECK(std::fabs(slots[3].x) < 1e-3f);
    CHECK(std::fabs(slots[4].x) > 0.5f && std::fabs(slots[5].x) > 0.5f);
    CHECK(std::fabs(std::fabs(slots[4].x) - std::fabs(slots[5].x)) < 1e-2f);
    CHECK(PairwiseMin(slots) > 0.5f);
    // One of everything: still three rows and a single arc point.
    std::vector<P::Slot> const one = P::FallbackSlots(P::FallbackCounts{ 1, 1, 0, 1 }, P::Gap::Normal);
    CHECK(one.size() == 3);
    CHECK(one[0].y > one[1].y && one[1].y > one[2].y);
    // No bots at all: no slots, no crash.
    CHECK(P::FallbackSlots(P::FallbackCounts{ 0, 0, 0, 0 }, P::Gap::Normal).empty());
    // Nobody on the rally point itself.
    for (P::Slot const& slot : slots)
        CHECK(std::sqrt(slot.x * slot.x + slot.y * slot.y) > 0.5f);
}

static void TestMinimapPing()
{
    P::CursorPoint point;
    // The vanilla body: two floats, x then y.
    uint8_t vanilla[8];
    float x = -9100.5f, y = -2005.25f;
    std::memcpy(vanilla, &x, 4);
    std::memcpy(vanilla + 4, &y, 4);
    CHECK(P::ParseMinimapPing(vanilla, sizeof vanilla, point));
    CHECK(std::fabs(point.x - x) < 1e-2f && std::fabs(point.y - y) < 1e-2f);
    // The pinned core's layout (PartyPackets.cpp MinimapPingClient::Read): a flags byte (bit 7 = a party index follows the floats),
    // then x and y, then the optional party byte.
    uint8_t withParty[10];
    withParty[0] = 0x80;
    std::memcpy(withParty + 1, &x, 4);
    std::memcpy(withParty + 5, &y, 4);
    withParty[9] = 0;
    CHECK(P::ParseMinimapPing(withParty, sizeof withParty, point));
    CHECK(std::fabs(point.x - x) < 1e-2f && std::fabs(point.y - y) < 1e-2f);
    uint8_t noParty[9] = { 0x00 };
    std::memcpy(noParty + 1, &x, 4);
    std::memcpy(noParty + 5, &y, 4);
    CHECK(P::ParseMinimapPing(noParty, sizeof noParty, point));
    CHECK(std::fabs(point.x - x) < 1e-2f);
    // Garbage and truncation are refused, never guessed.
    CHECK(!P::ParseMinimapPing(nullptr, 0, point));
    uint8_t shortBody[4] = { 0, 0, 0, 0 };
    CHECK(!P::ParseMinimapPing(shortBody, sizeof shortBody, point));
    float nan = std::nanf("");
    uint8_t bad[8];
    std::memcpy(bad, &nan, 4);
    std::memcpy(bad + 4, &y, 4);
    CHECK(!P::ParseMinimapPing(bad, sizeof bad, point));
}

int main()
{
    TestWords();
    TestLayouts();
    TestFrames();
    TestRanksAndWho();
    TestFallback();
    TestMinimapPing();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}
