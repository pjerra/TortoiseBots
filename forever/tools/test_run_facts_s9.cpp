// Standalone check of package F9 (object, wait and task facts, forever/src/host/run/TbRunFactsPure.h): no game types, no server.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_facts_s9.cpp -o /tmp/test_run_facts_s9 && /tmp/test_run_facts_s9
//
// A small Deadmines-shaped plan (a door to open, a door to wait for, a chest to loot, the cannon to open with its item, a door the cannon blows, a clear
// wait) is read by the glue's pure half with a scripted world, and the run leader's own decision header (DungeonRun::Tick) is fed the result: the facts must
// give the orders and the step ends the policy expects (open / loot orders, the wait_object state words, wait_clear, the carrier and the item rules).
#include "forever/src/host/run/TbRunFactsPure.h"
#include "forever/src/host/run/TbRunTasksPure.h"

#include <cstdio>
#include <memory>

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;
namespace fx = TortoiseBots::run::facts;
namespace tk = TortoiseBots::run::tasks;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
enum Index : std::size_t { kOpenDoor = 0, kWaitDoor, kLootChest, kOpenCannon, kWaitBlast, kWaitClear, kSteps };

std::shared_ptr<ai::DungeonPlanTable> MakePlan()
{
    auto table = std::make_shared<ai::DungeonPlanTable>();
    table->AddPlan("s9test", 36);
    table->AddLeg("s9test", 0, "doors");
    auto add = [&](std::uint16_t seq, char const* kind, std::uint32_t entry, float x, float y, float z, float radius, std::uint32_t item, std::uint32_t param,
                   std::uint32_t timeoutS) {
        PlanStep step;
        step.leg = 0;
        step.seq = seq;
        ai::ParsePlanStepRowKind(kind, step.kind, step.exactLaunch, step.openProof);
        step.entry = entry;
        step.x = x; step.y = y; step.z = z;
        step.radius = radius;
        step.item = item;
        step.param = param;
        step.timeoutS = std::uint16_t(timeoutS);
        auto refusal = table->AddStep("s9test", step);
        if (refusal != ai::PlanStepRefusal::None)
            std::printf("step %u refused\n", seq);
    };
    add(0, "open", 17153, 10, 0, 0, 0, 0, 0, 0);
    add(1, "wait_object", 17153, 10, 0, 0, 0, 0, 1, 60);
    add(2, "loot", 17155, 20, 0, 0, 0, 5397, 0, 0);
    add(3, "open", 16398, 20, 5, 0, 0, 5397, 0, 0);
    add(4, "wait_object", 16397, 22, 8, 0, 0, 0, 2, 60);
    add(5, "wait_clear", 0, 30, 0, 0, 15, 0, 0, 90);
    table->Seal();
    return table;
}

// The scripted world: what the core reader would see.
class World : public fx::StepWorld
{
public:
    fx::ObjectObs object;
    std::vector<fx::ClearObs> hostiles;
    fx::TaskView view;
    int retired = 0;
    bool bags = true;
    std::set<std::string> notKnown;

    fx::PathObs PathFromLead(float, float, float) override { return fx::PathObs(); }
    bool LeadSees(float, float, float) override { return true; }
    std::vector<fx::CreatureObs> CreaturesOfEntry(std::uint32_t, float) override { return {}; }
    bool OwnBoss(ai::PlanStep const&, fx::OwnBossObs&) override { return false; }
    bool BossSpawns(ai::PlanStep const&, std::vector<fx::BossSpawnObs>&) override { return false; }
    void HopCandidates(float, float, float, float, std::vector<fx::HopObs>&) override {}
    bool GroundUnder(fx::CreatureObs const&, float, float&) override { return false; }
    void HostilesNear(float, std::vector<fx::ForwardObs>&) override {}
    fx::ObjectObs ObjectAt(ai::PlanStep const&) override { return object; }
    void ClearHostiles(float, std::vector<fx::ClearObs>& out) override { out = hostiles; }
    bool TaskBags(ai::PlanStep const& step, fx::TaskView& out) override
    {
        if (!bags)
            return false;
        out = view;
        out.inputs.clear();
        out.inputs.push_back(step.kind == ai::PlanStepKind::Open || step.kind == ai::PlanStepKind::UseOn ? step.item : step.entry);
        if (step.param)
            out.inputs.push_back(step.param);
        return true;
    }
    void RetireTask() override
    {
        ++retired;
        RetireOrder(const_cast<Run&>(*runRef));
        view.taskCarrierKey = 0;
        view.reply = 0;
        view.replyText.clear();
    }
    void NotKnown(char const* field) override { notKnown.insert(field); }
    Run const* runRef = nullptr;
};

