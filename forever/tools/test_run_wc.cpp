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

bool LoadRows(char const* path, ai::DungeonPlanTable& table, std::vector<Row>& rows, int& unitRows,
    std::map<std::pair<int, int>, std::vector<Row>>& unitsByStep)
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
            Row u;
            u.leg = std::uint8_t(std::stoi(f[1]));
            u.seq = std::uint16_t(std::stoi(f[2]));
            u.entry = std::uint32_t(std::stoul(f[4]));
            u.x = std::stof(f[5]); u.y = std::stof(f[6]); u.z = std::stof(f[7]);
            unitsByStep[{int(u.leg), int(u.seq)}].push_back(u);
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
        // The event's start NPC walks his road alive through the escort (kPlanFlagEscortNpc): a units hold of the escort ends only
        // while he is read alive, and the real event has him there.
        if (w.step && w.step->kind == PlanStepKind::Hold && (w.step->flags & ai::kPlanFlagEscortNpc))
        {
            bool have = false;
            for (Mob const& m : w.mobs)
                have = have || m.entry == 3678;
            if (!have)
            {
                Mob d;
                d.handle = w.nextHandle++;
                d.entry = 3678;
                d.x = w.step->x + 2.0f; d.y = w.step->y; d.z = w.step->z;
                w.mobs.push_back(d);
            }
        }
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
            m.healerSight = true;               // the walk harness's party stands together: the healer sees the tank
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
                    if (m.entry != 3678)          // the Disciple is not one of the packs the run fights (kPlanFlagEscortNpc)
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
                    if (m.entry != 3678)          // the wave dies; the Disciple walks his road alive (kPlanFlagEscortNpc)
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
        CHECK(s.leg < plan->legs.size());
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
    CHECK(plan->steps.size() == 302);
    CHECK(pulls == 87);
    CHECK(holds == 5);
    CHECK(gossip == 1);
    CHECK(exact == 2);          // the lift up to Serpentis's part, and the hop down on the way back
    // The required bosses, per the data (the notes): Kresh, Cobrahn, Pythas, Skum, Serpentis, Verdan, Mutanus.
    std::set<std::uint32_t> const want{ 3653, 3669, 3670, 3674, 3673, 5775, 3654 };
    CHECK(bosses == want);
    CHECK(bosses.count(3671) == 0);     // Lady Anacondra: a pool boss inside a pack, kill_first 3671
    CHECK(std::find(plan->killFirst.begin(), plan->killFirst.end(), 3671u) != plan->killFirst.end());
    // Live run 5: the Druids of the Fang (3840) are the kill-first units, ahead of Anacondra, and are never in the CC list
    // (a kill_first unit is not crowd-controlled, so a cc_priority entry for it would be dead). Live run 6: the leg-5 camp
    // (leg 5 seq 6, 3840,5048,5755) killed the party twice even with the Druid first, so the camp's own Adder and Viper
    // follow it in the order (TestLeg5CampKillFirstOrder).
    // Live run 8: the Deviate Shambler camps (5761; leg 6 seq 11 and leg 8 seq 2) join the list behind the leg-5 units
    // (TestShamblerCampsKillFirstOrder).
    std::vector<std::uint32_t> const wantKillFirst{ 3840, 3671, 5048, 5755, 5761 };
    CHECK(plan->killFirst == wantKillFirst);
    CHECK(std::find(plan->ccPriority.begin(), plan->ccPriority.end(), 3840u) == plan->ccPriority.end());
}

void TestWalk(ai::DungeonPlan const* plan)
{
    Result const res = Walk(plan, 40000);

    CHECK(res.done);
    CHECK(res.orders.count("move") && res.orders.at("move") > 150);
    CHECK(res.orders.count("pullback") && res.orders.at("pullback") >= 80);
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


// The party facts of the walk harness (the lead and four bots), the same shape Walk builds.
void BuildPartyFacts(Facts& f, World const& w)
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
        m.x = w.x + m.yd; m.y = w.y; m.z = w.z;
        m.tank = i == 0; m.healer = i == 1; m.ranged = (i == 1 || i == 2); m.core = i < 2; m.level = 22;
        m.healerSight = true;                     // the party stands together: the healer sees the tank (the pull's healer gate, run 9)
        m.actedMs = 0xFFFFFFFFu;
        party.push_back(e);
    }
    fx::RollUpParty(f, party);
    fx::LeadObs lo;
    lo.x = w.x; lo.y = w.y; lo.z = w.z;
    lo.name = "Mtank";
    lo.ground.mmapLoaded = true; lo.ground.mapFloorZ = w.z;
    lo.isTank = true; lo.staying = true; lo.iconsRead = true;
    f.pointLiquid = [](float, float, float) { return false; };
    fx::ReadLeadFacts(f, lo);
}

