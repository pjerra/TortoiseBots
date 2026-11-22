// Standalone test of piece D of #136 (loot methods): the method table both the roll gate and the run gate read
// (forever/src/host/TbRollMethodsPure.h) and the two gates that call it (forever/src/host/run/TbRunTasksPure.h).
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_roll_methods_pure.cpp -o /tmp/test_roll_methods && /tmp/test_roll_methods
#include "forever/src/host/TbRollMethodsPure.h"
#include "forever/src/host/run/TbRunTasksPure.h"

#include <cstdio>
#include <vector>

namespace rm = TortoiseBots::rollmethods;
namespace tk = TortoiseBots::run::tasks;
namespace RC = TortoiseBots::RemoteCommander;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static tk::TaskOrder order(char const* method, char const* threshold = nullptr)
{
    tk::TaskOrder out;
    std::string code, message;
    if (threshold)
    {
        std::vector<std::string> args = { method, threshold };
        CHECK(tk::ParseTaskOrder("lootmethod", args, out, code, message));
    }
    else
    {
        std::vector<std::string> args = { method };
        CHECK(tk::ParseTaskOrder("lootmethod", args, out, code, message));
    }
    return out;
}

static void TestTable()
{
    // TB (LootMethodTakesRolls): rolls exist under group loot and need-before-greed only.
    CHECK(rm::MethodTakesRolls(RC::kLootGroup));
    CHECK(rm::MethodTakesRolls(RC::kLootNeedBeforeGreed));
    CHECK(!rm::MethodTakesRolls(RC::kLootFfa));
    CHECK(!rm::MethodTakesRolls(RC::kLootRoundRobin));
    CHECK(!rm::MethodTakesRolls(RC::kLootMaster));
    CHECK(!rm::MethodTakesRolls(-1));           // no group / personal loot: silent, never a guess
    CHECK(!rm::MethodTakesRolls(5));
    CHECK(!rm::MethodTakesRolls(200));
    // The run goes on under the same two methods.
    CHECK(rm::RunMayUseMethod(RC::kLootGroup));
    CHECK(rm::RunMayUseMethod(RC::kLootNeedBeforeGreed));
    CHECK(!rm::RunMayUseMethod(RC::kLootFfa));
    CHECK(!rm::RunMayUseMethod(RC::kLootRoundRobin));
    CHECK(!rm::RunMayUseMethod(RC::kLootMaster));
    CHECK(!rm::RunMayUseMethod(-1));
}

static void TestJudge()
{
    // Every loot method is carried out now (piece D): master and nbg are no longer refused.
    CHECK(tk::JudgeLootMethod(order("ffa")).ok);
    CHECK(tk::JudgeLootMethod(order("group")).ok);
    CHECK(tk::JudgeLootMethod(order("rr")).ok);
    CHECK(tk::JudgeLootMethod(order("master")).ok);
    CHECK(tk::JudgeLootMethod(order("nbg")).ok);
    CHECK(tk::JudgeLootMethod(order("group", "epic")).ok);
    // The threshold rule is unchanged: the core refuses below uncommon.
    tk::MethodVerdict v = tk::JudgeLootMethod(order("group", "common"));
    CHECK(!v.ok && v.code == "invalid");
}

static void TestRunGate()
{
    std::string why;
    CHECK(tk::RunLootMethodOk(RC::kLootGroup, why));
    CHECK(tk::RunLootMethodOk(RC::kLootNeedBeforeGreed, why));
    CHECK(!tk::RunLootMethodOk(RC::kLootFfa, why) && why.find("ffa") != std::string::npos);
    CHECK(!tk::RunLootMethodOk(RC::kLootRoundRobin, why) && why.find("rr") != std::string::npos);
    CHECK(!tk::RunLootMethodOk(RC::kLootMaster, why) && why.find("master") != std::string::npos);
    CHECK(!tk::RunLootMethodOk(-1, why) && why.find("personal") != std::string::npos);
    // The core's methods reach the gate through the policy mapping, unchanged.
    CHECK(tk::RunLootMethodOk(tk::PolicyLootMethod(3), why));
    CHECK(tk::RunLootMethodOk(tk::PolicyLootMethod(4), why));
    CHECK(!tk::RunLootMethodOk(tk::PolicyLootMethod(2), why));
    CHECK(!tk::RunLootMethodOk(tk::PolicyLootMethod(5), why));
}

int main()
{
    TestTable();
    TestJudge();
    TestRunGate();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
