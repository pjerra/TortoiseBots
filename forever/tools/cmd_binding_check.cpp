// Standalone test of the PURE half of the commander binding card (forever/src/host/cmd/TbCmdBindingPure.h; package P1, forever/docs/CMD-P1-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/cmd_binding_check.cpp -o /tmp/cmd_binding_check && /tmp/cmd_binding_check
// It runs next to the Turtle telemetry header (runtime/CommanderTelemetry.h), which builds the COMMANDERS datagram, so the format is the Turtle one by construction.
#include "forever/src/host/cmd/TbCmdBindingPure.h"

#include <cstdio>
#include <string>

namespace P = TortoiseBots::cmd::pure;
namespace CT = TortoiseBots::CommanderTelemetry;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// guid counters of the fixture
constexpr uint32_t Tank = 10, Heal = 11, Dps1 = 12, Dps2 = 13, Dps3 = 14, Cmd = 20, Cmd2 = 21, Other = 30;
constexpr uint32_t Human = 40, UnboundBot = 41;   // in the leader's party, not bound to it

std::vector<P::Pair> PartyBoundTo(uint32_t commander, std::initializer_list<uint32_t> bots)
{
    std::vector<P::Pair> out;
    for (uint32_t b : bots)
        out.push_back({ b, commander });
    return out;
}

P::Card const* CardOf(std::vector<P::Card> const& cards, uint32_t commander)
{
    for (P::Card const& c : cards)
        if (c.commander == commander)
            return &c;
    return nullptr;
}