Facts ReadAt(ai::DungeonPlan const* plan, std::size_t step, World& w, Run& at)
{
    at = Start(plan, step);
    w.Prepare(at);
    w.x = plan->steps[step].x;
    w.y = plan->steps[step].y;
    w.z = plan->steps[step].z;
    Facts f;
    BuildPartyFacts(f, w);
    fx::LeadView lead;
    lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = 43; lead.name = "Mtank";
    DungeonRun::MoveProbeCache probe;
    fx::ReadStepFacts(f, at, lead, probe, w);
    return f;
}

// The real unit rows of a plan pull row (positions from the spawns): the facts list them where they stand.
void TestRealUnitRows(ai::DungeonPlan const* plan, std::map<std::pair<int, int>, std::vector<Row>> const& unitsByStep)
{
    int checked = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
    {
        PlanStep const& s = plan->steps[i];
        if (s.kind != PlanStepKind::Pull)
            continue;
        auto it = unitsByStep.find({int(s.leg), int(s.seq)});
        if (it == unitsByStep.end() || it->second.empty())
            continue;
        World w;
        Run at = Start(plan, i);
        Facts f = ReadAt(plan, i, w, at);          // prepares the step and seeds its scripted mobs
        w.mobs.clear();                             // now the plan's own unit rows, at their spawn positions
        for (Row const& u : it->second)
        {
            Mob m;
            m.handle = 7000 + std::uint64_t(w.mobs.size());
            m.entry = u.entry;
            m.x = u.x; m.y = u.y; m.z = u.z;
            w.mobs.push_back(m);
        }
        f = Facts();
        BuildPartyFacts(f, w);
        {
            fx::LeadView lead2;
            lead2.x = w.x; lead2.y = w.y; lead2.z = w.z; lead2.alive = true; lead2.mapId = 43; lead2.name = "Mtank";
            DungeonRun::MoveProbeCache probe2;
            fx::ReadStepFacts(f, at, lead2, probe2, w);
        }
        for (Row const& u : it->second)
        {
            // A member outside the pull's own radius is a Left/patroller read (the pull still finds it by entry and radius); the check's
            // reach is 15 yd, wider than the pull. The rows within the radius are the units the facts must list where they stand.
            float const d3 = std::sqrt((u.x - s.x) * (u.x - s.x) + (u.y - s.y) * (u.y - s.y) + (u.z - s.z) * (u.z - s.z));
            if (d3 > s.radius)
                continue;
            bool found = false;
            for (UnitFact const& have : f.units)
                found = found || (have.entry == u.entry && std::fabs(have.x - u.x) < 0.01f && std::fabs(have.y - u.y) < 0.01f &&
                                  std::fabs(have.z - u.z) < 0.01f);
            for (UnitFact const& have : f.patrollers)
                found = found || (have.entry == u.entry && std::fabs(have.x - u.x) < 0.01f && std::fabs(have.y - u.y) < 0.01f);
            CHECK(found);
        }
        ++checked;
    }
    CHECK(checked >= 10);
}

// A hold unit east of the spot lays the ranged row west of it (the side the event comes from): x and y are read into the hold's UnitFact.
void TestHoldStandsSide(ai::DungeonPlan const* plan)
{
    std::size_t step = 0;
    bool found = false;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && plan->steps[i].entries.size() == 1 && plan->steps[i].entries[0] == 3636)
        {
            step = i;
            found = true;
            break;
        }
    CHECK(found);
    World w;
    Run at;
    Facts f = ReadAt(plan, step, w, at);
    CHECK(f.units.size() == 1);
    CHECK(std::fabs(f.units[0].x - (plan->steps[step].x + 1.0f)) < 0.01f);
    CHECK(std::fabs(f.units[0].y - (plan->steps[step].y + 1.0f)) < 0.01f);
    // A wave unit east of the spot: the ranged row stands west of it.
    w.mobs[0].x = plan->steps[step].x + 12.0f;
    w.mobs[0].y = plan->steps[step].y;
    Facts g = ReadAt(plan, step, w, at);
    std::vector<HoldStand> stands = HoldStands(at, g, plan->steps[step]);
    CHECK(!stands.empty());
    for (HoldStand const& st : stands)
    {
        CHECK(st.x < plan->steps[step].x);
        CHECK(std::fabs(st.x - plan->steps[step].x) <= kHoldGatherYd);
    }
}