fx::BagObs Bag(char const* name, std::uint64_t key, bool hasItem5397 = false)
{
    fx::BagObs b;
    b.fact.name = name;
    b.fact.guid = key;
    b.hasStepItem = hasItem5397;
    if (hasItem5397)
        b.fact.held.push_back(5397);
    return b;
}

// One tick's Facts for a lead standing at (x, y): the five-member party of the policy's own scenarios, the lead read, then the step.
void Read(Facts& f, World& w, Run& r, float x, float y)
{
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    char const* names[5] = { "Mtank", "Mpriest", "Mdpsa", "Mdpsb", "Mdpsc" };
    std::vector<fx::PartyEntry> party;
    for (int i = 0; i < 5; ++i)
    {
        fx::PartyEntry e;
        e.name = names[i];
        e.isLead = i == 0;
        e.present = e.alive = true;
        e.healer = i == 1;
        e.dps = i >= 2;
        e.usesMana = i < 2;
        MemberFact& m = e.fact;
        m.name = e.name;
        m.yd = i == 0 ? 0.0f : 2.0f;
        m.x = x + m.yd; m.y = y; m.z = 0;
        m.tank = i == 0; m.healer = i == 1; m.core = i < 2; m.level = 22;
        m.actedMs = 0xFFFFFFFFu;
        party.push_back(e);
    }
    fx::RollUpParty(f, party);
    fx::LeadObs lo;
    lo.x = x; lo.y = y; lo.z = 0;
    lo.name = "Mtank";
    lo.ground.mmapLoaded = true;
    lo.iconsRead = true;
    fx::ReadLeadFacts(f, lo);
    f.pointLiquid = [](float, float, float) { return false; };
    fx::LeadView lead;
    lead.x = x; lead.y = y; lead.z = 0; lead.alive = true; lead.mapId = 36; lead.name = "Mtank";
    w.runRef = &r;
    DungeonRun::MoveProbeCache probe;
    fx::ReadStepFacts(f, r, lead, probe, w);
}

Run At(ai::DungeonPlan const* plan, std::size_t step)
{
    return Start(plan, step);
}

void TestObjectState()
{
    CHECK(fx::PlanObjectState(1) == 0);            // GO_STATE_READY: a closed door is the plan's `ready`
    CHECK(fx::PlanObjectState(0) == 1);            // GO_STATE_ACTIVE: an open door is the plan's `active`
    CHECK(fx::PlanObjectState(2) == 2);            // GO_STATE_DESTROYED: the door the cannon blew
    CHECK(fx::PlanObjectState(24) == 2);
}