// ---- the cards ----
void TestCards()
{
    std::map<uint32_t, uint32_t> noGroups;

    // Two bots with one commander make ONE record, the lead first (the lead named by `.tb master party`).
    {
        auto const cards = P::BuildCards(PartyBoundTo(Cmd, { Heal, Tank }), {}, { { Cmd, Tank, 1000 } }, noGroups);
        CHECK(cards.size() == 1);
        CHECK(cards[0].commander == Cmd && cards[0].lead == Tank && !cards[0].system && cards[0].run == -1);
        CHECK((cards[0].bots == std::vector<uint32_t>{ Tank, Heal }));
        CHECK(cards[0].boundAt == 1000);
    }
    // No lead named: the leader of the first bot's group, else the first bot.
    {
        auto cards = P::BuildCards(PartyBoundTo(Cmd, { Heal, Tank }), {}, {}, { { Heal, Tank } });
        CHECK(cards.size() == 1 && cards[0].lead == Tank && cards[0].bots.front() == Tank);
        cards = P::BuildCards(PartyBoundTo(Cmd, { Heal, Tank }), {}, {}, noGroups);
        CHECK(cards.size() == 1 && cards[0].lead == Heal);
        // A lead that is no longer bound is not taken.
        cards = P::BuildCards(PartyBoundTo(Cmd, { Heal }), {}, { { Cmd, Tank, 5 } }, noGroups);
        CHECK(cards.size() == 1 && cards[0].lead == Heal);
    }
    // KEY BY COMMANDER: two commanders whose bots stand in the SAME group are two cards (a card keyed by group would merge them); one commander with
    // bots in two groups is one card.
    {
        std::vector<P::Pair> pairs = PartyBoundTo(Cmd, { Tank, Heal });
        for (P::Pair const& p : PartyBoundTo(Cmd2, { Dps1, Dps2 }))
            pairs.push_back(p);
        std::map<uint32_t, uint32_t> sameGroup = { { Tank, Tank }, { Heal, Tank }, { Dps1, Tank }, { Dps2, Tank } };
        auto const cards = P::BuildCards(pairs, {}, {}, sameGroup);
        CHECK(cards.size() == 2);
        CHECK(CardOf(cards, Cmd) && CardOf(cards, Cmd)->bots.size() == 2);
        CHECK(CardOf(cards, Cmd2) && CardOf(cards, Cmd2)->bots.size() == 2);
        std::map<uint32_t, uint32_t> twoGroups = { { Tank, Tank }, { Heal, Tank }, { Dps1, Dps1 }, { Dps2, Dps1 } };
        auto const one = P::BuildCards(PartyBoundTo(Cmd, { Tank, Heal, Dps1, Dps2 }), {}, {}, twoGroups);
        CHECK(one.size() == 1 && one[0].bots.size() == 4);
    }
    // A bot bound to itself, or with an empty guid, is no binding.
    {
        auto const cards = P::BuildCards({ { Tank, Tank }, { 0, Cmd }, { Heal, 0 } }, {}, {}, noGroups);
        CHECK(cards.empty());
    }
    // THE FAILING CASE: a run's system binding shows ONCE. The run made 4 pairs (members -> the tank); with a record + the run it would be two cards.
    {
        P::RunIn run;
        run.leader = Tank;
        run.members = { Tank, Heal, Dps1, Dps2, Dps3 };
        run.index = 3;
        auto const cards = P::BuildCards(PartyBoundTo(Tank, { Heal, Dps1, Dps2, Dps3 }), { run }, {}, noGroups);
        CHECK(cards.size() == 1);
        CHECK(cards[0].commander == Tank && cards[0].system && cards[0].run == 3);
        CHECK(cards[0].lead == Tank && cards[0].bots.size() == 5 && cards[0].bots.front() == Tank);
    }
    // A commanded party that runs: the run's members are folded into the commander's card (the leader kept its commander), not shown as a second one.
    {
        std::vector<P::Pair> pairs = PartyBoundTo(Tank, { Heal, Dps1, Dps2, Dps3 });   // the run's pairs
        pairs.push_back({ Tank, Cmd });                                                // the tank itself stays commanded
        P::RunIn run;
        run.leader = Tank;
        run.leaderCommander = Cmd;
        run.members = { Tank, Heal, Dps1, Dps2, Dps3 };
        run.index = 0;
        auto const cards = P::BuildCards(pairs, { run }, {}, noGroups);
        CHECK(cards.size() == 1);
        CHECK(cards[0].commander == Cmd && !cards[0].system && cards[0].run == 0 && cards[0].bots.size() == 5);
        // The commander's bots outside the run's party stay on its card.
        pairs.push_back({ Other, Cmd });
        auto const more = P::BuildCards(pairs, { run }, {}, noGroups);
        CHECK(more.size() == 1 && more[0].bots.size() == 6 && P::Has(more[0].bots, Other));
    }
    // A run that is not in the list (ended) leaves the pairs as they are.
    {
        auto const cards = P::BuildCards(PartyBoundTo(Cmd, { Tank, Heal }), {}, {}, noGroups);
        CHECK(cards.size() == 1 && cards[0].run == -1);
    }
    // A run and a plain commander side by side are two cards, sorted by commander guid.
    {
        std::vector<P::Pair> pairs = PartyBoundTo(Tank, { Heal });
        for (P::Pair const& p : PartyBoundTo(Cmd, { Other }))
            pairs.push_back(p);
        P::RunIn run;
        run.leader = Tank;
        run.members = { Tank, Heal };
        auto const cards = P::BuildCards(pairs, { run }, {}, noGroups);
        CHECK(cards.size() == 2 && cards[0].commander == Tank && cards[1].commander == Cmd);
    }
    // The named lead is kept even when it is the first bound bot and its party leader is another bound bot (the fallback must not overwrite it).
    {
        auto const cards = P::BuildCards(PartyBoundTo(Cmd, { Tank, Heal }), {}, { { Cmd, Tank, 7 } }, { { Tank, Heal } });
        CHECK(cards.size() == 1 && cards[0].lead == Tank);
    }
    // Every card is returned: the telemetry cap belongs to the dashboard serializer, never to the collection the module prunes its state from.
    {
        std::vector<P::Pair> pairs;
        for (uint32_t c = 100; c < 100 + 30; ++c)
            pairs.push_back({ c + 1000, c });
        auto const all = P::BuildCards(pairs, {}, {}, noGroups);
        CHECK(all.size() == 30);
        CHECK(all.size() > CT::kMaxCommandersReported);
        CHECK(all.front().commander == 100 && all.back().commander == 129);
    }
    // A run's card counts only the bots h3 binds to its leader: an unbound party member (a human player or an unbound bot) is never on it.
    {
        P::RunIn run;
        run.leader = Tank;
        run.members = { Tank, Heal, Human, UnboundBot };
        auto const cards = P::BuildCards(PartyBoundTo(Tank, { Heal }), { run }, {}, noGroups);
        CHECK(cards.size() == 1 && cards[0].system && cards[0].lead == Tank);
        CHECK((cards[0].bots == std::vector<uint32_t>{ Tank, Heal }));
        // The run's own bound members are enough while the leader is not in the world and its live group cannot be read.
        run.members = { Heal };
        auto const offWorld = P::BuildCards(PartyBoundTo(Tank, { Heal }), { run }, {}, noGroups);
        CHECK(offWorld.size() == 1 && (offWorld[0].bots == std::vector<uint32_t>{ Tank, Heal }));
    }
}

