#include "forever/src/host/TbBotPerfPure.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::perf::BotPerfLine;
using TortoiseBots::perf::CoreMs;
using TortoiseBots::perf::Window;

int main()
{
    std::cout << "Starting TortoiseBots BOTPERF tests...\n";

    // An empty window writes zeros, not a division by zero.
    Window empty;
    CHECK(TortoiseBots::perf::AveragePassUs(empty) == 0);
    CHECK(TortoiseBots::perf::AverageWorldMs(empty) == 0);
    CHECK(CoreMs(empty) == 0);
    std::cout << "  [PASS] an empty window reads zero\n";

    // Three ticks: 3 ms of module work in all (3000 us) and 9 ms of world update in all. The module
    // pass averages 1000 us, the world tick 3 ms, so the core and the rest spent 2 ms.
    Window window;
    window.elapsedMs = 30000;
    window.ticks = 3;
    window.passSumUs = 3000;
    window.maxPassUs = 1500;
    window.worldSumMs = 9;
    CHECK(TortoiseBots::perf::AveragePassUs(window) == 1000);
    CHECK(TortoiseBots::perf::AverageWorldMs(window) == 3);
    CHECK(CoreMs(window) == 2);
    std::cout << "  [PASS] pass, world and core averages\n";

    // A module pass that outran the last world update never reports a negative core share.
    Window slow;
    slow.ticks = 1;
    slow.passSumUs = 20000;
    slow.worldSumMs = 5;
    CHECK(CoreMs(slow) == 0);
    std::cout << "  [PASS] the core share never goes negative\n";

    // The line is TB's `key=value` shape. Every field comes from the window/fleet it was handed, so a
    // reader can parse it without a second source.
    std::string const line = BotPerfLine(window, 7, 5, 3, 6, 2, 1);
    CHECK(line.rfind("TortoiseBots: BOTPERF ", 0) == 0);
    CHECK(line.find("passUs=1000 ") != std::string::npos);
    CHECK(line.find("maxUs=1500 ") != std::string::npos);
    CHECK(line.find("ticks=3 ") != std::string::npos);
    CHECK(line.find("coreMs=2 ") != std::string::npos);
    CHECK(line.find("bots=7 ") != std::string::npos);
    CHECK(line.find("engines=5 ") != std::string::npos);
    CHECK(line.find("owned=3 ") != std::string::npos);
    CHECK(line.find("mastered=6 ") != std::string::npos);
    CHECK(line.find("free=2 ") != std::string::npos);
    CHECK(line.find("pool=1") != std::string::npos);
    std::cout << "  [PASS] the line carries every field\n";

    std::cout << "RESULT: PASS (test_bot_perf)\n";
    return 0;
}