// A dead or gone Disciple in an escort hold stops the event with its name (no wait for the hold's timeout), and the run goes back to the gossip once.
void TestEscortNpcGone(ai::DungeonPlan const* plan)
{
    std::size_t step = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && (plan->steps[i].flags & ai::kPlanFlagEscortNpc))
        {
            step = i;
            break;
        }
    CHECK(step != 0);
    World w;
    w.mobs.clear();                 // the Disciple (3678) is not there: the start NPC is gone
    Run at;
    Facts f = ReadAt(plan, step, w, at);
    CHECK(f.eventNpcRead && !f.eventNpcAlive);
    for (int t = 0; t < 13 && at.state == State::Running; ++t)
    {
        Tick(at, f, 1000);
        Facts next;
        BuildPartyFacts(next, w);
        fx::LeadView lead2;
        lead2.x = w.x; lead2.y = w.y; lead2.z = w.z; lead2.alive = true; lead2.mapId = 43; lead2.name = "Mtank";
        DungeonRun::MoveProbeCache probe2;
        fx::ReadStepFacts(next, at, lead2, probe2, w);
        f = next;
    }
    CHECK(at.state == State::Paused);
    CHECK(at.note.find("Disciple of Naralex") != std::string::npos);
    // Back to the gossip that starts the event (the one re-arm): a respawned Disciple can be talked to again.
    std::size_t const back = at.step;
    CHECK(plan->steps[back].kind == PlanStepKind::Gossip);

    // A gossip that did not start the escort: the Disciple is alive, still at his stand, and no wave came. The first escort hold says so
    // after kEscortStartMs and goes back to the gossip.
    World w2;
    Run at2;
    Facts f2 = ReadAt(plan, step, w2, at2);
    w2.mobs.clear();
    Mob still;
    still.handle = 7300;
    still.entry = 3678;
    still.x = -134.965f; still.y = 125.402f; still.z = -78.094f;
    w2.mobs.push_back(still);
    Facts r2 = Facts();
    BuildPartyFacts(r2, w2);
    {
        fx::LeadView l2;
        l2.x = w2.x; l2.y = w2.y; l2.z = w2.z; l2.alive = true; l2.mapId = 43; l2.name = "Mtank";
        DungeonRun::MoveProbeCache pr2;
        fx::ReadStepFacts(r2, at2, l2, pr2, w2);
    }
    CHECK(r2.eventNpcRead && r2.eventNpcAlive);
    for (int t = 0; t < 32 && at2.state == State::Running; ++t)
    {
        Tick(at2, r2, 1000);
        Facts n2 = Facts();
        BuildPartyFacts(n2, w2);
        fx::LeadView l3;
        l3.x = w2.x; l3.y = w2.y; l3.z = w2.z; l3.alive = true; l3.mapId = 43; l3.name = "Mtank";
        DungeonRun::MoveProbeCache pr3;
        fx::ReadStepFacts(n2, at2, l3, pr3, w2);
        r2 = n2;
    }
    CHECK(at2.state == State::Paused);
    CHECK(at2.note.find("did not leave his stand") != std::string::npos);
    CHECK(plan->steps[at2.step].kind == PlanStepKind::Gossip);

    // A later escort hold (the Viper wave at stop 15) finds the same gossip across the holds between it and the gossip: a dead Disciple in
    // its read range stops the event there (the back-search is EventGossipStartStep, not the hold-stopping EventStartStep).
    std::size_t vipers = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && plan->steps[i].entries.size() == 1 && plan->steps[i].entries[0] == 5755)
        {
            vipers = i;
            break;
        }
    CHECK(vipers != 0);
    World w3;
    Run at3;
    ReadAt(plan, vipers, w3, at3);
    w3.mobs.clear();
    Mob dead;
    dead.handle = 7400;
    dead.entry = 3678;
    dead.alive = false;
    dead.x = -60.0f; dead.y = 250.0f; dead.z = -90.0f;
    w3.mobs.push_back(dead);
    Facts r3 = Facts();
    BuildPartyFacts(r3, w3);
    {
        fx::LeadView l4;
        l4.x = w3.x; l4.y = w3.y; l4.z = w3.z; l4.alive = true; l4.mapId = 43; l4.name = "Mtank";
        DungeonRun::MoveProbeCache pr4;
        fx::ReadStepFacts(r3, at3, l4, pr4, w3);
    }
    CHECK(r3.eventNpcRead && !r3.eventNpcAlive);
    for (int t = 0; t < 12 && at3.state == State::Running; ++t)
        Tick(at3, r3, 1000);
    CHECK(at3.state == State::Paused);
    CHECK(at3.note.find("dead or gone") != std::string::npos);
    CHECK(plan->steps[at3.step].kind == PlanStepKind::Gossip);
}