// ---- the per-card order tallies ----
void TestTally()
{
    std::map<uint32_t, uint32_t> cardOfBot = { { Tank, Cmd }, { Heal, Cmd }, { Dps1, Cmd2 }, { Dps2, Cmd2 } };
    auto const tallies = P::TallyByCard({ { Tank, "" }, { Heal, "" }, { Dps1, "no-engine" }, { Dps2, "not-in-group" } }, cardOfBot);
    CHECK(tallies.size() == 2);
    CHECK(tallies.at(Cmd).queued == 2 && tallies.at(Cmd).refused == 0 && tallies.at(Cmd).firstRefusal.empty());
    CHECK(tallies.at(Cmd2).queued == 0 && tallies.at(Cmd2).refused == 2 && tallies.at(Cmd2).firstRefusal == "no-engine");
    // A mixed card keeps its own first refusal and both counts; a bot without a card is not counted anywhere.
    auto const mixed = P::TallyByCard({ { Tank, "" }, { Heal, "no-pull-strategy" }, { Other, "" } }, cardOfBot);
    CHECK(mixed.size() == 1);
    CHECK(mixed.at(Cmd).queued == 1 && mixed.at(Cmd).refused == 1 && mixed.at(Cmd).firstRefusal == "no-pull-strategy");
}

// ---- the order log ----
void TestOrders()
{
    CT::OrderLog log;
    for (int i = 0; i < 12; ++i)
        log.Note(Cmd, P::MakeOrder(1000 + i, "stay", std::to_string(i), true, "", "queued 5 of 5"));
    auto const recent = log.Recent(Cmd);
    CHECK(recent.size() == CT::kMaxOrdersPerCommander);
    CHECK(recent.front().detail == "4" && recent.back().detail == "11");   // the last N, oldest first
    CHECK(log.Recent(Cmd2).empty());
    log.Note(Cmd2, P::MakeOrder(5, "pull", "mark", false, "no-pull-strategy", "the bot has no pull strategy"));
    CHECK(log.Recent(Cmd2).size() == 1 && !log.Recent(Cmd2)[0].ok && log.Recent(Cmd2)[0].result == "no-pull-strategy: the bot has no pull strategy");
    log.Retain({ Cmd2 });                                // a released binding takes its log with it
    CHECK(log.Recent(Cmd).empty() && log.Recent(Cmd2).size() == 1);

    std::string verb, detail;
    P::SplitRunOrder("pull 12 body", verb, detail);
    CHECK(verb == "pull" && detail == "12 body");
    P::SplitRunOrder("rest", verb, detail);
    CHECK(verb == "rest" && detail.empty());
    P::SplitRunOrder("revive party", verb, detail);
    CHECK(verb == "revive" && detail == "party");
}

// ---- the role ----
void TestRole()
{
    CHECK(P::RoleOfBrainLines({}) == "");
    CHECK(P::RoleOfBrainLines({ "brain: spike" }) == "");
    CHECK(P::RoleOfBrainLines({ "brain: engine", "combat strategies: tank assist, close" }) == "tank");
    CHECK(P::RoleOfBrainLines({ "brain: engine", "combat strategies: holy, cure" }) == "healer");
    CHECK(P::RoleOfBrainLines({ "brain: engine", "combat strategies: arcane" }) == "dps");
}

