// Standalone check of the opt-in player-led run (issue #139; forever/docs/RUN-KNOBS-139-NOTES.md): no game types, no server.
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_run_playerled.cpp -o /tmp/test_run_playerled && /tmp/test_run_playerled
//
// It checks the pure half of the start (host/run/TbRunPlayerLedPure.h: the `lead=` word, the bind gate with its refusals, the owned-bot rule) and
// the follow logic the glue drives (runtime/PlayerLedPolicy.h Pick + Tick over a scripted player): a GM walks a small plan from its entrance to the
// first boss, the run pulls each pack only when he stands near it and still, never orders a walk, and reaches the boss step.
#include "forever/src/host/run/TbRunPlayerLedPure.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;
namespace pl = TortoiseBots::run::playerled;
namespace PL = TortoiseBots::PlayerLed;
using pl::LeadWord;
using pl::MemberObs;
using pl::StartObs;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// ---- the start ----

MemberObs Bot(std::string const& name, bool tank = false)
{
    MemberObs m;
    m.name = name;
    m.bot = true;
    m.owned = true;
    m.commanderIsStarter = true;
    m.tankRole = tank;
    return m;
}

StartObs Party(std::vector<MemberObs> members, std::string const& tank = "Tanky")
{
    StartObs o;
    o.grouped = true;
    o.starterLeads = true;
    o.tankName = tank;
    o.members = std::move(members);
    return o;
}

void TestLeadWord()
{
    std::vector<std::string> w{ "Tanky", "wc" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::None && w.size() == 2);
    w = { "Tanky", "wc", "lead=player" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Player && w == std::vector<std::string>({ "Tanky", "wc" }));
    w = { "Tanky", "wc", "LEAD=Player" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Player && w.size() == 2);
    w = { "Tanky", "wc", "lead=bot" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Bot && w.size() == 2);
    w = { "Tanky", "wc", "lead=me" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Bad);
    // two lead words: no guess
    w = { "Tanky", "wc", "lead=player", "lead=bot" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Bad);
    // the word stands anywhere after the plan, the other words keep their order
    w = { "Tanky", "wc", "from", "lead=player", "3" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::Player && w == std::vector<std::string>({ "Tanky", "wc", "from", "3" }));
    // the leader and the plan are never taken, even when a plan is named like the word
    w = { "lead=player", "wc" };
    CHECK(pl::TakeLeadWord(w) == LeadWord::None && w.size() == 2);
}

void TestStartAccepts()
{
    pl::StartVerdict v = pl::DecideStart(Party({ Bot("Tanky", true), Bot("Healy"), Bot("Stabby") }));
    CHECK(v.code.empty() && v.tank == 0);
    // another real player in the party is fine (he stays, he is never ordered)
    MemberObs friendPlayer;
    friendPlayer.name = "Baerthe";
    v = pl::DecideStart(Party({ Bot("Tanky", true), friendPlayer, Bot("Healy") }));
    CHECK(v.code.empty() && v.tank == 0);
}

void TestStartRefusals()
{
    // the key: off refuses with its own words
    StartObs o = Party({ Bot("Tanky", true) });
    o.keyOn = false;
    pl::StartVerdict v = pl::DecideStart(o);
    CHECK(v.code == "disabled" && v.text.find("DungeonRunPlayerLed") != std::string::npos);
    // a bot cannot start it
    o = Party({ Bot("Tanky", true) });
    o.starterIsBot = true;
    CHECK(pl::DecideStart(o).code == "commander-is-bot");
    // no party, a raid, not the leader
    o = Party({ Bot("Tanky", true) });
    o.grouped = false;
    CHECK(pl::DecideStart(o).code == "not-grouped");
    o = Party({ Bot("Tanky", true) });
    o.raid = true;
    CHECK(pl::DecideStart(o).code == "raid");
    o = Party({ Bot("Tanky", true) });
    o.starterLeads = false;
    CHECK(pl::DecideStart(o).code == "not-leader");
    // the starter has no bot in the group at all: nothing to lead
    MemberObs friendPlayer;
    friendPlayer.name = "Baerthe";
    CHECK(pl::DecideStart(Party({ friendPlayer })).code == "no-bots");
    // the starter has no OWNED bot in the group: every bot is someone else's
    MemberObs foreign = Bot("Tanky", true);
    foreign.owned = false;
    foreign.commanderIsStarter = false;
    CHECK(pl::DecideStart(Party({ foreign })).code == "not-controllable");
    // one foreign bot among his own refuses too (it would never follow him)
    MemberObs stranger = Bot("Zappy");
    stranger.owned = false;
    stranger.commanderIsStarter = false;
    CHECK(pl::DecideStart(Party({ Bot("Tanky", true), stranger })).code == "not-controllable");
    // a bot of his that another binding holds (a bot-led run's tank, another commander)
    MemberObs bound = Bot("Healy");
    bound.commanderIsStarter = false;
    bound.commanderOther = true;
    CHECK(pl::DecideStart(Party({ Bot("Tanky", true), bound })).code == "bot-bound");
    // his own bot with no commander yet: the run does not bind it
    MemberObs loose = Bot("Healy");
    loose.commanderIsStarter = false;
    pl::StartVerdict v2 = pl::DecideStart(Party({ Bot("Tanky", true), loose }));
    CHECK(v2.code == "not-commanded" && v2.text.find("Healy") != std::string::npos);
    // a bot not in the world
    MemberObs away = Bot("Healy");
    away.inWorld = false;
    CHECK(pl::DecideStart(Party({ Bot("Tanky", true), away })).code == "not-live");
    // the named tank must be a bot of the party
    CHECK(pl::DecideStart(Party({ Bot("Healy") }, "Nobody")).code == "tank-not-in-party");
    MemberObs player;
    player.name = "Baerthe";
    CHECK(pl::DecideStart(Party({ Bot("Healy"), player }, "Baerthe")).code == "tank-not-bot");
    // a party of six
    CHECK(pl::DecideStart(Party({ Bot("Tanky", true), Bot("A"), Bot("B"), Bot("C"), Bot("D"), Bot("E") })).code == "group-too-large");
}