// N1: a Disciple who dies mid-fight. The summons vanish on the tick he dies, so the hold must not take "every unit of it is dead" for
// the event's success: its own 10 s gone timer names him, and the last hold goes back to the gossip instead of walking on into Mutanus.
void TestDiscipleDiesMidFight(ai::DungeonPlan const* plan)
{
    std::vector<std::size_t> holds;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && (plan->steps[i].flags & ai::kPlanFlagEscortNpc))
            holds.push_back(i);
    CHECK(holds.size() == 4);
    if (holds.size() != 4)
        return;

    auto readNow = [](World& w, Run& at) {
        Facts f;
        BuildPartyFacts(f, w);
        fx::LeadView lead;
        lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = 43; lead.name = "Mtank";
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, at, lead, probe, w);
        return f;
    };
    auto arm = [](World& w, ai::DungeonPlan const* plan, std::size_t hold, bool waveAlive, bool discipleAlive) {
        w.mobs.clear();
        Mob disciple;
        disciple.entry = 3678;
        disciple.x = -134.965f; disciple.y = 125.402f; disciple.z = -78.094f;
        disciple.alive = discipleAlive;
        w.mobs.push_back(disciple);
        if (!plan->steps[hold].entries.empty())
        {
            Mob wave;
            wave.entry = plan->steps[hold].entries.front();
            wave.x = w.x + 1.0f; wave.y = w.y + 1.0f; wave.z = w.z;
            wave.alive = waveAlive;
            w.mobs.push_back(wave);
        }
    };

    // Holds 1-3: the wave dies while the Disciple lives. The hold ends on that tick, as it always did.
    for (std::size_t h = 0; h + 1 < holds.size(); ++h)
    {
        World w;
        Run at;
        Facts f = ReadAt(plan, holds[h], w, at);
        arm(w, plan, holds[h], true, true);
        Tick(at, readNow(w, at), 1000);
        arm(w, plan, holds[h], false, true);
        Tick(at, readNow(w, at), 1000);
        CHECK(at.step == holds[h] + 1);
        CHECK(at.state == State::Running);
        CHECK(at.eventNpcGoneMs == 0);
    }

    // The Disciple dies on the tick his wave dies: the hold keeps holding (no "event done"), and after kEscortNpcGoneMs it names him
    // and goes back to the gossip.
    {
        World w;
        Run at;
        Facts f = ReadAt(plan, holds[0], w, at);
        arm(w, plan, holds[0], true, true);
        Tick(at, readNow(w, at), 1000);
        CHECK(at.step == holds[0]);
        arm(w, plan, holds[0], false, false);
        Tick(at, readNow(w, at), 1000);
        CHECK(at.step == holds[0]);             // the old rule advanced here ("every unit of it is dead")
        CHECK(at.state == State::Running);
        for (int t = 0; t < 12 && at.step == holds[0]; ++t)
            Tick(at, readNow(w, at), 1000);
        CHECK(plan->steps[at.step].kind == PlanStepKind::Gossip);
        for (int t = 0; t < 20 && at.state == State::Running; ++t)
            Tick(at, readNow(w, at), 1000);
        CHECK(at.state == State::Paused);
        CHECK(at.note.find("Disciple of Naralex") != std::string::npos);
    }

    // The last hold (the Ectoplasm wave before Mutanus): the same death must go back to the gossip, never on to the Mutanus boss step
    // that would wait its 300 s and pause "not found".
    {
        World w;
        Run at;
        Facts f = ReadAt(plan, holds.back(), w, at);
        arm(w, plan, holds.back(), true, true);
        Tick(at, readNow(w, at), 1000);
        arm(w, plan, holds.back(), false, false);
        Tick(at, readNow(w, at), 1000);
        CHECK(at.step == holds.back());
        for (int t = 0; t < 12 && at.step == holds.back(); ++t)
            Tick(at, readNow(w, at), 1000);
        CHECK(at.step != holds.back() + 1);     // not the Mutanus boss step
        CHECK(plan->steps[at.step].kind == PlanStepKind::Gossip);
        for (int t = 0; t < 20 && at.state == State::Running; ++t)
            Tick(at, readNow(w, at), 1000);
        CHECK(at.state == State::Paused);
        CHECK(at.note.find("Disciple of Naralex") != std::string::npos);
        CHECK(at.note.find("not found") == std::string::npos);
    }
}

