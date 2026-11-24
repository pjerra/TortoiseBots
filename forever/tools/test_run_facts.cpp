// Standalone check of package F6 (the run leader's facts glue, forever/src/host/run/TbRunFactsPure.h): no game types, no server.
//   cd <worktree> && python3 forever/tools/facts_rfc_rows.py ragefire_chasm /tmp/rfc_rows.tsv
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_facts.cpp -o /tmp/test_run_facts
//   /tmp/test_run_facts /tmp/rfc_rows.tsv forever/docs/RUN-S6-F-FIELDS.tsv [--table]
//
// It runs the REAL Turtle decision header (DungeonRun::Tick) over the exported Ragefire Chasm plan (9 legs, 84 steps) with a SCRIPTED world that implements
// the StepWorld interface and fills the plain observation structs the way the ForeverCore reader does; every `Facts` of every tick is built by the
// glue's pure half (ApplyConfig, RollUpParty, ReadLeadFacts, ReadFightFacts, ReadStepFacts). It then checks:
//   * each scenario (happy, rest, death, wipe, pack, hop) walks the plan to its end (state Done, step 84 of 84) or, for wipe, through the revive;
//   * every field the coverage table (forever/docs/RUN-S6-F-FIELDS.tsv) calls skipped, never or notknown is still its `Facts()` default after every tick of
//     every scenario (an unfilled field stays the safe default: the proof of "the 86 stay unfilled");
//   * the fields the table calls filled / config / forced and that the scenarios changed from the default are listed (the rest are core-only reads the
//     scripted world cannot exercise, or equal their default in these scenarios).
// It prints the per-tick cost of the glue's own work (microseconds, no Tick) and exits non-zero on any failure.
#include "forever/src/host/run/TbRunFactsPure.h"
#include "runtime/DecisionCaptureFields.gen.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <type_traits>

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;
namespace fx = TortoiseBots::run::facts;

namespace
{
// ---- field comparison against Facts() (the X-macro list of the decision capture, declaration order) ----
template <class T, class = void>
struct HasSize : std::false_type {};
template <class T>
struct HasSize<T, std::void_t<decltype(std::declval<T const&>().size())>> : std::true_type {};

template <class T>
bool Differs(T const& a, T const& b)
{
    if constexpr (std::is_same_v<T, std::string>)
        return a != b;
    else if constexpr (HasSize<T>::value)
        return a.size() != b.size();
    else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
        return !(a == b);
    else if constexpr (std::is_array_v<T>)
        return std::memcmp(&a, &b, sizeof(T)) != 0;
    else
        return false;
}
bool Differs(UnitFact const& a, UnitFact const& b) { return a.guid != b.guid || a.entry != b.entry; }
bool Differs(PullReadIn const& a, PullReadIn const& b) { return a.fromPoint != b.fromPoint || a.hasSpawn != b.hasSpawn; }
bool Differs(RaidFacts const& a, RaidFacts const& b) { return a.on != b.on; }

std::set<std::string>& Changed()
{
    static std::set<std::string> changed;
    return changed;
}

void NoteChanged(Facts const& f)
{
    static Facts const defaults;
#define TB_NOTE(name) if (Differs(f.name, defaults.name)) Changed().insert(#name);
    TB_DCAP_FIELDS_Facts(TB_NOTE)
#undef TB_NOTE
    if (f.pointLiquid)
        Changed().insert("pointLiquid");
}

// ---- the plan, from the exported rows (forever/tools/facts_rfc_rows.py) ----
std::vector<std::string> Split(std::string const& s, char c)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s)
        if (ch == c) { out.push_back(cur); cur.clear(); }
        else cur += ch;
    out.push_back(cur);
    return out;
}