// ---- the follow logic: a GM walks, the run pulls near him ----

PlanStep At(PlanStepKind kind, float x, std::vector<std::uint32_t> entries = {}, std::uint32_t entry = 0)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.entries = std::move(entries);
    s.entry = entry;
    ai::NormalizePlanStep(s);
    return s;
}

// move 0, pull 4851 at x 40, move 80, pull 4852 at x 120, pull 4853 at x 160, boss 100 at x 200, pull 4860 at x 260 (after the first boss).
DungeonPlan Route()
{
    DungeonPlan plan;
    plan.steps = { At(PlanStepKind::Move, 0), At(PlanStepKind::Pull, 40, { 4851 }), At(PlanStepKind::Move, 80), At(PlanStepKind::Pull, 120, { 4852 }),
        At(PlanStepKind::Pull, 160, { 4853 }), At(PlanStepKind::Boss, 200, {}, 100), At(PlanStepKind::Pull, 260, { 4860 }) };
    return plan;
}

UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.distLead = dist;
    return u;
}

Facts Seen(std::vector<UnitFact> units)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.seen = true;
    f.carrier = "Tanky";
    MemberFact tank;
    tank.name = "Tanky";
    f.members = { tank };
    f.units = std::move(units);
    PlayerFact me;
    me.name = "Gm";
    me.commander = true;
    me.yd = 4.0f;
    f.players = { me };
    return f;
}

bool Walks(std::string const& order)
{
    return order.compare(0, 5, "move ") == 0 || order.compare(0, 5, "walk ") == 0 || order.compare(0, 7, "escape ") == 0 || order.compare(0, 7, "rescue ") == 0;
}

PL::Anchor Player(float x, bool moving)
{
    PL::Anchor a;
    a.read = true;
    a.x = x;
    a.alive = true;
    a.onMap = true;
    a.moving = moving;
    a.botsMaxYd = 3.0f;
    return a;
}

// One scripted second: the glue's order (Pick, then Tick). `packs` are the living packs the facts show for the picked step.
struct Walk
{
    DungeonPlan plan = Route();
    Run run = Start(&plan);
    PL::State pls;
    std::vector<std::string> orders;
    std::vector<size_t> pulledSteps;
    Walk()
    {
        pls.on = true;
        pls.tank = "Tanky";
    }
    // The player stands at x for `seconds` (still, or walking when moving). `alive`: the units of each pack entry that are still alive.
    void Second(float x, bool moving, std::map<std::uint32_t, bool> const& alive, bool fight = false)
    {
        PL::Anchor a = Player(x, moving);
        PL::Pick(run, pls, a, {});
        std::vector<UnitFact> units;
        if (ai::PlanStep const* step = Current(run))
            for (std::uint32_t entry : step->entries)
            {
                auto const it = alive.find(entry);
                if (it == alive.end() || it->second)
                    units.push_back(Unit(1000 + entry, entry, std::abs(step->x - x)));
            }
        if (ai::PlanStep const* step = Current(run); step && step->kind == PlanStepKind::Boss && step->entry)
            units.push_back(Unit(1000 + step->entry, step->entry, std::abs(step->x - x)));
        Facts f = Seen(units);
        f.anyInCombat = fight;
        f.leadInCombat = fight;
        a.partyInCombat = fight;
        std::string const order = PL::Tick(run, pls, f, a);
        orders.push_back(order);
        if (order.compare(0, 4, "pull") == 0)
            pulledSteps.push_back(run.step);
    }
};

