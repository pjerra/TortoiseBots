// Dashboard round 1 (FOREVER-DASH-RUNS-SPEC.md section 6): the pure half of the Commander and Dungeon runs feeds, core-free.
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/dash_runs_check.cpp -o /tmp/dash_runs_check && /tmp/dash_runs_check
// `--print-golden` writes the golden COMMANDERS datagram and `--print-summary` a finished run's summary file: the Go tests' fixtures
// (tools/observability/internal/state/testdata, internal/dungeons/testdata). Either prints and exits.
#include "forever/src/dash/TbDashRunsPure.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace TortoiseBots::DashRuns::pure;
namespace CT = TortoiseBots::CommanderTelemetry;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static StepView Step(int leg, char const* kind, float x, float y, float r = 0.0f, uint32_t entry = 0, char const* label = "")
{
    StepView s;
    s.leg = leg;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.radius = r;
    s.entry = entry;
    s.label = label;
    return s;
}

static MemberView Member(char const* name, char const* cls, char const* role, uint32_t guid, bool lead, float x)
{
    MemberView m;
    m.name = name;
    m.className = cls;
    m.role = role;
    m.guid = guid;
    m.level = 18;
    m.hp = 900;
    m.maxHp = 1000;
    m.power = 40;
    m.maxPower = 100;
    m.powerType = std::string(cls) == "warrior" ? "rage" : "mana";
    m.lead = lead;
    m.map = 389;
    m.zone = 2437;
    m.x = x;
    m.y = -20.0f;
    m.z = -22.0f;
    return m;
}

// A Ragefire Chasm run at step 3 of 6: two legs, one boss (11520 Taragaman), a party of three.
static RunView Rfc()
{
    RunView r;
    r.leaderName = "Mtank";
    r.leaderGuid = 7;
    r.commanderName = "Mcmd";
    r.commanderGuid = 9;
    r.commanderOnline = true;
    r.groupId = 3;
    r.planName = "ragefire_chasm";
    r.planMap = 389;
    r.steps = { Step(0, "move", 1.0f, 2.0f), Step(0, "pull", 3.0f, 4.0f, 12.0f, 11321, "first pack"), Step(0, "move", 5.0f, 6.0f),
                Step(1, "pull", 7.0f, 8.0f, 15.0f, 11322), Step(1, "boss", 9.0f, 10.0f, 30.0f, 11520, "Taragaman"), Step(1, "loot", 9.5f, 10.5f) };
    r.legNames = { {0, "entrance"}, {1, "depths"} };
    r.state = "running";
    r.step = 2;
    r.phase = "walk";
    r.note = "walking to the pack";
    r.startedUnix = 1763560000;
    r.changedUnix = 1763560100;
    r.deaths = 1;
    r.wipes = 0;
    r.startedMs = 5000;
    r.events = {
        { 1763560000, -1, -1, "start", "" },
        { 1763560030, 1, 0, "pull", "" },
        { 1763560060, 1, 0, "death", "Mdps" },
        { 1763560100, 2, 0, "step", "" },
    };
    r.members = { Member("Mtank", "warrior", "tank", 7, true, 100.0f), Member("Mheal", "priest", "healer", 8, false, 105.0f),
                  Member("Mdps", "mage", "dps", 10, false, 110.5f) };
    r.members[2].alive = false;
    r.members[2].hp = 0;
    r.bosses = { { 11520, "Taragaman the Hungerer", false, false } };
    return r;
}

// A finished Ragefire Chasm run: done, its one boss read dead, one death, no wipe.
static RunView FinishedRfc()
{
    RunView r = Rfc();
    r.ended = true;
    r.done = true;
    r.step = 6;
    r.state = "done";
    r.endedUnix = 1763560400;
    r.bosses = { { 11520, "Taragaman the Hungerer", true, true } };
    return r;
}

static std::string Golden()
{
    std::vector<CT::BindingInfo> bindings{ Binding(Rfc()) };
    return CT::BuildPayloadParts(12, 1763559000, 5, 1763560110, bindings, 1763560110123).front();
}