void TestWaitObject(ai::DungeonPlan const* plan)
{
    World w;
    Run r = At(plan, kWaitDoor);
    Facts f;
    w.object.found = true;
    w.object.coreState = 1;
    w.object.readable = true;
    Read(f, w, r, 10, 0);
    CHECK(f.objectState == 0 && f.objectReadable);
    // the door opens: the plan's word 1 = the step's param, so the policy ends the step (after its own confirm time)
    w.object.coreState = 0;
    Facts g;
    Read(g, w, r, 10, 0);
    CHECK(g.objectState == 1 && g.objectState == int(plan->steps[kWaitDoor].param));
    // not found, readably: -1 and readable; not found, not readable: -1 and not readable (never "gone")
    w.object = fx::ObjectObs();
    w.object.readable = true;
    Facts h;
    Read(h, w, r, 10, 0);
    CHECK(h.objectState == -1 && h.objectReadable);
    w.object.readable = false;
    Facts i;
    Read(i, w, r, 10, 0);
    CHECK(i.objectState == -1 && !i.objectReadable);
    // the blast door: state 2
    Run blast = At(plan, kWaitBlast);
    w.object = fx::ObjectObs();
    w.object.found = true;
    w.object.coreState = 2;
    w.object.readable = true;
    Facts j;
    Read(j, w, blast, 22, 8);
    CHECK(j.objectState == 2 && j.objectState == int(plan->steps[kWaitBlast].param));
    // the policy ends the wait on the state it reads (the stale-confirm time is its own): drive the ticks and see the step advance
    Run drive = At(plan, kWaitDoor);
    w.object = fx::ObjectObs();
    w.object.found = true;
    w.object.coreState = 0;
    w.object.readable = true;
    for (int tick = 0; tick < 20 && drive.step == kWaitDoor; ++tick)
    {
        Facts k;
        Read(k, w, drive, 10, 0);
        (void)Tick(drive, k, kTickMs);
    }
    CHECK(drive.step != kWaitDoor);
    // a door that stays closed does not end the step
    Run closed = At(plan, kWaitDoor);
    w.object.coreState = 1;
    for (int tick = 0; tick < 10; ++tick)
    {
        Facts k;
        Read(k, w, closed, 10, 0);
        (void)Tick(closed, k, kTickMs);
    }
    CHECK(closed.step == kWaitDoor);
}

void TestWaitClear(ai::DungeonPlan const* plan)
{
    World w;
    Run r = At(plan, kWaitClear);
    w.hostiles = { fx::ClearObs{ 32, 0, 0, true }, fx::ClearObs{ 60, 0, 0, false } };      // one within the 15 yd radius of (30,0), one far
    Facts f;
    Read(f, w, r, 30, 0);
    CHECK(f.clearUnits.size() == 1 && f.clearUnits[0].engaged);
    CHECK(WaitClearCount(plan->steps[kWaitClear], f) == 1);
    // the policy waits while a hostile is there and ends the step when none is left
    Run drive = At(plan, kWaitClear);
    for (int tick = 0; tick < 6; ++tick)
    {
        Facts k;
        Read(k, w, drive, 30, 0);
        (void)Tick(drive, k, kTickMs);
    }
    CHECK(drive.step == kWaitClear);
    w.hostiles.clear();
    for (int tick = 0; tick < 10 && drive.step == kWaitClear; ++tick)
    {
        Facts k;
        Read(k, w, drive, 30, 0);
        CHECK(k.clearUnits.empty());
        (void)Tick(drive, k, kTickMs);
    }
    CHECK(drive.step != kWaitClear);
}

void TestOpenAndCarrier(ai::DungeonPlan const* plan)
{
    World w;
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2), Bag("Mdpsa", 3) };
    // an open with no item: the lead bot is the carrier and the order goes out to it
    Run r = At(plan, kOpenDoor);
    Facts f;
    Read(f, w, r, 10, 0);
    CHECK(f.carrier == "Mtank" && !f.carrierHasItem && !f.carrierBusy && f.taskReply == 0);
    std::string order = Tick(r, f, kTickMs);
    CHECK(order == "open " + EntryArg(17153, 10, 0, 0) + " Mtank");
    CHECK(r.orderSent);
    // the task answers ACK: the step ends
    w.view.taskCarrierKey = 1;
    w.view.reply = 1;
    w.view.replyText = "opened";
    Facts g;
    Read(g, w, r, 10, 0);
    CHECK(g.taskReply == 1 && g.taskText == "opened" && !g.carrierBusy);
    (void)Tick(r, g, kTickMs);
    CHECK(r.step == kWaitDoor);
    // a busy carrier is read (the policy waits for it)
    Run busy = At(plan, kOpenDoor);
    w.view.busyKeys = { 1 };
    w.view.taskCarrierKey = 0;
    w.view.reply = 0;
    Facts h;
    Read(h, w, busy, 10, 0);
    CHECK(h.carrierBusy);
    CHECK(Tick(busy, h, kTickMs).empty());
}