P::CardView FixtureView()
{
    P::CardView v;
    v.commanderName = "Mcmd";
    v.commanderGuid = Cmd;
    v.commanderOnline = true;
    v.commanderPlaced = true;
    v.commanderMap = 389;
    v.commanderX = 3.5f;
    v.commanderY = -7.25f;
    v.groupId = 4;
    v.leadName = "Mtank";
    v.boundAt = 1700000000;
    P::BotView tank;
    tank.name = "Mtank";
    tank.guid = Tank;
    tank.className = "warrior";
    tank.role = "tank";
    tank.level = 18;
    tank.hp = 900;
    tank.maxHp = 1000;
    tank.power = 20;
    tank.maxPower = 100;
    tank.powerType = "rage";
    tank.lead = true;
    tank.map = 389;
    tank.zone = 2437;
    tank.x = 1.0f;
    tank.y = 2.0f;
    tank.z = 3.0f;
    P::BotView heal = tank;
    heal.name = "Mheal";
    heal.guid = Heal;
    heal.className = "priest";
    heal.role = "healer";
    heal.powerType = "mana";
    heal.power = 400;
    heal.maxPower = 500;
    heal.lead = false;
    v.bots = { tank, heal };
    v.orders.push_back(P::MakeOrder(1700000100, "stay", "", true, "", "queued 2 of 2"));
    v.orders.push_back(P::MakeOrder(1700000105, "pull", "mark", false, "no-pull-strategy", "give it the tank role"));
    return v;
}

// ---- the datagram ----
void TestGolden()
{
    std::vector<CT::BindingInfo> bindings = { P::BindingOf(FixtureView()) };
    std::string const payload = CT::BuildPayload(12, 7, 1, 1700000200, bindings, 1700000200123LL);
    // Pinned once, read by eye against the Turtle format: view=false (the TBM view is cut), no mob/object/boss layers, orders oldest first.
    static char const* const golden = R"JSON({"v":12,"session":7,"seq":1,"ts":1700000200,"ts_ms":1700000200123,"type":"COMMANDERS","commanders":[{"commander":"Mcmd","commander_guid":20,"bound_at":1700000000,"online":true,"view":false,"group":4,"lead":"Mtank","bots":[{"name":"Mtank","guid":10,"class":"warrior","role":"tank","level":18,"hp":900,"max_hp":1000,"power":20,"max_power":100,"power_type":"rage","alive":true,"lead":true,"map":389,"zone":2437,"x":1.0,"y":2.0,"z":3.0},{"name":"Mheal","guid":11,"class":"priest","role":"healer","level":18,"hp":900,"max_hp":1000,"power":400,"max_power":500,"power_type":"mana","alive":true,"lead":false,"map":389,"zone":2437,"x":1.0,"y":2.0,"z":3.0}],"commander_pos":{"map":389,"x":3.5,"y":-7.2},"orders":[{"at":1700000100,"verb":"stay","detail":"","ok":true,"result":"queued 2 of 2"},{"at":1700000105,"verb":"pull","detail":"mark","ok":false,"result":"no-pull-strategy: give it the tank role"}]}]})JSON";
    if (payload != golden)
        std::printf("golden differs; the payload is:\n%s\n", payload.c_str());
    CHECK(payload == golden);
    // A card with no bound bot in the world and no order still reports its commander.
    P::CardView empty;
    empty.commanderName = "offline #20";
    empty.commanderGuid = Cmd;
    std::string const off = CT::BuildPayload(12, 7, 2, 1700000200, { P::BindingOf(empty) }, 0);
    CHECK(off.find("\"commander\":\"offline #20\"") != std::string::npos && off.find("\"online\":false") != std::string::npos);
}
}

int main()
{
    TestCards();
    TestTally();
    TestOrders();
    TestRole();
    TestGolden();
    std::printf("cmd_binding_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