static std::string const kGolden = R"GOLDEN({"v":12,"session":1763559000,"seq":5,"ts":1763560110,"ts_ms":1763560110123,"type":"COMMANDERS","commanders":[{"commander":"Mcmd","commander_guid":9,"bound_at":1763560000,"online":true,"view":false,"group":3,"lead":"Mtank","bots":[{"name":"Mtank","guid":7,"class":"warrior","role":"tank","level":18,"hp":900,"max_hp":1000,"power":40,"max_power":100,"power_type":"rage","alive":true,"lead":true,"map":389,"zone":2437,"x":100.0,"y":-20.0,"z":-22.0},{"name":"Mheal","guid":8,"class":"priest","role":"healer","level":18,"hp":900,"max_hp":1000,"power":40,"max_power":100,"power_type":"mana","alive":true,"lead":false,"map":389,"zone":2437,"x":105.0,"y":-20.0,"z":-22.0},{"name":"Mdps","guid":10,"class":"mage","role":"dps","level":18,"hp":0,"max_hp":1000,"power":40,"max_power":100,"power_type":"mana","alive":false,"lead":false,"map":389,"zone":2437,"x":110.5,"y":-20.0,"z":-22.0}],"run":{"plan":"ragefire_chasm","state":"running","step":3,"steps":6,"leg":"entrance","kind":"move","phase":"walk","label":"","note":"walking to the pack","changed":1763560100,"notes":[{"at":1763560000,"step":0,"leg":"","kind":"","phase":"start","state":"running","note":""},{"at":1763560030,"step":2,"leg":"entrance","kind":"pull","phase":"pull","state":"running","note":""},{"at":1763560060,"step":2,"leg":"entrance","kind":"pull","phase":"death","state":"running","note":"Mdps"},{"at":1763560100,"step":3,"leg":"entrance","kind":"move","phase":"step","state":"running","note":""}],"plan_steps":[{"i":1,"k":"move","x":1.0,"y":2.0},{"i":2,"k":"pull","x":3.0,"y":4.0,"r":12.0,"e":11321,"c":"first pack"},{"i":3,"k":"move","x":5.0,"y":6.0}]},"orders":[]}]})GOLDEN";