void TestItemCarrier(ai::DungeonPlan const* plan)
{
    // the cannon needs item 5397: the bot that holds it is the carrier, whoever it is
    World w;
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2, true), Bag("Mdpsa", 3) };
    Run r = At(plan, kOpenCannon);
    Facts f;
    Read(f, w, r, 20, 5);
    CHECK(f.carrier == "Mpriest" && f.carrierHasItem);
    std::string const order = Tick(r, f, kTickMs);
    CHECK(order == "open " + EntryArg(16398, 20, 5, 0) + " Mpriest");
    // the lead bot keeps it when it holds the item too
    w.view.bags = { Bag("Mtank", 1, true), Bag("Mpriest", 2, true) };
    Run lead = At(plan, kOpenCannon);
    Facts g;
    Read(g, w, lead, 20, 5);
    CHECK(g.carrier == "Mtank");
    // nobody holds it: the lead bot, no item: the policy pauses with the ask (it does not send an open it knows cannot work)
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2) };
    Run none = At(plan, kOpenCannon);
    Facts h;
    Read(h, w, none, 20, 5);
    CHECK(h.carrier == "Mtank" && !h.carrierHasItem);
    CHECK(Tick(none, h, kTickMs).empty() && none.state == State::Paused);
    // a dead bot that holds it is not the carrier; the split names who holds what
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2, true) };
    w.view.bags[1].fact.away = "dead";
    Run dead = At(plan, kOpenCannon);
    Facts i;
    Read(i, w, dead, 20, 5);
    CHECK(i.carrier == "Mtank" && !i.itemsSplit.empty() && i.itemsSplit.find("Mpriest") != std::string::npos);
    // an order out whose bot died is retired, and the step is read again from a fresh pick
    w.view.bags = { Bag("Mtank", 1, true), Bag("Mpriest", 2, true) };
    Run out = At(plan, kOpenCannon);
    out.orderSent = true;
    w.view.taskCarrierKey = 2;
    w.view.bags[1].fact.away = "dead";
    Facts j;
    Read(j, w, out, 20, 5);
    CHECK(w.retired == 1 && !out.orderSent);
    CHECK(j.carrier == "Mtank" && j.taskReply == 0);
    // a world that cannot read the bags leaves the facts at "nothing there"
    World blind;
    blind.bags = false;
    Run lost = At(plan, kOpenCannon);
    Facts k;
    Read(k, blind, lost, 20, 5);
    CHECK(k.carrier == "Mtank" && !k.carrierHasItem && k.taskReply == 0);
}

