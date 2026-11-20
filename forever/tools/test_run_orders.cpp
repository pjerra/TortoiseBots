// Standalone test of the PURE half of the run order executor (forever/src/host/run/TbRunOrdersPure.h; package O6, forever/docs/RUN-S6-O-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_orders.cpp -o /tmp/test_run_orders && /tmp/test_run_orders
// It runs next to the run leader's own header (runtime/DungeonRunPolicy.h) so that every refusal text is checked against the policy's parsers.
#include "forever/src/host/run/TbRunOrdersPure.h"
#include "runtime/DungeonRunPolicy.h"

#include <cstdio>
#include <map>

namespace od = TortoiseBots::run::orders;
namespace DR = TortoiseBots::DungeonRun;
namespace RC = TortoiseBots::RemoteCommander;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static od::Parsed Parse(std::string const& order, bool expect = true)
{
    od::Parsed p;
    std::string code, message;
    bool const ok = od::ParseOrder(order, p, code, message);
    if (ok != expect)
        std::printf("  parse of '%s': ok=%d code=%s msg=%s\n", order.c_str(), ok, code.c_str(), message.c_str());
    CHECK(ok == expect);
    return p;
}

static std::string CodeOf(std::string const& order)
{
    od::Parsed p;
    std::string code, message;
    od::ParseOrder(order, p, code, message);
    return code;
}

static void TestParse()
{
    // The strings the policy builds (DungeonRunPolicy.h: MoveOrder 3408, WalkOrder 3415, pull/pullback 11716, attack 10309, focus 10652, rest 22903, revive 22084).
    od::Parsed p = Parse(DR::MoveOrder(-405.3f, 12.5f, -19.9f, false));
    CHECK(p.verb == od::Verb::Move && !p.direct && std::fabs(p.x + 405.3f) < 0.06f && std::fabs(p.y - 12.5f) < 0.06f && std::fabs(p.z + 19.9f) < 0.06f);
    p = Parse(DR::MoveOrder(1.0f, 2.0f, 3.0f, true));
    CHECK(p.verb == od::Verb::Move && p.direct);
    p = Parse(DR::WalkOrder("Mdpsa", 4.0f, 5.0f, 6.0f));
    CHECK(p.verb == od::Verb::Walk && p.bot == "Mdpsa" && p.z == 6.0f);
    p = Parse("pull 12 body");
    CHECK(p.verb == od::Verb::Pull && p.handle == 12 && p.body);
    p = Parse("pull 12");
    CHECK(p.verb == od::Verb::Pull && p.handle == 12 && !p.body);
    p = Parse("pullback 4096");
    CHECK(p.verb == od::Verb::Pullback && p.handle == 4096);
    p = Parse("attack 7 Mpriest");
    CHECK(p.verb == od::Verb::Attack && p.handle == 7 && p.bot == "Mpriest");
    p = Parse("attack 7");
    CHECK(p.verb == od::Verb::Attack && p.bot.empty());
    p = Parse("focus 9");
    CHECK(p.verb == od::Verb::Focus && p.handle == 9);
    CHECK(Parse("rest").verb == od::Verb::Rest);
    CHECK(Parse("REST").verb == od::Verb::Rest);
    CHECK(Parse("revive party").verb == od::Verb::ReviveParty);
    CHECK(Parse("stay").verb == od::Verb::Stay);
    CHECK(Parse("follow").verb == od::Verb::Follow);
    p = Parse("summon Mdpsb");
    CHECK(p.verb == od::Verb::Summon && p.bot == "Mdpsb");

    // Refused by grammar: the verbs the module does not carry out are `unsupported`, a broken one `invalid`.
    for (char const* order : { "escape from 1 2 3 4 5 6", "rescue from a to 1 2 3", "fetch 1 2 3", "return Mdps 389", "rejoin Mdps", "cc star 5 Mmage",
             "cc clear Mmage", "ritual 5 A B", "gossip 5 Mdps 1", "state 5", "enter near" })
    {
        Parse(order, false);
        CHECK(CodeOf(order) == "unsupported");
    }
    for (char const* order : { "", "dance", "move 1 2", "move 1 2 3 sideways", "move nan 2 3", "walk Mdps 1 2", "pull", "pull abc", "attack", "attack 0", "focus", "focus 1 2", "rest now", "revive Mdps" })
    {
        Parse(order, false);
        std::string const code = CodeOf(order);
        CHECK(code == "invalid" || code == "unsupported");
    }
    // S9 (O9): loot, lootmethod, open and use have a grammar now (the executor judges them: forever/tools/test_run_tasks.cpp); a broken one is `invalid`.
    CHECK(Parse("loot 5 Mdps").verb == od::Verb::Loot);
    CHECK(Parse("lootmethod ffa").verb == od::Verb::LootMethod);
    CHECK(Parse("open entry:1 Mdps").verb == od::Verb::Open);
    CHECK(Parse("use 5 Mdps").verb == od::Verb::Use);
    for (char const* order : { "loot", "loot 5", "lootmethod", "lootmethod sideways", "open", "open entry:0 Mdps", "use", "use 5" })
    {
        Parse(order, false);
        CHECK(CodeOf(order) == "invalid");
    }
    // The Turtle command's options that the module does not port are refused, not dropped.
    Parse("pull 12 ranged", false);
    CHECK(CodeOf("pull 12 ranged") == "unsupported");
    Parse("pullback 12 5", false);
    CHECK(CodeOf("pullback 12 5") == "unsupported");
}

