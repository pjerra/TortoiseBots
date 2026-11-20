// Standalone check of the Wailing Caverns plan on the run leader (the WC slice; forever/docs/RUN-WC-NOTES.md): no game types, no server.
//   python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_wc.cpp -o /tmp/test_run_wc
//   /tmp/test_run_wc /tmp/wc_rows.tsv
//
// It loads the plan rows the module ships (ai_dungeon_plan/leg/step/step_unit, as the table loader reads them), runs the REAL run leader
// (DungeonRun::Tick of runtime/DungeonRunPolicy.h) over every step with the glue's own pure half (TbRunFactsPure.h) and a scripted world,
// and checks:
//   * every row is accepted by the plan table (no refusal), and the boss rows are exactly the required ones: Kresh, the four Fanglords'
//     keepers, Skum, Verdan and Mutanus (Lady Anacondra has no boss row: she is a pool boss killed inside a pack, see the notes);
//   * the run walks the whole plan to `Done`, from the entrance to Verdan, back down the ramp, the Disciple's gossip, the escort waves and
//     Mutanus: pulls, boss steps, the gossip order, the holds (units and instance data) each end where they should;
//   * the event's own facts are read where the policy expects them (instance data, the wave units, the corpse count) and stay the default
//     elsewhere.
#include "forever/src/host/run/TbRunFactsPure.h"
#include "forever/src/host/run/TbRunTasksPure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;
namespace fx = TortoiseBots::run::facts;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// The plan as a test sees it: the step rows (leg, seq, kind, entry, point, radius, item, param, flags, timeout, entries) and the unit rows.
struct Row
{
    std::uint8_t leg = 0;
    std::uint16_t seq = 0;
    std::string kind;
    std::uint32_t entry = 0;
    float x = 0, y = 0, z = 0, radius = 0;
    std::uint32_t item = 0, param = 0, flags = 0, timeout = 0;
    std::vector<std::uint32_t> entries;
};

bool LoadRows(char const* path, ai::DungeonPlanTable& table, std::vector<Row>& rows, int& unitRows)
{
    std::ifstream in(path);
    if (!in)
        return false;
    table.AddPlan("wailing_caverns", 43);
    std::string line;
    int lastLeg = -1;
    while (std::getline(in, line))
    {
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t'))
            f.push_back(cell);
        if (f[0] == "P")
        {
            // The plan row: map, cc_priority, cc_always, kill_first (the same lists the loader reads).
            while (f.size() < 5)
                f.push_back("");
            if (!table.SetUnits("wailing_caverns", f.size() > 2 ? f[2] : "", f.size() > 3 ? f[3] : "",
                    f.size() > 4 ? f[4] : "", ""))
                return false;
            continue;
        }
        if (f.size() < 9)
            continue;
        if (f[0] == "U")
        {
            ++unitRows;
            continue;
        }
        if (f.size() < 14)
            continue;
        Row r;
        r.leg = std::uint8_t(std::stoi(f[0]));
        r.seq = std::uint16_t(std::stoi(f[2]));
        r.kind = f[3];
        r.entry = std::uint32_t(std::stoul(f[4]));
        r.x = std::stof(f[5]); r.y = std::stof(f[6]); r.z = std::stof(f[7]);
        r.radius = std::stof(f[8]);
        r.item = std::uint32_t(std::stoul(f[9]));
        r.param = std::uint32_t(std::stoul(f[10]));
        r.flags = std::uint32_t(std::stoul(f[11]));
        r.timeout = std::uint32_t(std::stoul(f[12]));
        if (f[13] != "-")
        {
            std::stringstream es(f[13]);
            std::string e;
            while (std::getline(es, e, ','))
                if (!e.empty())
                    r.entries.push_back(std::uint32_t(std::stoul(e)));
        }
        if (r.seq == 0 && int(r.leg) != lastLeg)
        {
            table.AddLeg("wailing_caverns", r.leg, f[1]);
            lastLeg = int(r.leg);
        }
        PlanStep step;
        step.leg = r.leg;
        step.seq = r.seq;
        ai::ParsePlanStepRowKind(r.kind, step.kind, step.exactLaunch, step.openProof);
        step.entry = r.entry;
        step.x = r.x; step.y = r.y; step.z = r.z;
        step.radius = r.radius;
        step.item = r.item; step.param = r.param; step.flags = r.flags;
        step.timeoutS = std::uint16_t(r.timeout);
        step.entries = r.entries;
        if (table.AddStep("wailing_caverns", step) != ai::PlanStepRefusal::None)
            return false;
        rows.push_back(r);
    }
    table.Seal();
    return !rows.empty();
}

