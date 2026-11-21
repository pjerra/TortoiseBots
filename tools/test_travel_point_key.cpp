// The travel destination point key (ai/playerbot/TravelPointKey.h). The ForeverCore guid's low half is only the spawn
// counter, and creatures and game objects count separately: keying pointsMap by the low half alone merged two unrelated
// spawns, and the destination then pointed at the other object's coordinates and cached area. The key carries both halves,
// so the same counter on a different guid type, map or entry stays a different point.
//
// Before the fix this file does not compile: the key was one uint64_t (and ForeverCore passed only GetRawValue(0)).
#include "../ai/playerbot/TravelPointKey.h"

#include <cstdio>
#include <unordered_map>

using ai::MakeTravelPointKey;
using ai::TravelPointKey;
using ai::TravelPointKeyHash;

static int failures = 0;

static void check(bool condition, char const* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A spawn as the loader keeps it: the point's coordinates stand in for the destination it is attached to.
struct Point
{
    float x;
    float y;
};

int main()
{
    // A creature spawn and a game-object spawn that share the spawn counter (7) but differ in type, map and entry: the
    // ForeverCore high half is (type, map, entry, subtype), the low half the counter.
    uint64_t const creatureHigh = (uint64_t(1) << 58) | (uint64_t(0) << 29) | (uint64_t(1234) << 6);   // type 1, map 0, entry 1234
    uint64_t const objectHigh   = (uint64_t(5) << 58) | (uint64_t(1) << 29) | (uint64_t(1234) << 6);   // type 5, map 1, same entry
    uint64_t const counter = 7;

    TravelPointKey const creature = MakeTravelPointKey(creatureHigh, counter);
    TravelPointKey const object = MakeTravelPointKey(objectHigh, counter);

    check(creature != object, "the same spawn counter on a creature and a game object is two keys");
    check(!(creature == object), "operator== agrees with operator!=");

    std::unordered_map<TravelPointKey, Point, TravelPointKeyHash> points;
    points.insert({ creature, Point{ 10.0f, 20.0f } });
    points.insert({ object, Point{ 30.0f, 40.0f } });
    check(points.size() == 2, "the destination map keeps both spawns");
    check(points.at(creature).x == 10.0f && points.at(creature).y == 20.0f, "the creature keeps its own coordinates");
    check(points.at(object).x == 30.0f && points.at(object).y == 40.0f, "the game object keeps its own coordinates");

    // The same identity twice is one point: a reload replaces, never duplicates.
    points.insert({ MakeTravelPointKey(creatureHigh, counter), Point{ 10.0f, 20.0f } });
    check(points.size() == 2, "an identical spawn identity does not add a second point");

    // The same high half and another counter are also different (the low half still counts).
    check(creature != MakeTravelPointKey(creatureHigh, counter + 1), "another spawn id of the same entry is another point");

    // The Turtle identity: one 64-bit guid as the low half, high half 0. It keeps the legacy identity exactly.
    TravelPointKey const turtle = MakeTravelPointKey(0, 0x0000000100000007ull);
    check(turtle == MakeTravelPointKey(0, 0x0000000100000007ull), "the Turtle identity compares equal to itself");
    check(turtle != MakeTravelPointKey(0, 0x0000000200000007ull), "different Turtle guids differ");
    check(turtle != creature, "a Turtle guid never equals a composite ForeverCore key of the same low half");

    // The hash separates the two halves: same counter, different high half must not land in the same bucket for this map
    // size (the concrete check is the map above; this pins that the hash is not the low half alone).
    TravelPointKeyHash hash;
    check(hash(creature) != hash(object), "the hash folds in the high half");
    check(hash(MakeTravelPointKey(0, 7)) == hash(MakeTravelPointKey(0, 7)), "the hash is stable for one key");

    if (failures == 0)
        std::printf("test_travel_point_key: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
