// Standalone pure unit tests for forever/src/host/TbAutoTrainPure.h.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_autotrain.cpp -o /tmp/test_autotrain && /tmp/test_autotrain
#include "forever/src/host/TbAutoTrainPure.h"

#include <cstdio>
#include <map>
#include <string>

namespace A = tbhost::autotrain;

static int g_checks = 0;
static std::map<std::string, bool> g_cases;

static void BeginCase(char const* name)
{
    g_cases.emplace(name, true);
}

static void Check(bool ok, char const* caseName, char const* what, int line)
{
    ++g_checks;
    if (ok)
        return;
    g_cases[caseName] = false;
    std::printf("FAIL %s: %s (line %d)\n", caseName, what, line);
}

#define CHECK(caseName, cond) Check((cond), (caseName), #cond, __LINE__)

static void TestRetailChainlessRule()
{
    char const* const c = "case 1 (retail-era id without rank chain is skipped)";
    BeginCase(c);
    // Classic-era id (< 100000) without chain: allowed (e.g. starter spells).
    CHECK(c, !A::ShouldSkipRetailChainless(585, 0));     // Smite Rank 1 (classic id)
    CHECK(c, !A::ShouldSkipRetailChainless(99999, 0));
    // Retail-era id (>= 100000) without chain: skipped.
    CHECK(c, A::ShouldSkipRetailChainless(100000, 0));
    CHECK(c, A::ShouldSkipRetailChainless(129250, 0));   // e.g. Mind Spike / retail spell
    // Retail-era id WITH a verified rank chain (rank > 0): allowed.
    CHECK(c, !A::ShouldSkipRetailChainless(129250, 1));
    CHECK(c, !A::ShouldSkipRetailChainless(150000, 3));
}

static void TestPostTrainRemoval()
{
    char const* const c = "case 2 (post-train removal preserves pre-existing spells)";
    BeginCase(c);
    // Pre-existing spell (e.g. from questspells or talents) is NEVER removed, even if retail-era chainless.
    CHECK(c, !A::ShouldRemovePostTrain(1309050, 0, /*knownBefore*/ true));
    // Newly learned classic-era spell is kept.
    CHECK(c, !A::ShouldRemovePostTrain(2050, 0, /*knownBefore*/ false));
    // Newly learned retail-era spell WITH rank chain is kept.
    CHECK(c, !A::ShouldRemovePostTrain(130000, 2, /*knownBefore*/ false));
    // Newly learned retail-era spell WITHOUT rank chain is removed.
    CHECK(c, A::ShouldRemovePostTrain(130000, 0, /*knownBefore*/ false));
}

static void TestClassLineSpellEligibility()
{
    char const* const c = "case 3 (class-line spell eligibility)";
    BeginCase(c);
    // Happy path: classic spell, level reached, matching family, not known.
    CHECK(c, A::ShouldTeachClassLineSpell(10, 12, 6, 6, false, 585, 1));
    // Already known: skip.
    CHECK(c, !A::ShouldTeachClassLineSpell(10, 12, 6, 6, true, 585, 1));
    // Level 0 (non-trainable/passive marker) or above bot level: skip.
    CHECK(c, !A::ShouldTeachClassLineSpell(0, 12, 6, 6, false, 585, 1));
    CHECK(c, !A::ShouldTeachClassLineSpell(14, 12, 6, 6, false, 585, 1));
    // Wrong spell family: skip.
    CHECK(c, !A::ShouldTeachClassLineSpell(10, 12, 4, 6, false, 585, 1));
    // Retail-era chainless: skip.
    CHECK(c, !A::ShouldTeachClassLineSpell(10, 12, 6, 6, false, 120000, 0));
    // Retail-era with chain: allow.
    CHECK(c, A::ShouldTeachClassLineSpell(10, 12, 6, 6, false, 120000, 1));
}

static void TestAutoTrainEventGate()
{
    char const* const c = "case 4 (auto-train event gate)";
    BeginCase(c);
    CHECK(c, A::ShouldAutoTrainOnEvent(true, true));
    CHECK(c, !A::ShouldAutoTrainOnEvent(false, true));
    CHECK(c, !A::ShouldAutoTrainOnEvent(true, false));
    CHECK(c, !A::ShouldAutoTrainOnEvent(false, false));
}

static void TestNetTrainerTaught()
{
    char const* const c = "case 5 (net trainer taught excludes post-train removals)";
    BeginCase(c);
    CHECK(c, A::NetTrainerTaught(1, 1) == 0);
    CHECK(c, A::NetTrainerTaught(3, 1) == 2);
    CHECK(c, A::NetTrainerTaught(0, 1) == 0);
}

int main()
{
    TestRetailChainlessRule();
    TestPostTrainRemoval();
    TestClassLineSpellEligibility();
    TestAutoTrainEventGate();
    TestNetTrainerTaught();

    int failed = 0;
    for (auto const& [name, ok] : g_cases)
    {
        std::printf("%s: %s\n", name.c_str(), ok ? "PASS" : "FAIL");
        if (!ok)
            ++failed;
    }
    std::printf("%d checks, %d failed\n", g_checks, failed);
    return failed ? 1 : 0;
}