struct Mob
{
    std::uint64_t handle = 0;
    std::uint32_t entry = 0;
    float x = 0, y = 0, z = 0;
    bool alive = true;
    bool boss = false;
};

// The scripted world: one plan step at a time. The lead walks to its point, pulls and boss steps fight their pack for a few ticks, a hold's
// wave appears and dies, a gossip answers after two ticks, and the instance data step its holds wait on moves when a hold starts.
class World : public fx::StepWorld
{
public:
    float x = 0, y = 0, z = 0;                  // the lead
    std::size_t stepIndex = std::size_t(-1);
    PlanStep const* step = nullptr;
    std::vector<Mob> mobs;
    std::uint64_t nextHandle = 5000;
    int fightTicks = 0;
    bool walking = false;
    float tx = 0, ty = 0, tz = 0;                // the move order's target
    std::map<std::uint32_t, long long> data;    // instance data by id
    int gossipTick = -1;                        // ticks since the gossip order
    int holdsDone = 0, holdsSeen = 0;
    int unitsHolds = 0, dataHolds = 0;
    std::set<std::string> notKnown;
    long long lastEventData = -1;
    std::size_t unitsHoldReads = 0;

    void Prepare(Run const& r)
    {
        step = Current(r);
        stepIndex = r.step;
        if (!step || stepIndex == holdsStep)
            return;
        holdsStep = stepIndex;
        mobs.clear();
        fightTicks = 0;
        walking = false;
        if (step->kind == PlanStepKind::Pull || step->kind == PlanStepKind::Boss)
        {
            std::vector<std::uint32_t> entries = step->entries;
            if (step->kind == PlanStepKind::Boss && step->entry)
                entries.insert(entries.begin(), step->entry);
            int n = 0;
            for (std::uint32_t e : entries)
            {
                if (n >= 6)
                    break;
                Mob m;
                m.handle = nextHandle++;
                m.entry = e;
                m.x = step->x + float(n) * 2.0f;
                m.y = step->y;
                m.z = step->z;
                m.boss = step->kind == PlanStepKind::Boss && e == step->entry;
                mobs.push_back(m);
                ++n;
            }
            return;
        }
        if (step->kind == PlanStepKind::Hold && !(step->flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
        {
            ++unitsHolds;
            for (std::uint32_t e : step->entries)
            {
                Mob m;
                m.handle = nextHandle++;
                m.entry = e;
                m.x = step->x + 1.0f;
                m.y = step->y + 1.0f;
                m.z = step->z;
                mobs.push_back(m);
            }
            return;
        }
        if (step->kind == PlanStepKind::Hold && (step->flags & ai::kPlanFlagDoneData))
            ++dataHolds;
    }

    std::size_t holdsStep = std::size_t(-1);

    // ---- StepWorld ----
    fx::PathObs PathFromLead(float px, float py, float pz) override
    {
        fx::PathObs p;
        p.valid = true;
        p.points = { PathPoint{ x, y, z }, PathPoint{ (x + px) / 2, (y + py) / 2, (z + pz) / 2 }, PathPoint{ px, py, pz } };
        p.endX = px; p.endY = py; p.endZ = pz;
        p.lengthYd = std::sqrt((px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z));
        return p;
    }
    bool LeadSees(float, float, float) override { return true; }
    std::vector<fx::CreatureObs> CreaturesOfEntry(std::uint32_t entry, float range) override
    {
        std::vector<fx::CreatureObs> out;
        for (Mob const& m : mobs)
        {
            if (m.entry != entry || Dist2(x, y, m.x, m.y) > range)
                continue;
            fx::CreatureObs c;
            c.handle = m.handle;
            c.entry = m.entry;
            c.alive = m.alive;
            c.x = m.x; c.y = m.y; c.z = m.z;
            c.distLead = Dist2(x, y, m.x, m.y);
            c.hasSpawn = true; c.spawnX = m.x; c.spawnY = m.y; c.spawnZ = m.z;
            c.hostile = true; c.attackable = true; c.normalRank = !m.boss; c.level = 20; c.healthPct = 100.0f;
            out.push_back(c);
        }
        return out;
    }
    bool OwnBoss(ai::PlanStep const&, fx::OwnBossObs& out) override
    {
        if (mobs.empty() || !mobs[0].boss)
            return false;
        out.known = true;
        out.loaded = true;
        out.alive = mobs[0].alive;
        out.entry = mobs[0].entry;
        out.handle = mobs[0].handle;
        out.x = mobs[0].x; out.y = mobs[0].y; out.z = mobs[0].z;
        out.rowX = step ? step->x : 0; out.rowY = step ? step->y : 0;
        return true;
    }
    bool BossSpawns(ai::PlanStep const&, std::vector<fx::BossSpawnObs>& out) override
    {
        for (Mob const& m : mobs)
            if (m.boss)
            {
                fx::BossSpawnObs o;
                o.handle = m.handle; o.isOwn = true; o.loaded = true; o.alive = m.alive; o.entry = m.entry;
                o.x = m.x; o.y = m.y; o.z = m.z;
                out.push_back(o);
            }
        return !out.empty();
    }
    void HopCandidates(float, float, float, float, std::vector<fx::HopObs>&) override {}
    bool GroundUnder(fx::CreatureObs const&, float, float&) override { return false; }
    void HostilesNear(float, std::vector<fx::ForwardObs>&) override {}
    fx::ObjectObs ObjectAt(ai::PlanStep const&) override { return fx::ObjectObs(); }
    void ClearHostiles(float, std::vector<fx::ClearObs>&) override {}
    bool InstanceData(std::uint32_t id, long long& out) override
    {
        auto it = data.find(id);
        if (it == data.end())
        {
            notKnown.insert("eventData");
            return false;
        }
        out = it->second;
        lastEventData = out;
        return true;
    }
    bool TaskBags(ai::PlanStep const& step, fx::TaskView& out) override
    {
        char const* names[5] = { "Mtank", "Mpriest", "Mdpsa", "Mdpsb", "Mdpsc" };
        for (int i = 0; i < 5; ++i)
        {
            fx::BagObs b;
            b.fact.name = names[i];
            b.fact.guid = 900 + i;
            if (step.kind == ai::PlanStepKind::Gossip && i == 0 && gossipTick >= 0)
            {
                out.taskCarrierKey = b.fact.guid;
                out.reply = gossipTick >= 2 ? 1 : 0;
                out.replyText = gossipTick >= 2 ? "Let the event begin!" : "";
                out.busyKeys.push_back(b.fact.guid);
            }
            out.bags.push_back(b);
        }
        return true;
    }
    void RetireTask() override {}
    void NotKnown(char const* field) override { notKnown.insert(field); }

    static float Dist2(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }
};

struct Result
{
    std::size_t step = 0;
    State state = State::Running;
    int ticks = 0;
    bool done = false;
    std::map<std::string, int> orders;
    std::vector<std::string> allOrders;
    std::set<std::uint32_t> bossesKilled;
};

Result Walk(ai::DungeonPlan const* plan, int maxTicks)
{
    World w;
    Run r = Start(plan);
    DungeonRun::MoveProbeCache probe;
    Result res;
    // The four Fanglords are dead by the time the party comes back (the event's gate), and the Disciple's state moves as the event goes.
    w.data[4] = 0;      // CWC_TYPE_DISCIPLE: not started
    w.data[5] = 0;      // CWC_TYPE_MUTANUS
    int dataTick = 0;
    for (int tick = 0; tick < maxTicks && !res.done; ++tick)
    {
        w.Prepare(r);
        // The script's own progress, as the instance would show it.
        if (w.step)
        {
            PlanStepKind const kind = w.step->kind;
            if (kind == PlanStepKind::Hold && (w.step->flags & ai::kPlanFlagDoneData))
            {
                if (++dataTick >= 2)
                    w.data[w.step->entry] = static_cast<long long>(w.step->param);
            }
            else
                dataTick = 0;
            if (kind == PlanStepKind::Boss && w.step->entry == 3654 && !w.mobs.empty() && !w.mobs[0].alive)
            {
                w.data[5] = 3;      // Mutanus DONE
                w.data[4] = 3;      // the Disciple's event over
            }
        }
        Facts f;
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
            m.x = w.x + m.yd; m.y = w.y; m.z = w.z;
            m.tank = i == 0; m.healer = i == 1; m.core = i < 2; m.level = 22;
            m.actedMs = 0xFFFFFFFFu;
            party.push_back(e);
        }
        fx::RollUpParty(f, party);
        fx::LeadObs lo;
        lo.x = w.x; lo.y = w.y; lo.z = w.z;
        lo.name = "Mtank";
        lo.ground.mmapLoaded = true; lo.ground.mapFloorZ = w.z;
        lo.guardRead = w.fightTicks > 0;
        lo.isTank = true; lo.staying = !w.walking;
        lo.resting = r.phase == Phase::Rest;
        lo.iconsRead = true;
        if (w.fightTicks > 0)
        {
            lo.stayKnown = true; lo.stayX = w.x; lo.stayY = w.y; lo.stayZ = w.z;
            lo.hit = true; lo.hasAttacker = true;
            lo.attacker.guid = 1000; lo.attacker.entry = w.mobs.empty() ? 3636 : w.mobs[0].entry; lo.attacker.hitting = true;
            lo.reaching = w.fightTicks == 4; lo.reachAggro = 1;
            lo.moveFailReason = "none";
        }
        f.pointLiquid = [](float, float, float) { return false; };
        fx::ReadLeadFacts(f, lo);
        if (w.fightTicks > 0)
        {
            std::vector<fx::EngagedObs> engaged;
            for (Mob const& m : w.mobs)
                if (m.alive)
                {
                    fx::EngagedObs e;
                    e.handle = m.handle; e.entry = m.entry; e.x = m.x; e.y = m.y; e.z = m.z;
                    e.distLead = World::Dist2(w.x, w.y, m.x, m.y);
                    e.partyHits = true; e.hitsParty = true;
                    engaged.push_back(e);
                }
            std::vector<OwnUnitFact> own;
            for (std::uint64_t h : r.ownFightGuids)
            {
                OwnUnitFact o;
                o.guid = h;
                o.state = OwnUnitState::Alive;
                for (Mob const& m : w.mobs)
                    if (m.handle == h)
                        o.state = m.alive ? OwnUnitState::Alive : OwnUnitState::Dead;
                own.push_back(o);
            }
            fx::ReadFightFacts(f, r.plan, engaged, &own, nullptr);
        }
        fx::LeadView lead;
        lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = 43; lead.name = "Mtank";
        fx::ReadStepFacts(f, r, lead, probe, w);
        if (w.step && w.step->kind == PlanStepKind::Hold && !(w.step->flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
            if (!f.units.empty() || f.eventDown)
                ++w.unitsHoldReads;

        std::string order = Tick(r, f, kTickMs);
        if (!order.empty())
        {
            ++res.orders[order.substr(0, order.find(' '))];
            res.allOrders.push_back(order);
            std::istringstream os(order);
            std::string verb;
            os >> verb;
            if (verb == "move")
            {
                os >> w.tx >> w.ty >> w.tz;
                w.walking = true;
            }
            else if (verb == "pull" || verb == "pullback")
                w.fightTicks = 4;
            else if (verb == "gossip")
                w.gossipTick = 0;
        }
        if (w.gossipTick >= 0)
            ++w.gossipTick;
        if (w.walking)
        {
            float const dx = w.tx - w.x, dy = w.ty - w.y, dz = w.tz - w.z;
            float const d = std::sqrt(dx * dx + dy * dy);
            if (d > 0.5f)
            {
                float const st = std::min(14.0f, d);
                w.x += dx / d * st;
                w.y += dy / d * st;
                w.z += dz / std::max(1.0f, d) * st;
            }
            else
                w.walking = false;
        }
        // A fight: the pack takes a few ticks, then dies (the boss with it).
        if (w.fightTicks > 0)
        {
            --w.fightTicks;
            if (w.fightTicks == 0)
                for (Mob& m : w.mobs)
                {
                    m.alive = false;
                    if (m.boss && m.entry)
                        res.bossesKilled.insert(m.entry);
                }
        }
        // A units hold: the wave is fought and dies while it is held.
        if (w.step && w.step->kind == PlanStepKind::Hold && !(w.step->flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
            if (r.step == w.stepIndex && ++w.fightTicks >= 6)
            {
                for (Mob& m : w.mobs)
                    m.alive = false;
            }
        if (r.step != w.stepIndex)
            w.fightTicks = 0;
        res.step = r.step;
        res.state = r.state;
        res.ticks = tick;
        res.done = r.state == State::Done;
    }
    return res;
}

void TestPlanShape(ai::DungeonPlan const* plan, int unitRows)
{
    CHECK(unitRows > 150);
    std::set<std::uint32_t> bosses;
    int pulls = 0, holds = 0, gossip = 0, exact = 0;
    for (PlanStep const& s : plan->steps)
    {
        CHECK(s.leg < plan->legs.size() + 40);
        switch (s.kind)
        {
            case PlanStepKind::Boss: bosses.insert(s.entry); break;
            case PlanStepKind::Pull: ++pulls; CHECK(!s.entries.empty()); CHECK(s.radius > 0.0f); break;
            case PlanStepKind::Hold: ++holds; break;
            case PlanStepKind::Gossip: ++gossip; CHECK(s.entry == 3678); CHECK(s.param >= 1 && s.param <= 15); break;
            case PlanStepKind::MoveDirect: CHECK(s.exactLaunch); ++exact; break;
            default: break;
        }
    }
    CHECK(plan->steps.size() > 400);
    CHECK(pulls > 80);
    CHECK(holds >= 6);
    CHECK(gossip == 1);
    CHECK(exact == 2);          // the lift up to Serpentis's part, and the hop down on the way back
    // The required bosses, per the data (the notes): Kresh, Cobrahn, Pythas, Skum, Serpentis, Verdan, Mutanus.
    std::set<std::uint32_t> const want{ 3653, 3669, 3670, 3674, 3673, 5775, 3654 };
    CHECK(bosses == want);
    CHECK(bosses.count(3671) == 0);     // Lady Anacondra: a pool boss inside a pack, kill_first 3671
    CHECK(std::find(plan->killFirst.begin(), plan->killFirst.end(), 3671u) != plan->killFirst.end());
}

void TestWalk(ai::DungeonPlan const* plan)
{
    Result const res = Walk(plan, 40000);

    CHECK(res.done);
    CHECK(res.orders.count("move") && res.orders.at("move") > 300);
    CHECK(res.orders.count("pullback") && res.orders.at("pullback") > 80);
    // The bosses the run pulled and killed: all seven, Mutanus last.
    CHECK(res.bossesKilled.count(3653) && res.bossesKilled.count(3669) && res.bossesKilled.count(3670) &&
          res.bossesKilled.count(3674) && res.bossesKilled.count(3673) && res.bossesKilled.count(5775));
    CHECK(res.bossesKilled.count(3654));
    // The gossip order the policy sends, once, to the Disciple and the lead bot.
    int gossipOrders = 0;
    for (std::string const& o : res.allOrders)
        if (o.rfind("gossip 3678 Mtank ", 0) == 0)
            ++gossipOrders;
    CHECK(gossipOrders == 1);
}

void TestUnitsHoldFacts(ai::DungeonPlan const* plan)
{
    // The 3636 hold of the escort: the wave is listed while it lives, and the policy ends the hold when it is dead.
    World w;
    Run r = Start(plan);
    std::size_t step = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && plan->steps[i].entries.size() == 1 && plan->steps[i].entries[0] == 3636)
            step = i;
    CHECK(step != 0);
    // Drive to that step the fast way: the policy's step is set by Start(plan, step) in the RFC test's terms; here the hold is read directly.
    Run at = Start(plan, step);
    w.Prepare(at);
    w.x = plan->steps[step].x;
    w.y = plan->steps[step].y;
    w.z = plan->steps[step].z;
    Facts f;
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    fx::LeadView lead;
    lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = 43;
    DungeonRun::MoveProbeCache probe;
    fx::ReadStepFacts(f, at, lead, probe, w);
    CHECK(f.units.size() == 1 && f.units[0].entry == 3636);
    CHECK(f.eventDown == 0);
    w.mobs[0].alive = false;
    Facts g;
    fx::ApplyConfig(g, cfg);
    fx::ReadStepFacts(g, at, lead, probe, w);
    CHECK(g.units.empty() && g.eventDown == 1);
}

void TestDataHoldFacts(ai::DungeonPlan const* plan)
{
    std::size_t step = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && (plan->steps[i].flags & ai::kPlanFlagDoneData))
            step = i;
    CHECK(step != 0);
    World w;
    w.data[plan->steps[step].entry] = 4;
    Run at = Start(plan, step);
    Facts f;
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    fx::LeadView lead;
    lead.x = plan->steps[step].x; lead.y = plan->steps[step].y; lead.z = plan->steps[step].z; lead.alive = true; lead.mapId = 43;
    DungeonRun::MoveProbeCache probe;
    fx::ReadStepFacts(f, at, lead, probe, w);
    CHECK(f.eventData == 4);
    World none;                                  // no instance script: the value stays -1 and the read is named
    Facts g;
    fx::ApplyConfig(g, cfg);
    fx::ReadStepFacts(g, at, lead, probe, none);
    CHECK(g.eventData == -1);
    CHECK(none.notKnown.count("eventData") != 0);
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <wc_rows.tsv>   (python3 forever/tools/wc_plan_rows.py --tsv <file>)\n", argv[0]);
        return 2;
    }
    auto table = std::make_shared<ai::DungeonPlanTable>();
    std::vector<Row> rows;
    int unitRows = 0;
    if (!LoadRows(argv[1], *table, rows, unitRows))
    {
        std::printf("FAIL: the plan rows could not be loaded from %s\n", argv[1]);
        return 1;
    }
    ai::DungeonPlan const* plan = table->Find("wailing_caverns");
    if (!plan)
    {
        std::printf("FAIL: no plan\n");
        return 1;
    }
    TestPlanShape(plan, unitRows);
    TestWalk(plan);
    TestUnitsHoldFacts(plan);
    TestDataHoldFacts(plan);
    std::printf("test_run_wc: %d checks, %d failed (plan: %zu steps, %zu legs, %d unit rows)\n", g_checks, g_fail,
        plan->steps.size(), plan->legs.size(), unitRows);
    return g_fail ? 1 : 0;
}