void TestGmWalksToTheFirstBoss()
{
    Walk w;
    std::map<std::uint32_t, bool> alive{ { 4851, true }, { 4852, true }, { 4853, true } };
    // The entrance: nothing in reach (the first pack is 40 yd away: inside the default pull distance, so he walks on first).
    float x = -60;
    for (; x < 25; x += 5)
        w.Second(x, true, alive);
    for (std::string const& o : w.orders)
        CHECK(o.empty());                                   // he is walking: no pull, no order at all
    CHECK(w.pulledSteps.empty());
    // He stops 30 yd before pack one: after two quiet seconds the tank pulls it.
    for (int i = 0; i < 4; ++i)
        w.Second(10, false, alive);
    CHECK(w.pulledSteps.size() == 1 && w.pulledSteps.back() == 1);
    // The fight, then the pack dies; the run does not walk, it waits for him.
    for (int i = 0; i < 3; ++i)
        w.Second(10, false, alive, true);
    alive[4851] = false;
    for (int i = 0; i < 6; ++i)
        w.Second(10, false, alive);
    CHECK(w.pls.done.count(1) == 1);
    // He walks to pack two and stands: pulled in plan order.
    for (float p = 10; p <= 100; p += 10)
        w.Second(p, true, alive);
    for (int i = 0; i < 4; ++i)
        w.Second(100, false, alive);
    CHECK(w.pulledSteps.size() == 2 && w.pulledSteps.back() == 3);
    for (int i = 0; i < 3; ++i)
        w.Second(100, false, alive, true);
    alive[4852] = false;
    for (int i = 0; i < 6; ++i)
        w.Second(100, false, alive);
    // Pack three, then the boss: the first boss step is picked when he stands near it, and the pull is the boss's.
    for (float p = 100; p <= 140; p += 10)
        w.Second(p, true, alive);
    for (int i = 0; i < 4; ++i)
        w.Second(140, false, alive);
    CHECK(w.pulledSteps.size() == 3 && w.pulledSteps.back() == 4);
    for (int i = 0; i < 3; ++i)
        w.Second(140, false, alive, true);
    alive[4853] = false;
    for (int i = 0; i < 6; ++i)
        w.Second(140, false, alive);
    CHECK(w.pls.done.count(4) == 1);
    for (float p = 140; p <= 175; p += 7)
        w.Second(p, true, alive);
    for (int i = 0; i < 4; ++i)
        w.Second(175, false, alive);
    CHECK(w.pls.picked == 5 || w.pls.done.count(5) == 1);   // the boss step is the run's current step
    CHECK(w.run.step == 5);
    // the boss is the fourth pull, and it is his: the tank pulled it only once he stood near it
    CHECK(w.pulledSteps.size() == 4 && w.pulledSteps.back() == 5);
    // the plan's order was followed and no order of the whole walk was a walk
    CHECK(w.pulledSteps.size() >= 3 && w.pulledSteps[0] < w.pulledSteps[1] && w.pulledSteps[1] < w.pulledSteps[2]);
    for (std::string const& o : w.orders)
        CHECK(!Walks(o));
    // the pack after the boss was never touched before he came to it
    for (size_t step : w.pulledSteps)
        CHECK(step != 6);
}

// The default stays bot-led: with the state off the policy's own tick runs and walks the plan itself.
void TestBotLedStaysTheDefault()
{
    DungeonPlan plan = Route();
    Run r = Start(&plan);
    PL::State off;                      // on = false
    Facts f = Seen({});
    f.leadX = -60;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = PL::Tick(r, off, f, Player(-60, false));
    CHECK(Walks(order));
}

// A pack the tank cannot see yet (stealthed) 35 yd ahead of it: the bot-led machine walks through to it. A player-led run never sends the walk (he leads the way).
void TestNoWalkGoesOut()
{
    Walk w;
    PL::Anchor a = Player(10, false);
    PL::Pick(w.run, w.pls, a, {});
    CHECK(w.pls.picked == 1);
    UnitFact hidden = Unit(11, 4851, 30);
    hidden.stealthed = true;
    Facts f = Seen({ hidden });
    f.leadX = 5;
    PL::Tick(w.run, w.pls, f, a);
    std::string const order = PL::Tick(w.run, w.pls, f, a);
    CHECK(order.empty());
    CHECK(w.run.note.find("not sent: move") != std::string::npos);
    // the same facts on a bot-led run: the policy's own tick does send the walk
    Run b = Start(&w.plan);
    b.step = 1;
    CHECK(Walks(Tick(b, f)));
}

// The starter is down or away: the bots hold where they are and follow him again when he is back.
void TestHeldWhenHeIsAway()
{
    Walk w;
    PL::Anchor a = Player(10, false);
    PL::Pick(w.run, w.pls, a, {});
    a.onMap = false;
    std::string order = PL::Tick(w.run, w.pls, Seen({}), a);
    CHECK(order == "stay");
    a = Player(10, false);
    order = PL::Tick(w.run, w.pls, Seen({}), a);
    CHECK(order == "follow");
}
}

int main()
{
    TestLeadWord();
    TestStartAccepts();
    TestStartRefusals();
    TestGmWalksToTheFirstBoss();
    TestBotLedStaysTheDefault();
    TestNoWalkGoesOut();
    TestHeldWhenHeIsAway();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
