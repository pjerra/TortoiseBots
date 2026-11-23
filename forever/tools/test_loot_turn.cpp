// Standalone test of the group-loot turn decision behind party corpse looting
// (task fx-loot; ticket TBF #134 acceptance "lootlog shows the last kill's loot"):
//   g++ -std=c++20 -O1 -Wall -Wextra -I/mnt/pny-ssd/Tortoise\ WoW\ Projects/TrinityCore/src/server/game/Groups forever/tools/test_loot_turn.cpp -o /tmp/test_loot_turn && /tmp/test_loot_turn
//
// What it pins (proven by code reading, see the fx-loot report):
// - Open-world grouped kills give every tapper (and, with a bot in the group,
//   every subgroup member via BotGroupRules::SharesTap) its own PERSONAL loot
//   (Unit::Kill, Creature::GetLootForPlayer): no round-robin turn exists, so
//   MayLootCorpse lets every member open its own copy. fx-134's "round-robin
//   turns explain the empty log" is wrong for that setup.
// - For shared (dungeon) corpses the turn rotates with BotGroupRules::
//   NextRoundRobinLooter: bots take a turn only while no real player of the
//   group is in range of the corpse. That is the rule a party bot's "may I
//   open" decision reads through CorpseRoundRobinGuidCompat.
// - A `lootmethod` chat trigger is NOT TurtleBot behaviour (no such word in
//   ChatCommandHandlerStrategy; TB drives it via `.bot cmd lootmethod`), so
//   nothing registers it; the host `.tb cmd lootmethod` keeps TB's gates
#include "BotGroupRules.h"
#include <cstdio>
#include <string>
#include <vector>

namespace BGR = BotGroupRules;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %d: %s\n", __LINE__, #cond); } } while (0)

using Cands = std::vector<BGR::LooterCandidate>;

static BGR::LooterCandidate Cand(bool inRange, bool bot)
{
    BGR::LooterCandidate c;
    c.inRange = inRange;
    c.bot = bot;
    return c;
}

// "their turn": with no player in range the turn rotates through every
// member in range, bots included (open-world personal loot carries no turn
// at all, so this is the shared-corpse rule).
static void TestBotsRotateWithoutPlayer()
{
    // player + 2 bots, nobody near the corpse: nobody may take it.
    Cands c = { Cand(false, false), Cand(false, true), Cand(false, true) };
    CHECK(BGR::NextRoundRobinLooter(c, 3, true) == 3);
    // player far, both bots near: the turn goes to a bot, then rotates.
    c = { Cand(false, false), Cand(true, true), Cand(true, true) };
    std::size_t first = BGR::NextRoundRobinLooter(c, 3, true);
    CHECK(first == 1 || first == 2);
    std::size_t second = BGR::NextRoundRobinLooter(c, first, false);
    CHECK(second != first && second < 3);
    // keepCurrent: the holder keeps the turn while it may still take it.
    CHECK(BGR::NextRoundRobinLooter(c, first, true) == first);
}

// "when the player is not the looter": while a real player is in range,
// bots never take the turn (so a headless owner parked at the corpse
// locks every shared corpse to itself); the moment the player leaves
// range the turn passes to a bot.
static void TestPlayerInRangeLocksBotsOut()
{
    // player + 2 bots, all in range: the turn never lands on a bot.
    Cands c = { Cand(true, false), Cand(true, true), Cand(true, true) };
    for (std::size_t cur = 0; cur <= 3; ++cur)
    {
        std::size_t next = BGR::NextRoundRobinLooter(c, cur, cur < 3);
        CHECK(next == 0);
    }
    // the player steps out of reward range: a bot takes the turn.
    c[0].inRange = false;
    std::size_t next = BGR::NextRoundRobinLooter(c, 0, true);
    CHECK(next == 1 || next == 2);
    // bots-only party: rotation is round-robin among the bots.
    Cands bots = { Cand(true, true), Cand(true, true) };
    CHECK(BGR::NextRoundRobinLooter(bots, 2, true) == 0);
    CHECK(BGR::NextRoundRobinLooter(bots, 0, false) == 1);
    CHECK(BGR::NextRoundRobinLooter(bots, 1, false) == 0);
}

// SharesTap: a tap outside raid maps covers the tapper's subgroup; in a
// group with bots every real player of the group is covered too, so a bot
// tagging first no longer leaves its player with no loot on the corpse.
static void TestSharesTap()
{
    CHECK(BGR::SharesTap(false, true, false, false));    // same subgroup, no bots: shared
    CHECK(!BGR::SharesTap(false, false, false, false));  // other subgroup, no bots: not shared
    CHECK(BGR::SharesTap(false, false, true, false));    // other subgroup, bot in group, member is a player: shared
    CHECK(!BGR::SharesTap(false, false, true, true));    // other subgroup, bot in group, member is a bot: not shared
    CHECK(BGR::SharesTap(true, false, false, false));    // raid map: shared
}

int main()
{
    TestBotsRotateWithoutPlayer();
    TestPlayerInRangeLocksBotsOut();
    TestSharesTap();
    std::printf("%s (%d checks)\n", g_fail ? "FAIL" : "OK", g_checks);
    return g_fail ? 1 : 0;
}