std::shared_ptr<ai::DungeonPlanTable> LoadPlan(char const* path, std::string& name)
{
    auto table = std::make_shared<ai::DungeonPlanTable>();
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        auto c = Split(line, '\t');
        if (c.empty()) continue;
        if (c[0] == "plan")
        {
            name = c[1];
            table->AddPlan(c[1], std::uint32_t(std::stoul(c[2])));
            std::string err;
            table->SetUnits(c[1], c[3], c[4], c[5], c[6], err);
        }
        else if (c[0] == "leg")
            table->AddLeg(c[1], std::uint8_t(std::stoul(c[2])), c[3]);
        else if (c[0] == "step")
        {
            PlanStep step;
            step.leg = std::uint8_t(std::stoul(c[2]));
            step.seq = std::uint16_t(std::stoul(c[3]));
            ai::ParsePlanStepRowKind(c[4], step.kind, step.exactLaunch, step.openProof);
            step.entry = std::uint32_t(std::stoul(c[5]));
            step.x = std::stof(c[6]); step.y = std::stof(c[7]); step.z = std::stof(c[8]);
            step.radius = std::stof(c[9]);
            step.item = std::uint32_t(std::stoul(c[10]));
            step.param = std::uint32_t(std::stoul(c[11]));
            step.flags = std::uint32_t(std::stoul(c[12]));
            step.timeoutS = std::uint16_t(std::stoul(c[13]));
            step.label = c[15];
            auto refusal = ai::ParsePlanEntries(c[14], step.entries) ? table->AddStep(c[1], step) : ai::PlanStepRefusal::BadEntries;
            if (refusal != ai::PlanStepRefusal::None)
                std::fprintf(stderr, "step refused: %s leg %s seq %s\n", c[1].c_str(), c[2].c_str(), c[3].c_str());
        }
        else if (c[0] == "boss")
            table->AddBoss(std::uint32_t(std::stoul(c[1])), std::uint32_t(std::stoul(c[2])));
    }
    table->Seal();
    return table;
}