void TestLoot(ai::DungeonPlan const* plan)
{
    World w;
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2), Bag("Mdpsa", 3) };
    // a loot step: the order goes to the lead bot, by the entry at the plan's point (no remembered corpse)
    Run r = At(plan, kLootChest);
    Facts f;
    Read(f, w, r, 20, 0);
    CHECK(f.carrier == "Mtank" && !f.partyHasItem);
    std::string const order = Tick(r, f, kTickMs);
    CHECK(order == "loot " + EntryArg(17155, 20, 0, 0) + " Mtank 5397");
    // the item is already in a party bot's bags: the step ends without an order (a quest item of a member, or a loot already taken)
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2, true) };
    Run have = At(plan, kLootChest);
    Facts g;
    Read(g, w, have, 20, 0);
    CHECK(g.partyHasItem && g.itemHolder == "Mpriest");
    CHECK(Tick(have, g, kTickMs).empty() && have.step == kOpenCannon);
    // a refusal on the loot rules moves the order to the bot whose turn it is: facts say whose, after a refusal, from the remembered corpse
    w.view.bags = { Bag("Mtank", 1), Bag("Mpriest", 2), Bag("Mdpsa", 3) };
    Run refused = At(plan, kLootChest);
    CorpseMark mark;
    mark.guid = 99;
    refused.corpses[plan->steps[kLootChest].entry] = mark;
    refused.lootRefusal = std::string("failed: ") + tk::LootRightsText();
    refused.lootRights = true;
    w.view.corpse.checked = true;
    w.view.corpse.hasItem = true;
    w.view.corpse.turn = "Mpriest";
    Facts h;
    Read(h, w, refused, 20, 0);
    CHECK(h.corpseChecked && h.corpseHasItem && h.lootTurn == "Mpriest");
    // no refusal: the corpse is not read
    Run plain = At(plan, kLootChest);
    plain.corpses[plan->steps[kLootChest].entry] = mark;
    Facts i;
    Read(i, w, plain, 20, 0);
    CHECK(!i.corpseChecked && i.lootTurn.empty());
    // the loot method is the glue's own read (core half), the FFA detour stays off: lootFfaOn is forced false by ApplyConfig
    CHECK(!f.lootFfaOn);
}

void TestUnreadSteps(ai::DungeonPlan const* plan)
{
    // a world with no answers for the new reads keeps the defaults and says what it did not know
    class Blind : public fx::StepWorld
    {
    public:
        std::set<std::string> notKnown;
        fx::PathObs PathFromLead(float, float, float) override { return fx::PathObs(); }
        bool LeadSees(float, float, float) override { return false; }
        std::vector<fx::CreatureObs> CreaturesOfEntry(std::uint32_t, float) override { return {}; }
        bool OwnBoss(ai::PlanStep const&, fx::OwnBossObs&) override { return false; }
        bool BossSpawns(ai::PlanStep const&, std::vector<fx::BossSpawnObs>&) override { return false; }
        void HopCandidates(float, float, float, float, std::vector<fx::HopObs>&) override {}
        bool GroundUnder(fx::CreatureObs const&, float, float&) override { return false; }
        void HostilesNear(float, std::vector<fx::ForwardObs>&) override {}
        void NotKnown(char const* field) override { notKnown.insert(field); }
    } blind;
    Run r = At(plan, kWaitDoor);
    Facts f;
    fx::LeadView lead;
    lead.x = 10; lead.y = 0; lead.alive = true; lead.mapId = 36; lead.name = "Mtank";
    DungeonRun::MoveProbeCache probe;
    fx::ReadStepFacts(f, r, lead, probe, blind);
    CHECK(f.objectState == -1 && !f.objectReadable && blind.notKnown.count("objectState"));
    Run c = At(plan, kWaitClear);
    Facts g;
    fx::ReadStepFacts(g, c, lead, probe, blind);
    CHECK(g.clearUnits.empty() && blind.notKnown.count("clearUnits"));
    Run o = At(plan, kOpenCannon);
    Facts h;
    fx::ReadStepFacts(h, o, lead, probe, blind);
    CHECK(h.carrier.empty() && !h.carrierHasItem && blind.notKnown.count("carrier"));
}
}

int main()
{
    auto table = MakePlan();
    ai::DungeonPlan const* plan = table->Find("s9test");
    if (!plan || plan->steps.size() != kSteps)
    {
        std::printf("plan not built (%zu steps)\n", plan ? plan->steps.size() : 0);
        return 2;
    }
    TestObjectState();
    TestWaitObject(plan);
    TestWaitClear(plan);
    TestOpenAndCarrier(plan);
    TestItemCarrier(plan);
    TestLoot(plan);
    TestUnreadSteps(plan);
    std::printf("test_run_facts_s9: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