// The refusal texts against the run leader's own parsers (the whole point of "the answer is in the Turtle shape").
static void TestTexts()
{
    RC::MoveFacts f;
    f.maxDistance = 150.0f;
    f.distanceFromLead = 40.0f;
    f.pathType = 0x0001;

    // ok: a complete path that ends on the point
    CHECK(od::JudgeMove(f).ok);

    // too far
    f.distanceFromLead = 200.0f;
    od::Judged j = od::JudgeMove(f);
    CHECK(!j.ok && j.code == "too-far");
    CHECK(DR::ReactTooFarAnswer(j.text));
    CHECK(!DR::UnreachableAnswer(j.text));
    CHECK(DR::DirectStragglerName(j.text).empty());      // a plain too-far is not a direct step's straggler

    // no path, and a path that goes nowhere from the lead's own spot
    f.distanceFromLead = 40.0f;
    f.pathType = 0x0008;
    j = od::JudgeMove(f);
    CHECK(!j.ok && j.code == "unreachable" && DR::UnreachableAnswer(j.text) && DR::IsPathRefusal(j.text) && !DR::LeadNoProgressRefusal(j.text));
    f.pathType = 0x0004;
    f.endFromLead = 0.5f;
    j = od::JudgeMove(f);
    CHECK(!j.ok && j.code == "unreachable" && DR::LeadNoProgressRefusal(j.text) && DR::UnreachableAnswer(j.text));
    // a complete path that ends far from the point
    f.pathType = 0x0001;
    f.endFromLead = 30.0f;
    f.endOffset = 12.0f;
    j = od::JudgeMove(f);
    CHECK(!j.ok && j.code == "off-mesh" && DR::UnreachableAnswer(j.text) && DR::IsPathRefusal(j.text));
    // hold here (within a yard) is ok whatever the path says
    f.distanceFromLead = 0.5f;
    f.pathType = 0x0008;
    CHECK(od::JudgeMove(f).ok);
    // a gather onto the lead's own spot within 3 yd
    f.distanceFromLead = 2.5f;
    f.leadSpotGather = true;
    f.fromRunLeader = true;
    CHECK(od::JudgeMove(f).ok);
    f.leadSpotGather = false;
    CHECK(!od::JudgeMove(f).ok);

    // direct steps
    std::vector<od::DirectFollower> party;
    od::DirectFollower a;
    a.name = "Mpriest";
    a.horizontalToLead = 3.0f;
    a.pathComplete = true;
    party.push_back(a);
    CHECK(od::JudgeDirect("Mtank", 10.0f, 1.0f, true, 20.0f, 20.0f, true, party).ok);
    j = od::JudgeDirect("Mtank", 40.0f, 1.0f, true, 20.0f, 20.0f, true, party);
    CHECK(!j.ok && j.code == "too-far" && !DR::ReactTooFarAnswer(j.text));
    j = od::JudgeDirect("Mtank", 10.0f, 1.0f, false, 0.0f, 20.0f, true, party);
    CHECK(!j.ok && j.code == "no-ground");
    j = od::JudgeDirect("Mtank", 10.0f, 1.0f, true, 20.0f, 20.0f, false, party);
    CHECK(!j.ok && j.code == "no-los");
    party[0].pathComplete = false;
    j = od::JudgeDirect("Mtank", 10.0f, 1.0f, true, 20.0f, 20.0f, true, party);
    CHECK(!j.ok && j.code == "too-far" && DR::DirectStragglerName(j.text) == "Mpriest");
    od::DirectFollower b;
    b.name = "Mdps";
    b.alive = false;
    party.push_back(b);
    j = od::JudgeDirect("Mtank", 10.0f, 1.0f, true, 20.0f, 20.0f, true, party);
    CHECK(!j.ok && DR::DirectStragglerName(j.text) == "Mdps");      // the dead one is named first, as T does
    CHECK(DR::IsPathRefusal("no-ground: x") && DR::IsPathRefusal("no-los: x"));

    // targets: the invalid-target prefix the pull rules read (DungeonRunPolicy.h:8161 compare(0, 14, "invalid-target"))
    RC::TargetFacts t;
    j = od::JudgeTarget(t);
    CHECK(!j.ok && j.text.compare(0, 14, "invalid-target") == 0);
    t.found = t.inWorld = t.alive = t.attackable = t.visible = true;
    CHECK(od::JudgeTarget(t).ok);
    t.attackable = false;
    CHECK(od::JudgeTarget(t).text.compare(0, 14, "invalid-target") == 0);
    t.attackable = true;
    t.alive = false;
    CHECK(od::JudgeTarget(t).code == "dead-target");
    t.alive = true;
    t.groupMemberOrBot = true;
    CHECK(od::JudgeTarget(t).code == "invalid-target");
    t.groupMemberOrBot = false;
    t.visible = false;
    CHECK(od::JudgeTarget(t).code == "no-target");

    // ack and ProtocolSafe
    CHECK(od::AckText("party", 5) == "party 5");
    CHECK(od::RefusalText("failed", "a|b\nc") == "failed: a b c");
}

