#include "../ai/playerbot/FourHorsemenRotationPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// Four corners of a 40yd square around (0,0). WoW angles: +x = 0,
// +y = pi/2 (counter-clockwise), so clockwise from +x goes to -y.
static void Square(HorsemanSpot* s)
{
    // idx 0 Thane (+x), 1 Blaumeux (+y), 2 Mograine (-x), 3 Zeliek (-y)
    const float pos[4][2] = { { 20.0f, 0.0f }, { 0.0f, 20.0f }, { -20.0f, 0.0f }, { 0.0f, -20.0f } };
    for (int i = 0; i < 4; ++i)
    {
        s[i] = HorsemanSpot();
        s[i].alive = true;
        s[i].x = pos[i][0];
        s[i].y = pos[i][1];
    }
}

int main()
{
    std::cout << "Starting TortoiseBots Four Horsemen rotation policy tests...\n";

    CHECK(FourHorsemenIndexForEntry(16064) == 0);
    CHECK(FourHorsemenIndexForEntry(16063) == 3);
    CHECK(FourHorsemenIndexForEntry(16149) == -1);
    std::cout << "  [PASS] entry table matches mark table order\n";

    HorsemanSpot s[4];
    Square(s);
    CHECK(FourHorsemenStackingIndex(s) == -1);
    s[0].stacks = 2;
    CHECK(FourHorsemenStackingIndex(s) == -1);
    s[0].stacks = 3;
    CHECK(FourHorsemenStackingIndex(s) == 0);
    s[2].stacks = 4;
    CHECK(FourHorsemenStackingIndex(s) == 2);
    std::cout << "  [PASS] rotation starts at 3 stacks, worst mark first\n";

    // At Thane (+x) with 3 stacks, all others clean: next clockwise is -y.
    Square(s);
    s[0].stacks = 3;
    CHECK(FourHorsemenNextIndex(s, 0, 0) == 3);
    // From Zeliek (-y) clockwise is Mograine (-x).
    Square(s);
    s[3].stacks = 3;
    CHECK(FourHorsemenNextIndex(s, 3, 3) == 2);
    std::cout << "  [PASS] next corner is clockwise\n";

    // Fewest stacks beats clockwise order.
    Square(s);
    s[0].stacks = 3;
    s[3].stacks = 2;
    CHECK(FourHorsemenNextIndex(s, 0, 0) == 2);
    // Dead horsemen are skipped.
    Square(s);
    s[0].stacks = 3;
    s[3].alive = false;
    CHECK(FourHorsemenNextIndex(s, 0, 0) == 2);
    // Nothing else alive: stay.
    Square(s);
    s[0].stacks = 3;
    s[1].alive = s[2].alive = s[3].alive = false;
    CHECK(FourHorsemenNextIndex(s, 0, 0) == -1);
    std::cout << "  [PASS] fewest stacks first, dead skipped, last one holds\n";

    // Already rotated to a corner still under 3: stay there.
    Square(s);
    s[0].stacks = 4;
    s[3].stacks = 2;
    CHECK(FourHorsemenNextIndex(s, 0, 3) == 3);
    // ... until that corner hits 3 too, then move on (not back to Thane).
    s[3].stacks = 3;
    CHECK(FourHorsemenNextIndex(s, 0, 3) == 2);
    std::cout << "  [PASS] stays at the new corner until its mark stacks up\n";

    // Stored choice: Thane (0) at 3, Blaumeux (1) and Zeliek (3) at 2, so
    // the bot walks diagonally to Mograine (2). Mid-walk Blaumeux is
    // nearest; the stored choice still wins over the nearest corner.
    Square(s);
    s[0].stacks = 3;
    s[1].stacks = 2;
    s[3].stacks = 2;
    CHECK(FourHorsemenNextIndex(s, 0, 1) == 1);
    CHECK(FourHorsemenNextIndex(s, 0, 0) == 2);
    CHECK(FourHorsemenChoiceHolds(s, 0, 2));
    CHECK(FourHorsemenNextIndex(s, 0, FourHorsemenChoiceHolds(s, 0, 2) ? 2 : 1) == 2);
    // It lapses when the old mark drops under 3 ...
    s[0].stacks = 2;
    CHECK(!FourHorsemenChoiceHolds(s, 0, 2));
    // ... when the chosen corner stacks up or dies ...
    s[0].stacks = 3;
    s[2].stacks = 3;
    CHECK(!FourHorsemenChoiceHolds(s, 0, 2));
    s[2].stacks = 0;
    s[2].alive = false;
    CHECK(!FourHorsemenChoiceHolds(s, 0, 2));
    // ... and nothing stored (0 - 1 = -1) or out of range never holds.
    CHECK(!FourHorsemenChoiceHolds(s, -1, -1));
    CHECK(!FourHorsemenChoiceHolds(s, 0, 4));
    CHECK(!FourHorsemenChoiceHolds(s, 1, 1));
    std::cout << "  [PASS] stored corner holds mid-walk, lapses on reset\n";

    // Stand-off spot: toward the centre, capped at the centre.
    Square(s);
    float x, y;
    FourHorsemenCornerSpot(s, 0, kFourHorsemenRangedOffsetYd, x, y);
    CHECK(Near(x, 0.0f) && Near(y, 0.0f));
    FourHorsemenCornerSpot(s, 1, kFourHorsemenMeleeOffsetYd, x, y);
    CHECK(Near(x, 0.0f) && Near(y, 17.0f));
    FourHorsemenCornerSpot(s, 0, 50.0f, x, y);
    CHECK(Near(x, 0.0f) && Near(y, 0.0f));
    std::cout << "  [PASS] stand-off spot points into the room\n";

    CHECK(FourHorsemenArrived(true, 5.0f, kFourHorsemenMeleeOffsetYd));
    CHECK(!FourHorsemenArrived(true, 9.0f, kFourHorsemenMeleeOffsetYd));
    CHECK(!FourHorsemenArrived(false, 2.0f, kFourHorsemenMeleeOffsetYd));
    CHECK(FourHorsemenArrived(true, 22.0f, kFourHorsemenRangedOffsetYd));
    std::cout << "  [PASS] arrival stops the move\n";

    std::cout << "All Four Horsemen rotation policy tests passed.\n";
    return 0;
}
