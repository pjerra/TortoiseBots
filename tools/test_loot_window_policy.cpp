#include "../ai/playerbot/strategy/actions/LootWindowPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DecideAfterLootWindow;
using ai::DecideOpenLootDuration;
using ai::IsPlayerLootOrder;
using ai::LootWindowVerdict;

int main()
{
    std::cout << "Starting TortoiseBots loot-window policy tests...\n";

    // 1. A window that paid out forgets the counted opens (multi-use ore vein,
    // loot-then-skin corpse), ordered or not.
    CHECK(DecideAfterLootWindow(false, true, false) == LootWindowVerdict::ForgetFailures);
    CHECK(DecideAfterLootWindow(false, true, true) == LootWindowVerdict::ForgetFailures);
    std::cout << "  [PASS] a paying window forgets the open count\n";

    // 2. The Uldaman case: the window gave the bot nothing (roll pending,
    // view-only, unwanted) - abandon at once instead of re-opening it.
    CHECK(DecideAfterLootWindow(false, false, false) == LootWindowVerdict::Abandon);
    std::cout << "  [PASS] an empty window abandons the object\n";

    // 3. The server refused the loot (loot error packet): abandon too, the
    // core keeps the corpse lootable so the sweep would re-queue it.
    CHECK(DecideAfterLootWindow(true, false, false) == LootWindowVerdict::Abandon);
    std::cout << "  [PASS] a refused window abandons the object\n";

    // 4. A player's explicit loot order never records the abandon, empty or
    // refused: it is only unqueued and keeps its open count.
    CHECK(DecideAfterLootWindow(false, false, true) == LootWindowVerdict::KeepCount);
    CHECK(DecideAfterLootWindow(true, false, true) == LootWindowVerdict::KeepCount);
    std::cout << "  [PASS] a player order is unqueued, never abandoned\n";

    // 5. Only the chat command trigger with a commanding player is an order;
    // the automatic sweep, RPG chests and the debug command are not.
    CHECK(IsPlayerLootOrder("add all loot", true));
    CHECK(!IsPlayerLootOrder("add all loot", false));
    CHECK(!IsPlayerLootOrder("often", false));
    CHECK(!IsPlayerLootOrder("often", true));
    CHECK(!IsPlayerLootOrder("timer", false));
    CHECK(!IsPlayerLootOrder("debug", true));
    CHECK(!IsPlayerLootOrder("", true));
    std::cout << "  [PASS] only the chat loot command counts as a player order\n";

    // 6. #1062: an open that started a cast (Herb Gathering 5 s, Mining 3.2 s)
    // sleeps through the cast, so "update follow" cannot walk the bot off the
    // node and cancel it. A corpse CMSG_LOOT keeps lootDelay, a failed open
    // keeps the react delay, and an instant open (no cast time) never sleeps
    // a cast time.
    CHECK(DecideOpenLootDuration(true, 5000, 100) == 5000);
    CHECK(DecideOpenLootDuration(true, 3200, 1000) == 3200);
    CHECK(DecideOpenLootDuration(true, 0, 1000) == 1000);
    CHECK(DecideOpenLootDuration(false, 5000, 100) == 100);
    CHECK(DecideOpenLootDuration(false, 0, 100) == 100);
    std::cout << "  [PASS] an open-loot cast sleeps through its cast time\n";

    std::cout << "All loot-window policy tests passed.\n";
    return 0;
}
