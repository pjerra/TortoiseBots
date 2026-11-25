// Pure tests for `.tb los` (#308): host/TbLosQueryPure.h. Every check here runs without a core, a server or an engine.
// Build & run:
//   g++ -std=c++17 -O0 -Iforever/src forever/tools/test_los_query.cpp -o /tmp/test_los_query && /tmp/test_los_query
#include "host/TbLosQueryPure.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace TortoiseBots::losq;

namespace
{
bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

Side ParseOk(std::string const& word)
{
    Side side;
    bool const ok = ParseSide(word, side);
    assert(ok);
    (void)ok;
    return side;
}

Query ParseQueryOk(std::vector<std::string> const& words)
{
    QueryResult const result = ParseQuery(words);
    assert(result.reason == Reason::None);
    return result.query;
}

Reason ParseQueryReason(std::vector<std::string> const& words)
{
    QueryResult const result = ParseQuery(words);
    assert(result.reason != Reason::None);
    return result.reason;
}
}

int main()
{
    // ---- one side: a character/bot name ----
    {
        Side const s = ParseOk("Mtank");
        assert(s.kind == SideKind::Name);
        assert(s.word == "Mtank");
    }
    {
        // The name is kept as typed; the handler lowers it for the in-world lookup.
        Side const s = ParseOk("mTaNk");
        assert(s.kind == SideKind::Name);
        assert(s.word == "mTaNk");
    }
    // ---- `target`: the caller's selection, in either case ----
    {
        Side const s = ParseOk("target");
        assert(s.kind == SideKind::Target);
    }
    {
        Side const s = ParseOk("TARGET");
        assert(s.kind == SideKind::Target);
    }
    // ---- a 32-hex guid, as `.tb state` prints it ----
    {
        Side const s = ParseOk("0000000000000000000000000000002a");
        assert(s.kind == SideKind::Guid);
        assert(s.guidHigh == 0);
        assert(s.guidLow == 0x2a);
    }
    {
        Side const s = ParseOk("00000000F1300E80000000000000002A");
        assert(s.kind == SideKind::Guid);
        assert(s.guidHigh == 0x00000000F1300E80ull);
        assert(s.guidLow == 0x2A);
    }
    // ---- the core's 0x form (`ObjectGuid::ToHexString`) ----
    {
        Side const s = ParseOk("0x00000000F1300E80000000000000002A");
        assert(s.kind == SideKind::Guid);
        assert(s.guidHigh == 0x00000000F1300E80ull);
        assert(s.guidLow == 0x2A);
    }
    // ---- a position `x,y,z` ----
    {
        Side const s = ParseOk("-601.25,-4250.5,38.9");
        assert(s.kind == SideKind::Position);
        assert(Near(s.pos.x, -601.25f));
        assert(Near(s.pos.y, -4250.5f));
        assert(Near(s.pos.z, 38.9f));
    }
    {
        Side const s = ParseOk("1,2,+3.5");
        assert(s.kind == SideKind::Position);
        assert(Near(s.pos.x, 1.0f) && Near(s.pos.y, 2.0f) && Near(s.pos.z, 3.5f));
    }
    // ---- words that are not a side ----
    {
        Side s;
        assert(!ParseSide("", s));
        assert(!ParseSide("1,2", s));             // two coordinates, not three
        assert(!ParseSide("1,2,3,4", s));          // four
        assert(!ParseSide("1,,3", s));             // an empty coordinate
        assert(!ParseSide("1,two,3", s));          // not a number
        assert(!ParseSide("map=389", s));          // the map word is not a side
        assert(!ParseSide("Tan k", s));            // a name is one word
        assert(!ParseSide("0000000000000000000000000000002", s));    // 31 hex digits
        assert(!ParseSide("0000000000000000000000000000002G", s));   // not hex
        assert(!ParseSide("Mtank!", s));           // not a name character
        assert(!ParseSide("00000000000000000000000000000000", s));   // the empty guid
    }

    // ---- the whole argument list ----
    {
        Query const q = ParseQueryOk({"Mtank", "Mhealer"});
        assert(q.a.kind == SideKind::Name && q.a.word == "Mtank");
        assert(q.b.kind == SideKind::Name && q.b.word == "Mhealer");
        assert(!q.hasMap);
    }
    {
        // A unit side and a position side: the position is on the unit's map.
        Query const q = ParseQueryOk({"Mtank", "1.0,2.0,3.0"});
        assert(q.a.kind == SideKind::Name);
        assert(q.b.kind == SideKind::Position);
        assert(!q.hasMap);
    }
    {
        // Two positions need their map.
        Query const q = ParseQueryOk({"1.0,2.0,3.0", "4.0,5.0,6.0", "map=389"});
        assert(q.hasMap);
        assert(q.mapId == 389);
    }
    {
        // Two positions without `map=`: refused as a bad argument, never guessed.
        assert(ParseQueryReason({"1.0,2.0,3.0", "4.0,5.0,6.0"}) == Reason::BadArgument);
        // `map=` is for a position-position query only.
        assert(ParseQueryReason({"Mtank", "Mhealer", "map=389"}) == Reason::BadArgument);
        assert(ParseQueryReason({"Mtank", "1.0,2.0,3.0", "map=389"}) == Reason::BadArgument);
        // Word count and the map word itself.
        assert(ParseQueryReason({"Mtank"}) == Reason::BadArgument);
        assert(ParseQueryReason({"Mtank", "Mhealer", "Mthird"}) == Reason::BadArgument);
        assert(ParseQueryReason({"1.0,2.0,3.0", "4.0,5.0,6.0", "map="}) == Reason::BadArgument);
        assert(ParseQueryReason({"1.0,2.0,3.0", "4.0,5.0,6.0", "map=1x"}) == Reason::BadArgument);
        assert(ParseQueryReason({"1.0,2.0,3.0", "4.0,5.0,6.0", "map=0"}) == Reason::BadArgument);
        assert(ParseQueryReason({"1.0,2.0,3.0", "4.0,5.0,6.0", "map=4294967296"}) == Reason::BadArgument);
    }

    // ---- the rate limit: five a second, per caller, on any clock ----
    {
        RateBucket bucket;                       // 5 tokens, one back every 200 ms
        for (int i = 0; i < 5; ++i)
            assert(bucket.Take(1000));
        assert(!bucket.Take(1000));             // the sixth in the same millisecond
        assert(!bucket.Take(1199));             // 199 ms: not a whole token yet
        assert(bucket.Take(1200));              // 200 ms: one token back
        assert(!bucket.Take(1200));
        for (int i = 0; i < 4; ++i)
            assert(bucket.Take(2000));          // 800 ms later: four tokens back, all taken
        assert(!bucket.Take(2000));
        assert(bucket.Take(2200));              // 200 ms: one back
        assert(!bucket.Take(2200));
        // A clock that went backwards refills nothing and does not crash.
        RateBucket rewound;
        assert(rewound.Take(5000));
        assert(rewound.Take(100));
        assert(rewound.Take(100));
        assert(rewound.Tokens() == 2);
        assert(!rewound.Idle(100));
        assert(rewound.Idle(6000));              // five whole slices after the last take: full and quiet
    }
    {
        RateBucket bucket;
        assert(bucket.Take(0));
        assert(!bucket.Idle(0));                 // a take less than a second ago: kept
        assert(bucket.Idle(1000));               // full again (4 tokens + 5 slices) and quiet
    }
    {
        RateBucket bucket;
        (void)bucket.Take(0); (void)bucket.Take(0); (void)bucket.Take(0);
        (void)bucket.Take(0); (void)bucket.Take(0);
        assert(bucket.Tokens() == 0);
        assert(bucket.Idle(1000));               // full again and quiet: prunable
        assert(bucket.Take(1001));
    }

    // ---- the refusal codes ----
    {
        assert(std::string(ReasonCode(Reason::UnknownUnit)) == "unknown-unit");
        assert(std::string(ReasonCode(Reason::NotInWorld)) == "not-in-world");
        assert(std::string(ReasonCode(Reason::DifferentMaps)) == "different-maps");
        assert(std::string(ReasonCode(Reason::BadArgument)) == "bad-argument");
        assert(std::string(ReasonCode(Reason::NoAccess)) == "no-access");
        assert(std::string(ReasonCode(Reason::Rate)) == "rate");
        assert(ReasonCode(Reason::None) == nullptr);
    }

    // ---- the access rule: console and GM always; a player only on its own units ----
    {
        // A unit side: a host bot, a real player's character, or a creature.
        AccessSide const botInRun{true, true, true, false, true};
        AccessSide const foreignPlayer{true, true, false, false, false};
        // A real player in no party: the shared check itself lets the caller through (guard::CheckControl has nothing to
        // refuse), which is exactly why the handler refuses another player's character before asking it.
        AccessSide const foreignPlayerLetIn{true, true, false, false, true};
        AccessSide const ownCharacter{true, true, false, true, false};
        AccessSide const creature{true, false, false, false, false};
        AccessSide const position{false, false, false, false, false};
        // The console and a GM pass on everything.
        assert(AccessAllowed(true, {position, position}));
        assert(AccessAllowed(true, {foreignPlayer, creature}));
        // A player needs one of its own allowed units, and may not name another player's character.
        assert(AccessAllowed(false, {botInRun, creature}));
        assert(AccessAllowed(false, {creature, botInRun}));
        assert(AccessAllowed(false, {ownCharacter, position}));
        assert(AccessAllowed(false, {botInRun, ownCharacter}));
        assert(!AccessAllowed(false, {position, position}));          // no unit to be allowed on
        assert(!AccessAllowed(false, {creature, position}));          // a mob is nobody's unit
        assert(!AccessAllowed(false, {foreignPlayer, botInRun}));     // probing another player
        assert(!AccessAllowed(false, {botInRun, foreignPlayer}));     // in either position
        assert(!AccessAllowed(false, {foreignPlayerLetIn, botInRun})); // even when the shared check would let it through
        assert(!AccessAllowed(false, {AccessSide{true, true, true, false, false}, creature}));   // a bot outside its party
    }

    // ---- the decision, in order ----
    {
        SideState const unit{false, true, true, true, 389};
        SideState const unitOtherMap{false, true, true, true, 390};
        SideState const ghost{false, true, false, true, 389};         // a unit that is not in the world
        SideState const missing{false, false, false, false, 0};       // a name or a guid that resolved to nothing
        SideState const pos{true, true, true, true, 389};
        SideState const posNoMap{true, true, false, true, 389};       // the map id is not loaded
        assert(Decide({unit, unit, true}) == Reason::None);
        assert(Decide({unit, pos, true}) == Reason::None);
        assert(Decide({pos, pos, true}) == Reason::None);
        assert(Decide({missing, unit, true}) == Reason::UnknownUnit);
        assert(Decide({unit, missing, true}) == Reason::UnknownUnit);
        assert(Decide({ghost, unit, true}) == Reason::NotInWorld);
        assert(Decide({posNoMap, pos, true}) == Reason::NotInWorld);
        assert(Decide({unit, unitOtherMap, true}) == Reason::DifferentMaps);
        // The instance too: two copies of one dungeon are not one place, even though the map id is equal.
        SideState const instance1{false, true, true, true, 389, true, 1};
        SideState const instance2{false, true, true, true, 389, true, 2};
        assert(Decide({instance1, instance1, true}) == Reason::None);
        assert(Decide({instance1, instance2, true}) == Reason::DifferentMaps);
        assert(Decide({instance2, instance1, true}) == Reason::DifferentMaps);
        // A position with a unit side inherits that unit's map and instance: no refusal against the same unit instance.
        SideState const posInstance1{true, true, true, true, 389, true, 1};
        SideState const posInstance2{true, true, true, true, 389, true, 2};
        assert(Decide({posInstance1, instance1, true}) == Reason::None);
        assert(Decide({instance1, posInstance1, true}) == Reason::None);
        // A position-position query is answered on one loaded copy: two positions in different copies cannot be mixed.
        assert(Decide({posInstance1, posInstance2, true}) == Reason::DifferentMaps);
        // The same instance number on another map is still another place.
        SideState const otherMapInstance1{false, true, true, true, 390, true, 1};
        assert(Decide({instance1, otherMapInstance1, true}) == Reason::DifferentMaps);
        assert(SamePlace(instance1, instance1));
        assert(SamePlace(posInstance1, instance1));
        assert(!SamePlace(instance1, instance2));
        assert(!SamePlace(posInstance1, posInstance2));
        assert(!SamePlace(instance1, otherMapInstance1));
        // An instance that is not known never invents a difference.
        assert(SamePlace(unit, instance1));
        // Authorization before world state, so a foreign player learns nothing of a guid it may not read.
        assert(Decide({ghost, unit, false}) == Reason::NoAccess);
        assert(Decide({missing, unit, false}) == Reason::UnknownUnit);
    }

    // ---- what the caller is told: no presence or map oracle for a caller below GameMaster ----
    {
        // Another player's character and a name that resolved to nothing give one answer: the same code and the same
        // (empty) world fields, so neither presence nor a map can be read from a refusal.
        assert(SeenByCaller(false, Reason::NoAccess) == Reason::NoAccess);
        assert(SeenByCaller(false, Reason::UnknownUnit) == Reason::NoAccess);
        assert(SeenByCaller(false, Reason::BadArgument) == Reason::BadArgument);
        assert(SeenByCaller(false, Reason::NotInWorld) == Reason::NotInWorld);
        assert(SeenByCaller(false, Reason::DifferentMaps) == Reason::DifferentMaps);
        assert(SeenByCaller(false, Reason::Rate) == Reason::Rate);
        // The GameMaster and the console keep the precise code.
        assert(SeenByCaller(true, Reason::UnknownUnit) == Reason::UnknownUnit);
        assert(SeenByCaller(true, Reason::NoAccess) == Reason::NoAccess);
        assert(AnswerHidesWorld(Reason::NoAccess));
        assert(!AnswerHidesWorld(Reason::UnknownUnit));
        assert(!AnswerHidesWorld(Reason::NotInWorld));
        assert(!AnswerHidesWorld(Reason::DifferentMaps));
        assert(!AnswerHidesWorld(Reason::None));
    }

    // ---- what the line prints ----
    {
        assert(FormatCoord({-601.25f, -4250.5f, 38.9f}) == "-601.25,-4250.50,38.90");
        assert(FormatDist(0.0f) == "0.0");
        assert(FormatDist(12.34f) == "12.3");
        assert(FormatDist(-1.0f) == "");
    }
    {
        // The position-position `dist`: horizontal only, so a height difference is never added to it.
        assert(Near(HorizontalDistance({0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}), 5.0f));
        assert(Near(HorizontalDistance({1.0f, 2.0f, 5.0f}, {1.0f, 2.0f, -90.0f}), 0.0f));
        assert(FormatDist(HorizontalDistance({0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 100.0f})) == "5.0");
        assert(FormatDist(HorizontalDistance({-601.25f, -4250.5f, 38.9f}, {-601.25f, -4246.5f, 3.0f})) == "4.0");
    }
    {
        // A position side is shown as its coordinates; a unit side as its name or its guid text.
        Query const q = ParseQueryOk({"1.0,2.0,3.0", "4.0,5.0,6.0", "map=389"});
        assert(DisplaySide(q.a, "") == "1.00,2.00,3.00");
        assert(DisplaySide(q.b, "") == "4.00,5.00,6.00");
    }
    {
        Side const s = ParseOk("0000000000000000000000000000002a");
        assert(DisplaySide(s, "") == "0000000000000000000000000000002A");
        assert(DisplaySide(s, "Mtank") == "Mtank");
    }
    {
        Side const s = ParseOk("Mtank");
        assert(DisplaySide(s, "") == "Mtank");    // unresolved: the word as typed
        assert(DisplaySide(s, "Mtank") == "Mtank");
        Side const target = ParseOk("target");
        assert(DisplaySide(target, "") == "target");
        assert(DisplaySide(target, "Kobold") == "Kobold");
    }

    std::cout << "test_los_query: OK" << std::endl;
    return 0;
}