int main(int argc, char** argv)
{
    if (argc > 1 && !std::strcmp(argv[1], "--print-summary"))
    {
        SummaryMeta meta;
        meta.coreRevision = "abc123";
        std::printf("%s\n", SummaryJson(FinishedRfc(), meta).c_str());
        return 0;
    }
    if (argc > 1 && !std::strcmp(argv[1], "--print-golden"))
    {
        std::printf("%s", Golden().c_str());
        return 0;
    }

    // ---- RunView -> BindingInfo maps every run field ----
    {
        CT::BindingInfo const b = Binding(Rfc());
        CHECK(b.commander == "Mcmd" && b.commanderGuid == 9 && b.commanderOnline && b.groupId == 3 && b.lead == "Mtank");
        CHECK(b.boundAt == 1763560000);
        CHECK(b.bots.size() == 3 && b.bots[0].lead && !b.bots[1].lead && !b.bots[2].alive && b.bots[2].hp == 0);
        CHECK(b.bots[0].role == "tank" && b.bots[0].className == "warrior" && b.bots[0].powerType == "rage" && b.bots[0].mapId == 389 && b.bots[0].zoneId == 2437);
        CHECK(b.bots[2].x == 110.5f && b.bots[2].level == 18 && b.bots[2].maxHp == 1000 && b.bots[2].subgroup == -1);
        CHECK(b.orders.empty() && !b.mobsReported && !b.objectsReported && !b.bossesReported && !b.commanderPlaced);
        CT::RunInfo const& r = b.run;
        CHECK(r.present && r.plan == "ragefire_chasm" && r.state == "running" && r.steps == 6 && r.step == 3);
        CHECK(r.leg == "entrance" && r.kind == "move" && r.phase == "walk" && r.note == "walking to the pack" && r.changedAt == 1763560100);
        CHECK(r.label.empty());
        // no `.tb master` commander: the leader stands in for it
        RunView alone = Rfc();
        alone.commanderName.clear();
        alone.commanderGuid = 0;
        alone.commanderOnline = false;
        CT::BindingInfo const a = Binding(alone);
        CHECK(a.commander == "Mtank" && a.commanderGuid == 7 && !a.commanderOnline);
        // a finished run past the last step: step = steps, the last leg
        RunView done = Rfc();
        done.step = 6;
        done.state = "done";
        CT::BindingInfo const d = Binding(done);
        CHECK(d.run.step == 6 && d.run.leg == "depths" && d.run.state == "done");
    }

    // ---- the notes window keeps the last 30 entries, oldest first ----
    {
        RunView r = Rfc();
        r.events.clear();
        for (int i = 0; i < 45; ++i)
            r.events.push_back({ 1763560000 + i, i % 6, i % 2, "step", "n" + std::to_string(i) });
        std::vector<CT::RunNoteInfo> const notes = Notes(r);
        CHECK(notes.size() == CT::kMaxRunNotes);
        CHECK(notes.front().note == "n15" && notes.back().note == "n44" && notes.front().at == 1763560015);
        CHECK(notes.front().step == 4 && notes.front().kind == "pull" && notes.front().leg == "depths" && notes.front().phase == "step" && notes.front().state == "running");
        CHECK(Binding(r).run.notes.size() == CT::kMaxRunNotes);
        // an event with no step or leg: step 0, no leg, no kind; pause / done / stop name their state
        RunView e = Rfc();
        e.events = { { 5, -1, -1, "pause", "player-in-group" }, { 6, -1, -1, "stop", "stopped by hand" } };
        std::vector<CT::RunNoteInfo> const en = Notes(e);
        CHECK(en[0].step == 0 && en[0].leg.empty() && en[0].kind.empty() && en[0].state == "paused" && en[0].note == "player-in-group");
        CHECK(en[1].state == "stopped");
    }

    // ---- the plan_steps window ----
    {
        RunView r;
        r.planName = "long";
        for (int i = 0; i < 12; ++i)
            r.steps.push_back(Step(0, "move", float(i), 0.0f));
        for (int i = 0; i < 50; ++i)
            r.steps.push_back(Step(1, i == 40 ? "boss" : "pull", float(100 + i), 0.0f, 10.0f, i == 40 ? 777u : 0u, i == 40 ? "the boss" : ""));
        r.steps.push_back(Step(2, "move", 500.0f, 0.0f));
        r.legNames = { {0, "a"}, {1, "b"}, {2, "c"} };
        // a short leg is listed whole
        r.step = 3;
        std::vector<CT::RunStepInfo> w = StepWindow(r);
        CHECK(w.size() == 12 && w.front().index == 1 && w.back().index == 12);
        // a 50-row leg: at most 20 rows, the current step inside and kMaxRunSteps/4 = 5 rows of look-behind
        r.step = 12 + 30;
        w = StepWindow(r);
        CHECK(w.size() == CT::kMaxRunSteps);
        CHECK(w.front().index == 12 + 30 - 5 + 1 && w.back().index == 12 + 30 - 5 + 20);
        bool seen = false;
        for (CT::RunStepInfo const& s : w)
            seen = seen || s.index == uint32_t(12 + 30 + 1);
        CHECK(seen);
        // the end of a leg: the window stops at the leg's last row
        r.step = 12 + 49;
        w = StepWindow(r);
        CHECK(w.size() == CT::kMaxRunSteps && w.back().index == 12 + 50);
        // fields of a row
        r.step = 12 + 40;
        w = StepWindow(r);
        for (CT::RunStepInfo const& s : w)
            if (s.index == 12 + 40 + 1)
                CHECK(s.kind == "boss" && s.entry == 777 && s.radius == 10.0f && s.label == "the boss");
        // no plan: no steps and the run has no leg
        RunView none;
        CHECK(StepWindow(none).empty() && CurrentLeg(none) == -1 && Binding(none).run.step == 0);
    }

    // ---- the outcome rule ----
    {
        RunView r = Rfc();
        CHECK(Outcome(r) == "running");
        r.ended = true;
        r.done = true;
        r.bosses = { { 11520, "Taragaman", true, true } };
        CHECK(Outcome(r) == "cleared");
        // FAILING CASE (spec section 6): a run `done` with one plan boss never read must be partial, not cleared
        r.bosses = { { 11520, "Taragaman", true, true }, { 11521, "Second", false, false } };
        CHECK(Outcome(r) == "partial");
        // a boss read alive at the end is not killed either
        r.bosses = { { 11520, "Taragaman", true, false } };
        CHECK(Outcome(r) == "partial");
        // a plan with no boss step that finished is cleared
        r.bosses.clear();
        CHECK(Outcome(r) == "cleared");
        RunView s = Rfc();
        s.ended = true;
        s.stopped = true;
        s.stopReason = "stopped by hand";
        CHECK(Outcome(s) == "stopped");
        s.stopReason = "the party wiped three times";
        CHECK(Outcome(s) == "wiped_out");
        s.stopReason = "Wipe limit";
        CHECK(Outcome(s) == "wiped_out");
        // ended, neither done nor stopped: partial
        RunView p = Rfc();
        p.ended = true;
        CHECK(Outcome(p) == "partial");
    }

    // ---- COMMANDERS is byte-equal to the golden string ----
    {
        std::string const g = Golden();
        if (g != kGolden)
            std::printf("golden differs:\n%s\n", g.c_str());
        CHECK(g == kGolden);
        CHECK(g.find("\"type\":\"COMMANDERS\"") != std::string::npos && g.find("\"orders\":[]") != std::string::npos);
        CHECK(g.find("\"mobs\"") == std::string::npos && g.find("\"objects\"") == std::string::npos && g.find("\"plan_steps\"") != std::string::npos);
    }

    // ---- an empty list is sent exactly once after the last run ----
    {
        CommandersGate gate;
        using C = CommandersGate::Cycle;
        CHECK(gate.Next(0) == C::Skip);                 // nothing stood yet: nothing is sent
        CHECK(gate.Next(2) == C::Send);
        CHECK(gate.Next(1) == C::Send);
        CHECK(gate.Next(0) == C::SendEmpty);            // the last run ended: one empty list
        CHECK(gate.Next(0) == C::Skip);                 // and no more
        CHECK(gate.Next(0) == C::Skip);
        CHECK(gate.Next(1) == C::Send);                 // a new run, then it ends again
        CHECK(gate.Next(0) == C::SendEmpty);
        // the empty payload is a valid COMMANDERS list
        std::string const empty = CT::BuildPayloadParts(12, 1, 2, 3, {}).front();
        CHECK(empty.find("\"commanders\":[]") != std::string::npos);
    }

    // ---- one summary job per (leader, startedMs); only a landed write marks it done ----
    {
        SummaryQueue q;
        SummaryJob job;
        job.leaderGuid = 7;
        job.startedMs = 5000;
        job.stem = "x";
        job.json = "{}";
        CHECK(!q.Done(7, 5000) && !q.Holds(7, 5000));
        CHECK(q.Add(job));                              // the run's own end enqueues it
        CHECK(q.Holds(7, 5000) && !q.Done(7, 5000));    // queued is not written
        CHECK(q.Next() && q.Next()->stem == "x");
        CHECK(!q.Add(job));                             // the same run seen again (the run table poll): nothing added
        // FAILING CASE: a transient write failure must not mark the run done, and the next cycle must retry it
        CHECK(q.Failed(7, 5000));
        CHECK(q.Holds(7, 5000) && !q.Done(7, 5000));
        CHECK(q.Next() && q.Next()->stem == "x");
        // the atomic rename succeeded: done, and never added again
        q.Succeeded(7, 5000);
        CHECK(q.Done(7, 5000) && !q.Holds(7, 5000) && q.Pending() == 0);
        CHECK(!q.Add(job));

        // a write that keeps failing is dropped after the bounded attempts, never marked done
        SummaryQueue bad;
        SummaryJob again;
        again.leaderGuid = 8;
        again.startedMs = 7000;
        again.stem = "y";
        again.json = "{}";
        CHECK(bad.Add(again));
        for (int i = 0; i < SummaryQueue::kMaxAttempts - 1; ++i)
            CHECK(bad.Failed(8, 7000) && bad.Holds(8, 7000));
        CHECK(!bad.Failed(8, 7000));                    // the last attempt drops it
        CHECK(!bad.Holds(8, 7000) && !bad.Done(8, 7000) && bad.Pending() == 0 && bad.Dropped() == 1);
        CHECK(bad.GaveUp(8, 7000) && !bad.Add(again));  // and the run table poll does not restart the 15 attempts

        // two runs of one leader in the same second are two jobs (different run clocks), oldest key first
        SummaryQueue multi;
        SummaryJob first = job;
        SummaryJob second = job;
        second.startedMs = 6000;
        SummaryJob other = job;
        other.leaderGuid = 8;
        CHECK(multi.Add(first) && multi.Add(second) && multi.Add(other));
        CHECK(multi.Pending() == 3);
        CHECK(multi.Next() && multi.Next()->leaderGuid == 7 && multi.Next()->startedMs == 5000);
        multi.Succeeded(7, 5000);
        CHECK(multi.Next() && multi.Next()->startedMs == 6000);
    }

    // ---- the summary file ----
    {
        RunView r = FinishedRfc();
        SummaryMeta meta;
        meta.coreRevision = "abc123";
        std::string const json = SummaryJson(r, meta);
        CHECK(SummaryFileName(r) == "ragefire_chasm-1763560000-5000-7.summary.json");
        char const* const want[] = {
            "\"run\":\"ragefire_chasm-1763560000-5000-7\"", "\"plan\":\"ragefire_chasm\"", "\"map\":389", "\"dungeon\":{\"map\":389}",
            "\"started\":1763560000", "\"ended\":1763560400", "\"duration_ms\":400000", "\"status\":\"done\"", "\"outcome\":\"cleared\"",
            "\"bosses\":[{\"entry\":11520,\"name\":\"Taragaman the Hungerer\",\"killed_at_s\":null,\"killed\":true}]",
            "\"wipes\":0", "\"deaths\":1", "\"party\":[{\"class\":\"warrior\",\"role\":\"tank\",\"level\":18}", "\"driver\":\"human\"",
            "\"build\":{\"module\":\"forever-port\",\"core\":\"abc123\"}",
        };
        for (char const* w : want)
            if (json.find(w) == std::string::npos)
            {
                std::printf("summary lacks %s\n%s\n", w, json.c_str());
                CHECK(false);
            }
        CHECK(json.find("stop_reason") == std::string::npos);
        // two starts of the same leader and plan inside one wall-clock second: the run's own clock tells them apart
        RunView sameSecond = r;
        sameSecond.startedMs = 6000;
        CHECK(sameSecond.startedUnix == r.startedUnix && sameSecond.planName == r.planName && sameSecond.leaderGuid == r.leaderGuid);
        if (SummaryFileName(sameSecond) == SummaryFileName(r))
            std::printf("two runs in one second share the file %s\n", SummaryFileName(r).c_str());
        CHECK(SummaryFileName(sameSecond) != SummaryFileName(r));
        CHECK(SummaryJson(sameSecond, meta) != json);
        // a stopped run names the reason; a run id never carries a character the file name cannot
        RunView s = Rfc();
        s.ended = true;
        s.stopped = true;
        s.stopReason = "wipe \"limit\"";
        s.planName = "a b/c";
        std::string const sj = SummaryJson(s, meta);
        CHECK(sj.find("\"outcome\":\"wiped_out\"") != std::string::npos && sj.find("\"status\":\"stopped\"") != std::string::npos);
        CHECK(sj.find("\"stop_reason\":\"wipe \\\"limit\\\"\"") != std::string::npos);
        CHECK(SummaryFileName(s) == "a_b_c-1763560000-5000-7.summary.json");
    }

    // ---- a finished run lingers on the Commander tab, then leaves ----
    {
        RunView r = Rfc();
        CHECK(OnCommanderTab(r, 1763561000));
        r.ended = true;
        r.endedUnix = 1763560400;
        CHECK(OnCommanderTab(r, 1763560400 + kLingerSecs - 1));
        CHECK(!OnCommanderTab(r, 1763560400 + kLingerSecs));
    }

    // ---- the role of a host bot ----
    {
        CHECK(RoleOfBrainLines({ "brain: engine", "combat strategies: tank assist, melee" }) == "tank");
        CHECK(RoleOfBrainLines({ "brain: engine", "combat strategies: heal, holy" }) == "healer");
        CHECK(RoleOfBrainLines({ "brain: engine", "combat strategies: heal interrupt, dps" }) == "dps");   // whole names only
        CHECK(RoleOfBrainLines({ "brain: spike (engine off)", "combat strategies: tank assist" }).empty());
        CHECK(RoleOfBrainLines({}).empty());
    }

    if (failures)
        return 1;
    std::printf("RESULT: PASS (dash_runs_check)\n");
    return 0;
}