float Dist2(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

// ---- the scripted world ----
struct Mob
{
    std::uint64_t handle = 0;
    std::uint32_t entry = 0;
    float x = 0, y = 0, z = 0;
    bool alive = true;
    bool ownWaypoint = false;
    bool boss = false;
};

class ScriptWorld : public fx::StepWorld
{
public:
    std::string scn = "happy";
    float x = 0, y = 0, z = 0;                  // the lead
    float tx = 0, ty = 0, tz = 0;
    bool walking = false;
    int fightTicks = 0;
    int mana = 100, health = 100;
    int pullsSeen = 0;
    int dead = 0;                               // dead dps bots
    bool wiped = false, wipedOnce = false;
    std::vector<Mob> mobs;
    std::size_t mobsStep = std::size_t(-1);
    std::uint64_t nextHandle = 1000;
    std::set<std::string> notKnown;
    std::uint32_t pathQueries = 0;
    PlanStep const* step = nullptr;             // the step the mobs belong to
    std::size_t stepIndex = 0;
    bool bossDead = false;

    void Prepare(Run const& r)
    {
        step = Current(r);
        stepIndex = r.step;
        if (mobsStep == r.step)
            return;
        mobsStep = r.step;
        mobs.clear();
        bossDead = false;
        if (!step || (step->kind != ai::PlanStepKind::Pull && step->kind != ai::PlanStepKind::Boss))
            return;
        std::vector<std::uint32_t> entries = step->entries;
        if (step->kind == ai::PlanStepKind::Boss && step->entry)
            entries = { step->entry };
        int n = 0;
        for (std::uint32_t e : entries)
        {
            Mob m;
            m.handle = nextHandle++;
            m.entry = e;
            m.x = step->x + float(n) * 2.0f; m.y = step->y; m.z = step->z;
            m.boss = step->kind == ai::PlanStepKind::Boss;
            mobs.push_back(m);
            ++n;
        }
        if (scn == "pack" && step->kind == ai::PlanStepKind::Pull)
        {
            // a patroller 25 yd out, a unit of the pack 12 yd out (residue band), one 40 yd out of the same entries (left), a flier above
            for (int k = 0; k < 3; ++k)
            {
                Mob m;
                m.handle = nextHandle++;
                m.entry = entries.front();
                m.x = step->x + (k == 0 ? 25.0f : k == 1 ? step->radius + 5.0f : step->radius + 45.0f); m.y = step->y; m.z = step->z;
                m.ownWaypoint = k == 0;
                mobs.push_back(m);
            }
        }
    }

    // ---- StepWorld ----
    fx::PathObs PathFromLead(float px, float py, float pz) override
    {
        ++pathQueries;
        fx::PathObs p;
        p.valid = true;
        p.points = { PathPoint{ x, y, z }, PathPoint{ (x + px) / 2, (y + py) / 2, (z + pz) / 2 }, PathPoint{ px, py, pz } };
        p.endX = px; p.endY = py; p.endZ = pz;
        p.lengthYd = std::sqrt((px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z));
        return p;
    }
    bool LeadSees(float px, float py, float) override { return Dist2(x, y, px, py) < 60.0f; }
    std::vector<fx::CreatureObs> CreaturesOfEntry(std::uint32_t entry, float range) override
    {
        std::vector<fx::CreatureObs> out;
        for (Mob const& m : mobs)
        {
            if (m.entry != entry || Dist2(x, y, m.x, m.y) > range)
                continue;
            fx::CreatureObs c;
            c.handle = m.handle; c.entry = m.entry; c.alive = m.alive;
            c.x = m.x; c.y = m.y; c.z = m.z;
            c.distLead = std::sqrt((x - m.x) * (x - m.x) + (y - m.y) * (y - m.y));
            c.hasSpawn = !m.ownWaypoint; c.spawnX = m.x; c.spawnY = m.y; c.spawnZ = m.z;
            c.ownWaypoint = m.ownWaypoint;
            c.hostile = true; c.attackable = true; c.normalRank = !m.boss; c.level = 17; c.healthPct = 100.0f;
            out.push_back(c);
        }
        return out;
    }
    bool OwnBoss(ai::PlanStep const& s, fx::OwnBossObs& out) override
    {
        if (mobs.empty() || !mobs[0].boss)
            return false;
        out.known = true;
        out.loaded = true;
        out.alive = mobs[0].alive;
        out.entry = mobs[0].entry;
        out.handle = mobs[0].handle;
        out.x = mobs[0].x; out.y = mobs[0].y; out.z = mobs[0].z;
        out.rowX = s.x; out.rowY = s.y;
        return true;
    }
    bool BossSpawns(ai::PlanStep const&, std::vector<fx::BossSpawnObs>& out) override
    {
        for (Mob const& m : mobs)
            if (m.boss)
            {
                fx::BossSpawnObs o;
                o.handle = m.handle; o.isOwn = true; o.loaded = true; o.alive = m.alive; o.entry = m.entry; o.x = m.x; o.y = m.y; o.z = m.z;
                out.push_back(o);
            }
        return !out.empty();
    }
    void HopCandidates(float range, float cx, float cy, float, std::vector<fx::HopObs>& out) override
    {
        if (scn != "hop")
            return;
        // a hostile 6 yd from the hop point, in sight, path read (complete): the policy must list it as a hop unit
        fx::HopObs h;
        h.handle = 7777; h.entry = 11520; h.x = cx + 6; h.y = cy; h.z = z;
        h.distLead = Dist2(x, y, h.x, h.y);
        if (h.distLead > range)
            return;
        h.los = true; h.pathRead = true; h.attackable = true;
        h.endX = h.x; h.endY = h.y; h.endZ = h.z; h.pathYd = h.distLead; h.straightYd = h.distLead;
        out.push_back(h);
    }
    bool GroundUnder(fx::CreatureObs const&, float, float&) override { return false; }
    void HostilesNear(float, std::vector<fx::ForwardObs>& out) override
    {
        for (Mob const& m : mobs)
            if (m.alive)
                out.push_back(fx::ForwardObs{ m.entry, m.x, m.y, m.z, 20.0f });
    }
    void NotKnown(char const* field) override { notKnown.insert(field); }
};

// One tick's Facts, the way TbRunFacts.cpp builds them: the pure half only (the world reads are the scripted world's).
void BuildFacts(Facts& f, ScriptWorld& w, Run const& r)
{
    fx::Config cfg;
    cfg.fightForward = true;
    cfg.lightRest = true;
    cfg.packClearMarginYd = 10.0f;
    cfg.packClearRepulls = 3;
    cfg.nearWipeStop = 2;
    cfg.nearWipeStopMinutes = 10;
    cfg.linkedDeath = true;                 // a conf value the glue copies
    cfg.nearWipeStopParty = true;
    cfg.walkBackAfterWonFight = true;
    cfg.retreatAt2Dead = true;
    fx::ApplyConfig(f, cfg);

    char const* names[5] = { "Mtank", "Mpriest", "Mdpsa", "Mdpsb", "Mdpsc" };
    std::vector<fx::PartyEntry> party;
    for (int i = 0; i < 5; ++i)
    {
        fx::PartyEntry e;
        e.name = names[i];
        e.isLead = i == 0;
        e.present = true;
        e.alive = !(w.wiped || (w.scn == "death" && w.dead > 0 && i == 3));
        if (w.scn == "wipe-absent" && i == 4)
            e.present = false;
        e.inCombat = w.fightTicks > 0;
        e.eating = r.phase == Phase::Rest && i == 1;
        e.healer = i == 1;
        e.dps = i >= 2;
        e.canResurrect = i == 1;
        e.usesMana = i == 1 || i == 0;
        e.healthPct = std::uint32_t(w.health);
        e.manaPct = std::uint32_t(w.mana);
        MemberFact& m = e.fact;
        m.name = e.name;
        m.yd = i == 0 ? 0.0f : 4.0f + float(i);
        m.x = w.x + m.yd; m.y = w.y; m.z = w.z;
        m.inCombat = e.inCombat;
        m.tank = i == 0; m.healer = i == 1; m.core = i < 2; m.level = 18; m.healerSight = true;
        m.actedMs = e.inCombat ? 0u : 0xFFFFFFFFu;
        party.push_back(e);
    }
    fx::RollUpParty(f, party);

    fx::LeadObs lo;
    lo.alive = !w.wiped;
    lo.x = w.x; lo.y = w.y; lo.z = w.z;
    lo.name = "Mtank";
    lo.moving = w.walking;
    lo.ground.mmapLoaded = true; lo.ground.mapFloorZ = w.z;
    lo.guardRead = f.anyInCombat;
    lo.isTank = true; lo.staying = !w.walking;
    lo.resting = r.phase == Phase::Rest;
    lo.iconsRead = true;
    lo.icons[7] = w.fightTicks > 0 ? 1000 : 0;
    if (f.anyInCombat)
    {
        lo.stayKnown = true; lo.stayX = w.x; lo.stayY = w.y; lo.stayZ = w.z;
        lo.hit = true; lo.hasAttacker = true; lo.attacker.guid = 1000; lo.attacker.entry = 11318; lo.attacker.hitting = true;
        lo.reaching = w.fightTicks == 4; lo.reachAggro = 1;
        lo.inWater = false; lo.casting = true; lo.moveFailed = false; lo.moveFailReason = "none";
    }
    f.pointLiquid = [](float, float, float) { return false; };       // set by the core reader (terrain liquid status)
    std::vector<fx::EngagedObs> engaged;
    if (f.anyInCombat)
    {
        for (Mob const& m : w.mobs)
            if (m.alive)
            {
                fx::EngagedObs e;
                e.handle = m.handle; e.entry = m.entry; e.x = m.x; e.y = m.y; e.z = m.z; e.distLead = Dist2(w.x, w.y, m.x, m.y);
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
    fx::ReadLeadFacts(f, lo);
}

struct Result
{
    std::size_t step = 0;
    State state = State::Running;
    int ticks = 0;
    bool done = false;
    std::map<std::string, int> orders;
    std::uint64_t fillUs = 0, maxUs = 0;
    std::uint32_t paths = 0;
};

Result RunScenario(ai::DungeonPlan const* plan, std::string const& scn, int maxTicks)
{
    ScriptWorld w;
    w.scn = scn;
    Run r = Start(plan);
    DungeonRun::MoveProbeCache probe;
    Result res;
    std::uint64_t samples = 0;
    for (int tick = 0; tick < maxTicks && !res.done; ++tick)
    {
        w.Prepare(r);
        Facts f;
        auto const t0 = std::chrono::steady_clock::now();
        BuildFacts(f, w, r);
        fx::LeadView lead;
        lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = !w.wiped; lead.mapId = plan->map;
        fx::ReadStepFacts(f, r, lead, probe, w);
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        res.fillUs += std::uint64_t(us);
        res.maxUs = std::max<std::uint64_t>(res.maxUs, std::uint64_t(us));
        ++samples;
        NoteChanged(f);

        std::string order = Tick(r, f, kTickMs);
        if (r.approachStandWanted)
        {
            PlanStep const* cur = Current(r);
            ApproachStandFound(r, f, true, cur ? cur->x - 20.0f : 0, cur ? cur->y : 0, cur ? cur->z : 0);
        }
        if (r.stallStandInWanted)
        {
            PlanStep const* cur = Current(r);
            StallStandIn(r, true, cur ? cur->x : 0, cur ? cur->y : 0, cur ? cur->z : 0);
        }
        (void)SpeedRateNow(r, f, kTickMs);

        if (!order.empty())
            ++res.orders[order.substr(0, order.find(' '))];
        std::istringstream os(order);
        std::string verb;
        os >> verb;
        PlanStep const* before = w.step;
        if (verb == "move")
        {
            os >> w.tx >> w.ty >> w.tz;
            w.walking = true;
        }
        else if (verb == "pull" || verb == "pullback")
        {
            w.fightTicks = 4;
            w.walking = false;
            // the policy remembers its own fight's units by the handle the facts carried (Run::ownFightGuids is filled by the policy itself)
        }
        if (w.walking)
        {
            float const d = Dist2(w.x, w.y, w.tx, w.ty);
            if (d <= 14.0f) { w.x = w.tx; w.y = w.ty; w.z = w.tz; w.walking = false; }
            else { w.x += (w.tx - w.x) / d * 14.0f; w.y += (w.ty - w.y) / d * 14.0f; w.z = w.tz; }
        }
        if (verb == "revive")
        {
            w.dead = 0;
            if (w.wiped) { w.wiped = false; w.x = 0; w.y = 0; w.z = 0; w.walking = false; }
        }
        if (w.fightTicks > 0 && --w.fightTicks == 0)
        {
            ++w.pullsSeen;
            if (scn == "death" && w.pullsSeen % 3 == 0)
                w.dead = 1;
            if (scn == "wipe" && before && before->kind == ai::PlanStepKind::Boss && !w.wipedOnce)
                w.wiped = true, w.wipedOnce = true;
            if (scn == "rest")
                w.mana = 20, w.health = 50;
            else
                w.mana = 55;
            for (Mob& m : w.mobs)
                if (w.step && Dist2(m.x, m.y, w.step->x, w.step->y) <= w.step->radius + 30.0f)
                    m.alive = false;
        }
        if (verb == "rest" || r.phase == Phase::Rest)
        {
            w.mana = std::min(100, w.mana + 15);
            w.health = std::min(100, w.health + 15);
        }
        res.ticks = tick + 1;
        if (r.state == State::Done || r.state == State::Paused)
        {
            res.done = true;
            if (r.state == State::Paused)
                std::fprintf(stderr, "  [%s] paused at step %zu: %s\n", scn.c_str(), r.step, r.note.c_str());
        }
    }
    res.step = r.step;
    res.state = r.state;
    res.paths = w.pathQueries;
    for (std::string const& n : w.notKnown)
        Changed().insert("(notknown) " + n);
    if (samples)
        res.fillUs /= samples;
    return res;
}

// ---- direct checks of the reads a scenario does not reach on its own (the policy asks for them only in rare states) ----
void DirectChecks(ai::DungeonPlan const* plan, std::vector<std::string>& failures)
{
    std::size_t firstPull = 0, firstMove = 0, firstBoss = 0;
    for (std::size_t i = plan->steps.size(); i-- > 0;)
    {
        if (plan->steps[i].kind == ai::PlanStepKind::Pull) firstPull = i;
        if (plan->steps[i].kind == ai::PlanStepKind::Move) firstMove = i;
        if (plan->steps[i].kind == ai::PlanStepKind::Boss) firstBoss = i;
    }
    // forward / shortcut / anchor: the policy asked on the previous tick
    {
        ScriptWorld w;
        Run r = Start(plan);
        r.step = firstMove;
        w.Prepare(r);
        w.x = plan->steps[firstMove].x; w.y = plan->steps[firstMove].y; w.z = plan->steps[firstMove].z;
        r.fightForwardWanted = firstPull;
        r.shortcutWanted = firstMove;
        r.anchorPathWanted = true; r.anchorKnown = true; r.anchorX = w.x + 5; r.anchorY = w.y; r.anchorZ = w.z;
        w.mobs.push_back(Mob{ 4000, 11318, plan->steps[firstPull].x + 3, plan->steps[firstPull].y, plan->steps[firstPull].z, true, false, false });
        Facts f;
        fx::LeadView lead; lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = plan->map;
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, r, lead, probe, w);
        NoteChanged(f);
        if (!(f.forwardRead && f.forwardFor == firstPull && f.shortcutRead && f.shortcutFor == firstMove && f.anchorPathRead && f.anchorReachable))
            failures.push_back("forward / shortcut / anchor reads were not filled");
    }
    // an unreadable path: leadPathGood false, no hop (never a direct walk), anchor not reachable
    {
        struct Blind : ScriptWorld { fx::PathObs PathFromLead(float, float, float) override { return fx::PathObs(); } };
        Blind w;
        Run r = Start(plan);
        r.step = firstMove;
        r.anchorPathWanted = true; r.anchorKnown = true;
        w.Prepare(r);
        w.x = plan->steps[firstMove].x - 100; w.y = plan->steps[firstMove].y; w.z = plan->steps[firstMove].z;
        Facts f;
        fx::LeadView lead; lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = plan->map;
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, r, lead, probe, w);
        if (f.approachKnown || f.leadPathGood || f.anchorReachable || !f.approachRead)
            failures.push_back("an unreadable path must give approachKnown=false, leadPathGood=false, anchorReachable=false");
    }
    // an island (no path from the lead's spot) and a walk back's path that ends short
    {
        struct NoWay : ScriptWorld
        {
            fx::PathObs PathFromLead(float px, float py, float pz) override
            {
                fx::PathObs p;
                p.valid = true; p.noPath = true; p.endX = x; p.endY = y; p.endZ = z;
                (void)px; (void)py; (void)pz;
                return p;
            }
        };
        NoWay w;
        Run r = Start(plan);
        r.step = firstMove;
        w.Prepare(r);
        w.x = plan->steps[firstMove].x - 30; w.y = plan->steps[firstMove].y; w.z = plan->steps[firstMove].z;
        Facts f;
        fx::LeadView lead; lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = plan->map;
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, r, lead, probe, w);
        NoteChanged(f);
        if (!f.leadIsland || f.leadPathGood)
            failures.push_back("a lead with no path from its spot must read island, not good path");
        NoWay w2;
        Run r2 = Start(plan);
        r2.step = firstMove + 1; r2.walkBack = true; r2.walkBackDirect = true; r2.walkBackIdx = firstMove;
        w2.Prepare(r2);
        w2.x = plan->steps[firstMove].x - 150; w2.y = plan->steps[firstMove].y; w2.z = plan->steps[firstMove].z;
        Facts g;
        fx::LeadView lead2; lead2.x = w2.x; lead2.y = w2.y; lead2.z = w2.z; lead2.alive = true; lead2.mapId = plan->map;
        DungeonRun::MoveProbeCache probe2;
        fx::ReadStepFacts(g, r2, lead2, probe2, w2);
        NoteChanged(g);
        if (!g.approachIncomplete || g.approachKnown)
            failures.push_back("a walk back whose path ends short must read approachIncomplete");
    }
    // boss search: alive, dead, own dead, not loaded grid
    for (int variant = 0; variant < 4; ++variant)
    {
        ScriptWorld w;
        Run r = Start(plan);
        r.step = firstBoss;
        r.bossSearchWanted = true;
        w.Prepare(r);
        w.x = plan->steps[firstBoss].x; w.y = plan->steps[firstBoss].y; w.z = plan->steps[firstBoss].z;
        if (variant == 1) w.mobs[0].alive = false;
        struct Spawns : ScriptWorld
        {
            int variant = 0;
            bool BossSpawns(ai::PlanStep const& s, std::vector<fx::BossSpawnObs>& out) override
            {
                fx::BossSpawnObs a;
                a.handle = 9; a.isOwn = variant == 2; a.entry = s.entry; a.x = s.x + 40; a.y = s.y; a.z = s.z;
                a.loaded = variant != 3; a.gridLoaded = variant != 3; a.alive = variant == 0;
                out.push_back(a);
                return true;
            }
        };
        Spawns sw;
        sw.variant = variant;
        sw.mobs = w.mobs; sw.step = w.step; sw.mobsStep = w.mobsStep; sw.x = w.x; sw.y = w.y; sw.z = w.z;
        Facts f;
        fx::LeadView lead; lead.x = w.x; lead.y = w.y; lead.z = w.z; lead.alive = true; lead.mapId = plan->map;
        DungeonRun::MoveProbeCache probe;
        fx::ReadStepFacts(f, r, lead, probe, sw);
        NoteChanged(f);
        bool ok = f.bossSearchRead;
        if (variant == 0) ok = ok && f.bossSearchAlive && f.bossAliveX != 0.0f;
        if (variant == 1) ok = ok && f.bossSearchDead && f.bossSearchCorpseGuid == 9;
        if (variant == 2) ok = ok && f.bossSearchOwnDead;
        if (variant == 3) ok = ok && f.bossSearchUnloaded && !f.bossSearchAlive && !f.bossSearchDead;
        if (!ok)
            failures.push_back("boss search variant " + std::to_string(variant));
    }
    // the lead floor and the lead's facts
    {
        Facts f;
        fx::LeadObs lo;
        lo.alive = true; lo.z = 100.0f; lo.ground.mmapLoaded = true; lo.ground.mapFloorZ = 70.0f;
        lo.casting = true; lo.inWater = false; lo.moveFailed = true; lo.moveFailReason = "no path";
        fx::ReadLeadFacts(f, lo);
        NoteChanged(f);
        if (!f.leadOffFloor || !f.leadCasting || !f.leadMoveFailed)
            failures.push_back("lead facts (off floor, casting, move failed)");
        fx::LeadObs ok2; ok2.z = 71.0f; ok2.ground.mmapLoaded = true; ok2.ground.mapFloorZ = 70.0f;
        Facts g;
        fx::ReadLeadFacts(g, ok2);
        if (g.leadOffFloor)
            failures.push_back("a lead 1 yd over the floor must not read off the floor");
        fx::LeadObs nomesh; nomesh.z = 100.0f; nomesh.ground.mapFloorZ = 70.0f;
        Facts h;
        fx::ReadLeadFacts(h, nomesh);
        if (h.leadOffFloor)
            failures.push_back("with no navmesh the floor cannot be judged: leadOffFloor stays false");
    }
    // a real player in the group (Facts::players) is the core reader's; the roll-up of a bot party with a member in combat and a mana user
    {
        Facts f;
        std::vector<fx::PartyEntry> party(2);
        party[0].name = "T"; party[0].present = true; party[0].alive = true; party[0].isLead = true; party[0].inCombat = true; party[0].usesMana = true; party[0].manaPct = 40; party[0].eating = true;
        party[1].name = "P"; party[1].present = true; party[1].alive = true; party[1].healer = true; party[1].usesMana = true; party[1].manaPct = 70; party[1].healthPct = 60; party[1].canResurrect = true; party[1].dps = false;
        fx::RollUpParty(f, party);
        if (!(f.anyInCombat && f.leadInCombat && f.minManaPct == 40 && f.minHealerManaPct == 70 && f.minHealthPct == 60 && f.rezzer && f.eating == 1))
            failures.push_back("party roll-up");
    }
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: test_run_facts rows.tsv RUN-S6-F-FIELDS.tsv [--table]\n");
        return 2;
    }
    int failures = 0;
    auto fail = [&](std::string const& why) { ++failures; std::printf("FAIL: %s\n", why.c_str()); };

    std::string name;
    auto table = LoadPlan(argv[1], name);
    ai::DungeonPlan const* plan = table->Find(name);
    if (!plan)
    {
        std::printf("FAIL: plan not found in %s\n", argv[1]);
        return 1;
    }
    std::printf("plan %s: %zu steps\n", name.c_str(), plan->steps.size());

    // ---- the scenarios ----
    for (char const* scn : { "happy", "rest", "death", "wipe", "pack", "hop" })
    {
        Result res = RunScenario(plan, scn, 4000);
        std::printf("scenario %-6s ticks %4d step %zu/%zu state %s  orders:", scn, res.ticks, res.step, plan->steps.size(), StateName(res.state));
        for (auto const& kv : res.orders)
            std::printf(" %s=%d", kv.first.c_str(), kv.second);
        std::printf("  glue cost per tick: mean %lu us, max %lu us, path queries %u\n", (unsigned long)res.fillUs, (unsigned long)res.maxUs, res.paths);
        bool const walked = res.state == State::Done && res.step >= plan->steps.size();
        if (!walked)
            fail(std::string("scenario ") + scn + " did not walk the plan to its end");
    }

    {
        std::vector<std::string> bad;
        DirectChecks(plan, bad);
        for (std::string const& b : bad)
            fail(b);
        std::printf("direct checks (forward, shortcut, anchor, unreadable path, boss search x4, lead floor, roll-up): %s\n", bad.empty() ? "ok" : "FAILED");
    }

    // ---- the coverage table ----
    struct Row { std::string field, cls, status; };
    std::vector<Row> rows;
    {
        std::ifstream in(argv[2]);
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line))
        {
            auto c = Split(line, '\t');
            if (c.size() >= 4)
                rows.push_back(Row{ c[0], c[2], c[3] });
        }
    }
    std::set<std::string> declared;