struct FakeHost
{
    std::map<int, std::pair<uint32_t, std::string>> last;   // bot -> (seq, result)
    od::SlotResult Look(int bot, uint32_t seq) const
    {
        od::SlotResult r;
        auto it = last.find(bot);
        if (it == last.end())
            return r;
        if (it->second.first > seq)
            r.state = od::SlotState::Superseded;
        else if (it->second.first == seq && it->second.second == "ok")
            r.state = od::SlotState::Ok;
        else if (it->second.first == seq && it->second.second.compare(0, 5, "fail:") == 0)
        {
            r.state = od::SlotState::Failed;
            r.result = it->second.second;
        }
        return r;
    }
};

static void TestSettle()
{
    FakeHost host;
    auto const look = [&](int bot, uint32_t seq) { return host.Look(bot, seq); };
    od::PendingOrder<int> pending;
    pending.order = "pull 5 body";
    pending.queuedMs = 1000;
    pending.slots.push_back({ 1, 10, true });
    std::string failure;
    // queued: waits
    host.last[1] = { 10, "queued" };
    CHECK(!od::Settle(pending, 2000, look, failure) && failure.empty());
    // ok: settled, no failure
    host.last[1] = { 10, "ok" };
    CHECK(od::Settle(pending, 2000, look, failure) && failure.empty());
    // fail: settled with a failure text in the refusal shape
    host.last[1] = { 10, "fail:pull" };
    CHECK(od::Settle(pending, 2000, look, failure) && failure.compare(0, 8, "failed: ") == 0 && failure.find("fail:pull") != std::string::npos);
    // never ran: expires after 20 s
    host.last[1] = { 10, "queued" };
    CHECK(!od::Settle(pending, 1000 + od::kPendingExpiryMs, look, failure));
    CHECK(od::Settle(pending, 1001 + od::kPendingExpiryMs, look, failure) && failure.find("fail:expired") != std::string::npos);
    // superseded by a later order of the same bot: says nothing
    host.last[1] = { 11, "ok" };
    CHECK(od::Settle(pending, 2000, look, failure) && failure.empty());

    // party-wide: one success is enough, a failure of the others is no failure; a helper's failure is ignored
    od::PendingOrder<int> party;
    party.order = "focus 5";
    party.queuedMs = 0;
    party.slots.push_back({ 1, 20, true });
    party.slots.push_back({ 2, 21, true });
    party.slots.push_back({ 3, 22, false });
    host.last[1] = { 20, "fail:focus" };
    host.last[2] = { 21, "queued" };
    host.last[3] = { 22, "fail:follow" };
    CHECK(!od::Settle(party, 100, look, failure));              // bot 2 is still to run
    host.last[2] = { 21, "ok" };
    CHECK(od::Settle(party, 100, look, failure) && failure.empty());
    host.last[2] = { 21, "fail:focus" };
    CHECK(od::Settle(party, 100, look, failure) && !failure.empty());   // every decisive bot failed
    host.last[3] = { 22, "ok" };
    CHECK(od::Settle(party, 100, look, failure) && !failure.empty());   // a helper's success does not rescue it
}

int main()
{
    TestParse();
    TestTexts();
    TestSettle();
    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