// An old corpse of the entry (a Ravager pack killed on the way in) does not end a hold early; a corpse this hold saw die does.
void TestOldCorpseHold(ai::DungeonPlan const* plan)
{
    std::size_t step = 0;
    for (std::size_t i = 0; i < plan->steps.size(); ++i)
        if (plan->steps[i].kind == PlanStepKind::Hold && plan->steps[i].entries.size() == 1 && plan->steps[i].entries[0] == 3636)
        {
            step = i;
            break;
        }
    World w;
    Run at;
    Facts f = ReadAt(plan, step, w, at);
    w.mobs.clear();                 // the Disciple is alive for this hold (only the escort-NPC read cares), no wave yet
    Mob disciple;
    disciple.handle = 7200;
    disciple.entry = 3678;
    disciple.x = -134.965f; disciple.y = 125.402f; disciple.z = -78.094f;
    w.mobs.push_back(disciple);
    // One old corpse in the radius, no living unit: the first read's corpse is the baseline.
    f.eventNpcRead = true;
    f.eventNpcAlive = true;         // this test is the corpse rule; the Disciple's own read is TestEscortNpcGone
    f.eventDown = 1;
    f.units.clear();
    for (int t = 0; t < 12 && at.state == State::Running; ++t)
    {
        Tick(at, f, 1000);
        f.eventDown = 1;
        f.units.clear();
    }
    CHECK(at.step == step);
    CHECK(at.state == State::Running);
    // A corpse of this hold (a second one): the "all dead before the hold" rule fires after kHoldAllDeadMs.
    Facts g = f;
    g.eventDown = 2;
    g.units.clear();
    for (int t = 0; t < 12 && at.step == step; ++t)
        Tick(at, g, 1000);
    CHECK(at.step != step || at.state != State::Running);
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

// Live run 6 (notes/live-wc-run6-2026-11-22.md): the leg-5 camp of the Druid of the Fang (leg 5 seq 6, index 54:
// 3840, 5048, 5755) killed the party twice (10 deaths) although the Druid is killed first: the world log's fight
// engaged exactly the row's three units (Druid of the Fang, Deviate Adder, Deviate Viper), and after the Druid fell
// each bot fought its own pick while the two poisoners finished the party. The camp's own units carry the pack's
// kill-first order now, the treatment the leg-7 Druid camps got (live run 5): the Druid, then the Adder, then the Viper.
void TestLeg5CampKillFirstOrder(ai::DungeonPlan const* plan)
{
    std::size_t const step = 54;
    CHECK(plan->steps[step].kind == PlanStepKind::Pull);
    CHECK(plan->steps[step].entries == (std::vector<std::uint32_t>{ 3840, 5048, 5755 }));
    int const druid = plan->KillFirstRank(3840);
    int const adder = plan->KillFirstRank(5048);
    int const viper = plan->KillFirstRank(5755);
    CHECK(druid >= 0);
    CHECK(adder > druid);
    CHECK(viper > adder);
    // The focus the run sends: the Druid while all three are engaged, the Adder once it falls, then the Viper.
    Run r = Start(plan, step);
    Facts f;
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    auto engaged = [](std::uint64_t guid, std::uint32_t entry, float yd) {
        UnitFact u;
        u.guid = guid;
        u.entry = entry;
        u.distLead = yd;
        u.hostile = true;
        u.attackable = true;
        u.engaged = true;
        return u;
    };
    f.killFirst = {engaged(1, 3840, 6.0f), engaged(2, 5048, 8.0f), engaged(3, 5755, 9.0f)};
    CHECK(PickKillFirst(r, f) && PickKillFirst(r, f)->guid == 1);
    f.killFirst.erase(f.killFirst.begin());
    CHECK(PickKillFirst(r, f) && PickKillFirst(r, f)->guid == 2);
    f.killFirst.erase(f.killFirst.begin());
    CHECK(PickKillFirst(r, f) && PickKillFirst(r, f)->guid == 3);
}

// Live run 6: the wipe at leg 8's boss (step 87, Pythas) released the party to the entrance and lost the route
// checkpoint; the straight walk back failed and the replay began at the plan's first move point (the live note's
// "walk back: to the point of step 4"), so the run walked the whole route back before pulling again. With no
// checkpoint the replay resumes at the last cleared step's move point (leg 8 seq 1, index 81).
void TestWipeResumeAtTheLastClearedStep(ai::DungeonPlan const* plan)
{
    std::size_t const boss = 87;
    CHECK(plan->steps[boss].kind == PlanStepKind::Boss && plan->steps[boss].entry == 3670);
    Run r = Start(plan, boss);
    CHECK(!r.routeKnown);                 // a wipe: the lead bot died, where it stands on the route is not known
    // The release spot: the plan's own step 4 point (the live note named step 4 as the point the replay walked to).
    World w;
    w.x = plan->steps[3].x;
    w.y = plan->steps[3].y;
    w.z = plan->steps[3].z;
    Facts f;
    BuildPartyFacts(f, w);
    f.seen = false;
    CHECK(ReplayWalkBack(r, f));
    CHECK(r.walkBack && r.walkBackIdx == 81);
    CHECK(LastClearedStep(r) == 86);      // the last camp cleared before the boss
    CHECK(ReplayFloor(r) == 81);          // leg 8's first move point, the last move point before the boss
    CHECK(plan->steps[81].kind == PlanStepKind::Move);
}


// Live run 8 (notes/live-wc-run8-2026-11-22.md): the Deviate Shambler camps (5761) are the deadliest after the Druid camps: leg 6
// seq 11 (index 69: 5761, 3840, 3640; 11 deaths and a wipe) and leg 8 seq 2 (index 82: 5761, 3840; 34 deaths, two wipes, three
// pauses). Their units stand 1.3-5.9 yd apart (Druid 1.3, Shambler 4.6, Snake 5.9 yd from the row's point at step 82), so the pull
// cannot be split; the plan's kill order is the lever: the Druid first, the Shambler after the camp-leg units, never "no rank".
void TestShamblerCampsKillFirstOrder(ai::DungeonPlan const* plan)
{
    std::size_t const camps[] = { 69, 82 };
    for (std::size_t step : camps)
    {
        CHECK(plan->steps[step].kind == PlanStepKind::Pull);
        CHECK(std::find(plan->steps[step].entries.begin(), plan->steps[step].entries.end(), 5761u) != plan->steps[step].entries.end());
        CHECK(std::find(plan->steps[step].entries.begin(), plan->steps[step].entries.end(), 3840u) != plan->steps[step].entries.end());
    }
    CHECK(plan->steps[69].entries == (std::vector<std::uint32_t>{ 5761, 3840, 3640 }));
    CHECK(plan->steps[82].entries == (std::vector<std::uint32_t>{ 5761, 3840 }));
    int const druid = plan->KillFirstRank(3840);
    int const viper = plan->KillFirstRank(5755);
    int const shambler = plan->KillFirstRank(5761);
    CHECK(druid == 0);
    CHECK(shambler > viper);          // behind the r7 list, which stays a prefix
    // Every row of the plan that lists the Shambler carries it in the order (kill_first is plan-wide).
    std::size_t rowsWithShambler = 0;
    for (PlanStep const& st : plan->steps)
        if (st.kind == PlanStepKind::Pull &&
            std::find(st.entries.begin(), st.entries.end(), 5761u) != st.entries.end())
            ++rowsWithShambler;
    CHECK(rowsWithShambler == 10);   // plus the Pythas boss row, which lists it too
    // The pull of step 82: the Druid is the target even with the Shambler nearer; the focus then goes Druid, Shambler.
    Run r = Start(plan, 82);
    Facts f;
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    f.seen = true;
    auto unit = [](std::uint64_t guid, std::uint32_t entry, float yd) {
        UnitFact u;
        u.guid = guid;
        u.entry = entry;
        u.distLead = yd;
        u.hostile = true;
        u.attackable = true;
        u.engaged = true;
        return u;
    };
    f.units = { unit(2, 5761, 5.0f), unit(1, 3840, 9.0f) };
    CHECK(PickPackPull(r, f) && PickPackPull(r, f)->guid == 1);
    f.killFirst = { unit(2, 5761, 5.0f), unit(1, 3840, 9.0f), unit(3, 2914, 4.0f) };   // the Snake (2914) has no rank
    CHECK(PickKillFirst(r, f) && PickKillFirst(r, f)->guid == 1);
    f.killFirst.erase(f.killFirst.begin() + 1);
    CHECK(PickKillFirst(r, f) && PickKillFirst(r, f)->guid == 2);
}

// Live run 8, the tank died first (24 of 77 deaths): the host asks for the pull's healer gate and a raised walk-back pull cap
// (ApplyConfig), and reads the healer's sight line each tick (TbRunFacts.cpp: m.healerSight).
void TestHostAsksForTheRun9Gates()
{
    Facts f;
    fx::Config cfg;
    fx::ApplyConfig(f, cfg);
    CHECK(f.healerPullGate);
    CHECK(f.walkBackPullCap == fx::kWalkBackPullCap && fx::kWalkBackPullCap > kMaxWalkBackPulls);
}

// The real plan, the real pull row of step 82: with the healer out of the tank's sight the pull is held, with it in sight the
// pull goes out.
void TestHealerGateAtTheShamblerCamp(ai::DungeonPlan const* plan)
{
    World w;
    Run r;
    Facts f = ReadAt(plan, 82, w, r);
    CHECK(f.healerPullGate);
    f.seen = true;
    UnitFact shambler;
    shambler.guid = 77;
    shambler.entry = 5761;
    shambler.distLead = 20.0f;
    shambler.hostile = true;
    shambler.attackable = true;
    f.units = { shambler };
    for (MemberFact& m : f.members)
        if (m.healer)
            m.healerSight = false;      // the priest stands round a corner of the cave
    std::string const held = Tick(r, f);
    CHECK(held.rfind("pullback", 0) != 0);
    CHECK(r.note.find("pull held: the healer Mpriest has no line of sight to the tank") != std::string::npos);
    for (MemberFact& m : f.members)
        if (m.healer)
            m.healerSight = true;
    CHECK(Tick(r, f) == "pullback 77");
}

// Live run 8, step 82: after the wipe the party walked from the entrance to step 81 through packs that had respawned (7200 s after
// they were cleared). The walk back lists every hostile within kWalkBackSeeYd of the lead bot, so each respawned pack on the way is
// pulled one at a time; a forward move lists only those within kHopPullYd of its point, as before; the current step's own pack
// stays the step's.
void TestWalkBackPullsRespawnedPacks(ai::DungeonPlan const* plan)
{
    auto hop = [](std::uint64_t handle, std::uint32_t entry, float x, float y, float z, float lx, float ly, float lz) {
        fx::HopObs h;
        h.handle = handle;
        h.entry = entry;
        h.x = x; h.y = y; h.z = z;
        h.distLead = fx::Dist3(x, y, z, lx, ly, lz);
        h.los = true;
        h.pathRead = true;
        h.endX = x; h.endY = y; h.endZ = z;
        h.straightYd = h.pathYd = h.distLead;
        return h;
    };
    // The lead bot stands on the leg 0 route (-72.2 214.8 -93.6 is leg 0 seq 5's pull point), the walk back's point is far ahead.
    float const lx = -72.2f, ly = 214.8f, lz = -93.6f;
    PlanStep const& ahead = plan->steps[81];                      // the point the walk back goes to
    Run walk = Start(plan, 82);
    walk.walkBack = true;
    std::vector<fx::HopObs> const around = {
        hop(1, 3636, lx + 18.0f, ly, lz, lx, ly, lz),            // a respawned Ravager pack 18 yd from the lead bot
        hop(2, 3636, lx + 45.0f, ly, lz, lx, ly, lz),            // one 45 yd off: not yet
        hop(3, 5761, -173.0f, -90.0f, -67.6f, lx, ly, lz),       // the camp of step 82 itself: its step's
    };
    Facts back;
    fx::ReadHopUnits(back, walk, ahead.x, ahead.y, ahead.z, around, lx, ly, lz);
    CHECK(back.hopUnits.size() == 1 && back.hopUnits[0].guid == 1);
    // A forward move (no walk back): only what stands within kHopPullYd of the point, none of these.
    Run forward = Start(plan, 81);
    Facts fwd;
    fx::ReadHopUnits(fwd, forward, ahead.x, ahead.y, ahead.z, around, lx, ly, lz);
    CHECK(fwd.hopUnits.empty());
    // A unit in combat is never a hop unit, in a walk back too.
    std::vector<fx::HopObs> busy = { hop(4, 3636, lx + 10.0f, ly, lz, lx, ly, lz) };
    busy[0].inCombat = true;
    Facts fought;
    fx::ReadHopUnits(fought, walk, ahead.x, ahead.y, ahead.z, busy, lx, ly, lz);
    CHECK(fought.hopUnits.empty());
}

// The same through ReadStepFacts: the glue is asked for hostiles out to kWalkBackSeeYd of the lead bot while the run walks back (the hop
// range is otherwise the distance to the point plus kHopPullYd, 20 yd here), and lists the one that stands 25 yd off only then.
struct HopWorld : World
{
    float asked = 0;
    std::vector<fx::HopObs> give;
    void HopCandidates(float range, float, float, float, std::vector<fx::HopObs>& out) override
    {
        asked = range;
        out = give;
    }
};

void TestWalkBackAsksForHostilesAroundTheLead(ai::DungeonPlan const* plan)
{
    PlanStep const& point = plan->steps[81];
    HopWorld w;
    w.x = point.x - 5.0f; w.y = point.y; w.z = point.z;       // 5 yd short of the walk-back point
    fx::HopObs h;
    h.handle = 9;
    h.entry = 3636;
    h.x = w.x - 25.0f; h.y = w.y; h.z = w.z;                   // a respawned Ravager 25 yd behind the lead bot
    h.distLead = 25.0f;
    h.los = true;
    h.pathRead = true;
    h.endX = h.x; h.endY = h.y; h.endZ = h.z;
    h.straightYd = h.pathYd = 25.0f;
    w.give = { h };
    for (bool walkBack : { true, false })
    {
        Run r = Start(plan, walkBack ? 82 : 81);
        r.walkBack = walkBack;
        r.walkBackIdx = 81;
        Facts f;
        fx::Config cfg;
        fx::ApplyConfig(f, cfg);
        fx::LeadView lead;
        lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = 43; lead.name = "Mtank";
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, r, lead, probe, w);
        CHECK(f.seen);
        CHECK(std::fabs(w.asked - (walkBack ? fx::kWalkBackSeeYd : 5.0f + kHopPullYd)) < 0.01f);
        CHECK(f.hopUnits.size() == (walkBack ? 1u : 0u));
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
    std::map<std::pair<int, int>, std::vector<Row>> unitsByStep;
    int unitRows = 0;
    if (!LoadRows(argv[1], *table, rows, unitRows, unitsByStep))
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
    TestLeg5CampKillFirstOrder(plan);
    TestWipeResumeAtTheLastClearedStep(plan);
    TestShamblerCampsKillFirstOrder(plan);
    TestHostAsksForTheRun9Gates();
    TestHealerGateAtTheShamblerCamp(plan);
    TestWalkBackPullsRespawnedPacks(plan);
    TestWalkBackAsksForHostilesAroundTheLead(plan);
    TestWalk(plan);
    TestUnitsHoldFacts(plan);
    TestDataHoldFacts(plan);
    TestRealUnitRows(plan, unitsByStep);
    TestHoldStandsSide(plan);
    TestEscortNpcGone(plan);
    TestDiscipleDiesMidFight(plan);
    TestOldCorpseHold(plan);
    std::printf("test_run_wc: %d checks, %d failed (plan: %zu steps, %zu legs, %d unit rows)\n", g_checks, g_fail,
        plan->steps.size(), plan->legs.size(), unitRows);
    return g_fail ? 1 : 0;
}