#define TB_DECL(name) declared.insert(#name);
    TB_DCAP_FIELDS_Facts(TB_DECL)
#undef TB_DECL
    declared.insert("pointLiquid");
    if (rows.size() != declared.size())
        fail("the table has " + std::to_string(rows.size()) + " rows, Facts has " + std::to_string(declared.size()) + " fields");
    std::map<std::string, int> byStatus;
    std::map<std::string, int> byClassStatus;
    int notFilledYet = 0;
    for (Row const& r : rows)
    {
        if (!declared.count(r.field))
            fail("table row for unknown field " + r.field);
        ++byStatus[r.status];
        if (r.cls == "A" || r.cls == "yes" || r.cls == "check")
            ++byClassStatus[r.cls + "/" + r.status];
        bool const changed = Changed().count(r.field) != 0;
        if ((r.status == "skipped" || r.status == "never" || r.status == "notknown") && changed)
            fail("field " + r.field + " is " + r.status + " in the table but a scenario changed it from its default");
        if ((r.status == "filled" || r.status == "config" || r.status == "forced") && !changed)
            ++notFilledYet;
        if ((r.cls == "A" || r.cls == "yes") && (r.status == "skipped" || r.status == "never"))
            fail("field " + r.field + " (class " + r.cls + ") must be filled, config, forced or notknown");
    }
    std::printf("table: %zu fields;", rows.size());
    for (auto const& kv : byStatus)
        std::printf(" %s %d,", kv.first.c_str(), kv.second);
    std::printf("\n");
    for (auto const& kv : byClassStatus)
        std::printf("  %s: %d\n", kv.first.c_str(), kv.second);
    std::printf("filled/config/forced fields the scripted scenarios changed from their default: ");
    int changedCount = 0;
    for (Row const& r : rows)
        if ((r.status == "filled" || r.status == "config" || r.status == "forced") && Changed().count(r.field))
            ++changedCount;
    std::printf("%d; not changed from the default (core-only reads, or the value equals the default): %d\n", changedCount, notFilledYet);
    if (argc > 3 && std::string(argv[3]) == "--table")
        for (Row const& r : rows)
            if ((r.status == "filled" || r.status == "config" || r.status == "forced") && !Changed().count(r.field))
                std::printf("  not changed: %s (%s, %s)\n", r.field.c_str(), r.cls.c_str(), r.status.c_str());
    std::printf("notknown answers given by the step world:");
    for (std::string const& n : Changed())
        if (n.rfind("(notknown) ", 0) == 0)
            std::printf(" %s", n.c_str() + 11);
    std::printf("\n");

    // ---- the forcing: a conf that switches unported features on must not reach the facts ----
    {
        Facts f;
        fx::Config cfg;
        fx::ApplyConfig(f, cfg);
        f.stragglerPull = true; f.autoCc = true; f.pullStand = true; f.dryWalk = true; f.walkBackRescue = true; f.teleportReturnYd = 120.0f; f.respawnNest = true;
        f.joinerCeiling = 5; f.reviveHoldYd = 100.0f;
        fx::ApplyConfig(f, cfg);
        if (f.stragglerPull || f.autoCc || f.pullStand || f.dryWalk || f.walkBackRescue || f.teleportReturnYd != 0.0f || f.respawnNest || f.joinerCeiling || f.reviveHoldYd != 0.0f)
            fail("ApplyConfig left a forced-off feature on");
        else
            std::printf("ApplyConfig forces the unported features off: ok\n");
    }
    // ---- handles ----
    {
        fx::HandleTable<std::uint64_t> t;
        std::uint64_t a = t.Handle(11, 0), b = t.Handle(22, 0), a2 = t.Handle(11, 5);
        std::uint64_t back = 0;
        bool ok = a == a2 && a != b && a != 0 && t.Resolve(a, back) && back == 11 && !t.Resolve(999, back);
        // fill to the cap: the table never grows past it and a handle is never reused
        for (std::uint64_t k = 100; k < 100 + fx::kHandleCap + 50; ++k)
            t.Handle(k, std::uint32_t(k));
        ok = ok && t.Size() <= fx::kHandleCap && t.Issued() > fx::kHandleCap;
        if (!ok)
            fail("handle table");
        else
            std::printf("handle table: stable, resolves back, capped at %zu: ok\n", fx::kHandleCap);
    }
    // ---- an absent member counts dead and far ----
    {
        Facts f;
        std::vector<fx::PartyEntry> party(3);
        party[0].name = "A"; party[0].present = true; party[0].alive = true; party[0].isLead = true; party[0].fact.name = "A";
        party[1].name = "B"; party[1].present = false;
        party[2].name = "C"; party[2].present = true; party[2].alive = false;
        fx::RollUpParty(f, party);
        if (!(f.total == 3 && f.alive == 1 && f.dead == 2 && f.maxMemberYd >= 1000.0f && f.deadNames.size() == 2 && f.members.size() == 1))
            fail("absent member roll-up");
        else
            std::printf("absent member counts dead and 1000 yd away: ok\n");
    }
    std::printf("%s\n", failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures ? 1 : 0;
}
