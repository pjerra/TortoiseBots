// The core-free half of the class-quest spells (forever/src/host/TbClassQuestSpellsPure.h).
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_class_quest_spells.cpp -o /tmp/test_class_quest_spells && /tmp/test_class_quest_spells
// The fixtures are QuestRaw literals copied from the pinned core's quest data.
#include "forever/src/host/TbClassQuestSpellsPure.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Q = TortoiseBots::questspells;

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

static char const* kCase1 = "case 1 (level-1 orc warlock gets 688)";
static char const* kCase2 = "case 2 (688 already known stays known)";
static char const* kCase3 = "case 3 (warrior stances: below and at level 10)";
static char const* kCase4 = "case 4 (troll warlock: ClassWide falls back to all class quests)";
static char const* kCase5 = "case 5 (priest racials: race, fit, no race quest)";
static char const* kCase6 = "case 6 (hunter pet kit from both quest lines)";
static char const* kCase7 = "case 7 (CollectTaught: masks, repeatable, displays, source spell)";
static char const* kCase8 = "case 8 (shaman totem: the bot's own race decides the level)";
static char const* kCase9 = "case 9 (no quest; one jump 1 -> 30 teaches everything)";
static char const* kCase10 = "case 10 (the 39-row table is sane)";
static char const* kCase11 = "case 11 (a classless quest teaches only the class its spell belongs to)";

static Q::QuestRaw const kStolenTome{ 1598, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };
static Q::QuestRaw const kBeginnings{ 1485, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };

// 1. A new level-1 orc warlock that knows nothing is taught Summon Imp.
static void TestCase1()
{
    BeginCase(kCase1);
    CHECK(kCase1, Q::FindRow(9, 688) != nullptr);
    CHECK(kCase1, Q::CollectTaught(kBeginnings, 9) == std::vector<uint32_t>{ 688 });
    CHECK(kCase1, Q::CollectTaught(kStolenTome, 9) == std::vector<uint32_t>{ 688 });

    Q::AllowedSpell const* row = Q::FindRow(9, 688);
    if (!row)
        return;
    std::vector<Q::Candidate> const cands{ { 1485, true, 1 } };
    Q::Decision const d = Q::Decide(*row, 1, cands, /*known*/ false, /*fits*/ true);
    CHECK(kCase1, d.verdict == Q::Verdict::Teach);
    CHECK(kCase1, d.spellId == 688);
    CHECK(kCase1, d.questId == 1485);
    CHECK(kCase1, d.gate == 1);
}

// 2. The same bot already knowing 688 is left alone (idempotent).
static void TestCase2()
{
    BeginCase(kCase2);
    Q::AllowedSpell const* row = Q::FindRow(9, 688);
    if (!row)
        return;
    std::vector<Q::Candidate> const cands{ { 1485, true, 1 } };
    Q::Decision const d = Q::Decide(*row, 1, cands, /*known*/ true, /*fits*/ true);
    CHECK(kCase2, d.verdict == Q::Verdict::Known);
    CHECK(kCase2, d.gate == 1);
    CHECK(kCase2, d.questId == 1485);
}

// 3. The warrior stance quest gates all three spells at its min level.
static void TestCase3()
{
    BeginCase(kCase3);
    Q::QuestRaw const q1498{ 1498, 1, false, 10, 8121, { 71, 7386, 355 }, {}, 0, {} };
    CHECK(kCase3, Q::CollectTaught(q1498, 1) == (std::vector<uint32_t>{ 71, 7386, 355 }));

    std::vector<Q::Candidate> const cands{ { 1498, true, 10 } };
    for (uint32_t spellId : { 71u, 7386u, 355u })
    {
        Q::AllowedSpell const* row = Q::FindRow(1, spellId);
        CHECK(kCase3, row != nullptr);
        if (!row)
            continue;
        Q::Decision const below = Q::Decide(*row, 9, cands, false, true);
        CHECK(kCase3, below.verdict == Q::Verdict::BelowLevel);
        CHECK(kCase3, below.gate == 10);
        CHECK(kCase3, below.questId == 1498);
        Q::Decision const at = Q::Decide(*row, 10, cands, false, true);
        CHECK(kCase3, at.verdict == Q::Verdict::Teach);
        CHECK(kCase3, at.gate == 10);
    }
}

// 4. A troll warlock takes none of the Imp quests by race; the class-wide row still teaches 688.
static void TestCase4()
{
    BeginCase(kCase4);
    Q::QuestRaw const piercings{ 1470, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };
    Q::QuestRaw const vile{ 1485, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };
    Q::QuestRaw const stolen{ 1598, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };
    Q::QuestRaw const begin{ 1599, 256, false, 1, 7763, { 688 }, { 688 }, 0, {} };
    for (Q::QuestRaw const& q : { piercings, vile, stolen, begin })
        CHECK(kCase4, Q::CollectTaught(q, 9) == std::vector<uint32_t>{ 688 });

    Q::AllowedSpell const* row = Q::FindRow(9, 688);
    if (!row)
        return;
    std::vector<Q::Candidate> const cands{ { 1470, false, 1 }, { 1485, false, 1 }, { 1598, false, 1 }, { 1599, false, 1 } };
    Q::Decision const d = Q::Decide(*row, 1, cands, false, true);
    CHECK(kCase4, d.verdict == Q::Verdict::Teach);
    CHECK(kCase4, d.gate == 1);
    CHECK(kCase4, d.questId == 1470);   // all gates tie: the lowest quest id names it
}

// 5. The priest racials: a race-fitting quest is required, and the core's class/race mask still has the last word.
static void TestCase5()
{
    BeginCase(kCase5);
    Q::QuestRaw const q5659{ 5659, 16, false, 10, 19318, { 2652 }, {}, 0, {} };
    Q::QuestRaw const q5658{ 5658, 16, false, 10, 19318, { 2652 }, {}, 0, {} };
    CHECK(kCase5, Q::CollectTaught(q5659, 5) == std::vector<uint32_t>{ 2652 });
    CHECK(kCase5, Q::CollectTaught(q5658, 5) == std::vector<uint32_t>{ 2652 });

    Q::AllowedSpell const* row = Q::FindRow(5, 2652);
    if (!row)
        return;
    std::vector<Q::Candidate> const troll{ { 5659, true, 10 } };
    Q::Decision const unfit = Q::Decide(*row, 10, troll, false, /*fits*/ false);
    CHECK(kCase5, unfit.verdict == Q::Verdict::Unfit);
    CHECK(kCase5, unfit.gate == 10);

    std::vector<Q::Candidate> const undead{ { 5658, true, 10 } };
    Q::Decision const teach = Q::Decide(*row, 10, undead, false, /*fits*/ true);
    CHECK(kCase5, teach.verdict == Q::Verdict::Teach);
    CHECK(kCase5, teach.questId == 5658);

    std::vector<Q::Candidate> const human{ { 5658, false, 10 } };
    Q::Decision const none = Q::Decide(*row, 10, human, false, true);
    CHECK(kCase5, none.verdict == Q::Verdict::NoRaceQuest);
}

// 6. The hunter kit comes from what the quests show they give, and nothing else.
static void TestCase6()
{
    BeginCase(kCase6);
    Q::QuestRaw const q6082{ 6082, 4, false, 10, 1579, { 1515 }, { 23356, 1515, 883, 2641 }, 0, {} };
    Q::QuestRaw const q6081{ 6081, 4, false, 10, 5300, {}, { 23357, 5149, 6991, 982 }, 0, {} };
    CHECK(kCase6, Q::CollectTaught(q6082, 3) == (std::vector<uint32_t>{ 1515, 883, 2641 }));
    CHECK(kCase6, Q::CollectTaught(q6081, 3) == (std::vector<uint32_t>{ 5149, 6991, 982 }));

    std::vector<Q::Candidate> const cands{ { 6081, true, 10 }, { 6082, true, 10 } };
    for (uint32_t spellId : { 1515u, 883u, 2641u, 5149u, 6991u, 982u })
    {
        Q::AllowedSpell const* row = Q::FindRow(3, spellId);
        CHECK(kCase6, row != nullptr);
        if (!row)
            continue;
        Q::Decision const d = Q::Decide(*row, 10, cands, false, true);
        CHECK(kCase6, d.verdict == Q::Verdict::Teach);
        CHECK(kCase6, d.gate == 10);
    }
    for (uint32_t displayOnly : { 23356u, 23357u })
        CHECK(kCase6, Q::FindRow(3, displayOnly) == nullptr);
}

// 7. CollectTaught keeps only allow-listed ids of the quest's own class.
static void TestCase7()
{
    BeginCase(kCase7);
    // No class mask: the taught spell implies the class, so only the class whose list holds 688 collects it.
    Q::QuestRaw const openToAll{ 1, 0, false, 1, 7763, { 688 }, { 688 }, 0, {} };
    CHECK(kCase7, Q::CollectTaught(openToAll, 9) == std::vector<uint32_t>{ 688 });
    CHECK(kCase7, Q::CollectTaught(openToAll, 1).empty());

    Q::QuestRaw const repeatable{ 2, 256, true, 1, 7763, { 688 }, { 688 }, 0, {} };
    CHECK(kCase7, Q::CollectTaught(repeatable, 9).empty());

    Q::QuestRaw const warlockMasked{ 3, 256, false, 1, 0, { 71 }, { 71 }, 0, {} };
    CHECK(kCase7, Q::CollectTaught(warlockMasked, 9).empty());
    CHECK(kCase7, Q::CollectTaught(warlockMasked, 1).empty());

    Q::QuestRaw const divineGrace{ 94773, 16, false, 1, 1277387, {}, { 1277370 }, 0, {} };
    CHECK(kCase7, Q::CollectTaught(divineGrace, 5).empty());

    Q::QuestRaw const moonglade{ 5921, 1024, false, 10, 0, {}, {}, 19027, { 18960 } };
    CHECK(kCase7, Q::CollectTaught(moonglade, 11) == std::vector<uint32_t>{ 18960 });
}

// 8. A totem row counts the bot's own race's quest for the level, and a race with only the other quest for its own gate.
static void TestCase8()
{
    BeginCase(kCase8);
    Q::QuestRaw const callOfEarth{ 1518, 64, false, 4, 8073, { 8071 }, {}, 0, {} };
    Q::QuestRaw const skyborne{ 92468, 64, false, 3, 8073, { 8071 }, {}, 0, {} };
    CHECK(kCase8, Q::CollectTaught(callOfEarth, 7) == std::vector<uint32_t>{ 8071 });
    CHECK(kCase8, Q::CollectTaught(skyborne, 7) == std::vector<uint32_t>{ 8071 });

    Q::AllowedSpell const* row = Q::FindRow(7, 8071);
    if (!row)
        return;
    std::vector<Q::Candidate> const orc{ { 1518, true, 4 }, { 92468, false, 3 } };
    Q::Decision const below = Q::Decide(*row, 3, orc, false, true);
    CHECK(kCase8, below.verdict == Q::Verdict::BelowLevel);
    CHECK(kCase8, below.gate == 4);
    CHECK(kCase8, below.questId == 1518);
    Q::Decision const at = Q::Decide(*row, 4, orc, false, true);
    CHECK(kCase8, at.verdict == Q::Verdict::Teach);
    CHECK(kCase8, at.gate == 4);

    std::vector<Q::Candidate> const skyborneRace{ { 1518, false, 4 }, { 92468, true, 3 } };
    Q::Decision const earlier = Q::Decide(*row, 3, skyborneRace, false, true);
    CHECK(kCase8, earlier.verdict == Q::Verdict::Teach);
    CHECK(kCase8, earlier.gate == 3);
    CHECK(kCase8, earlier.questId == 92468);
}

// 9. A row with no quest is never taught, and a level jump teaches every gate it passed.
static void TestCase9()
{
    BeginCase(kCase9);
    Q::AllowedSpell const* doom = Q::FindRow(9, 18540);
    CHECK(kCase9, doom != nullptr);
    if (doom)
    {
        Q::Decision const none = Q::Decide(*doom, 60, {}, false, true);
        CHECK(kCase9, none.verdict == Q::Verdict::NoQuest);
    }

    Q::QuestRaw const q1498{ 1498, 1, false, 10, 8121, { 71, 7386, 355 }, {}, 0, {} };
    Q::QuestRaw const q1719{ 1719, 1, false, 30, 8616, { 2458, 20252 }, {}, 0, {} };
    CHECK(kCase9, Q::CollectTaught(q1719, 1) == (std::vector<uint32_t>{ 2458, 20252 }));

    struct Expect { uint32_t spellId; uint32_t questId; uint8_t gate; };
    Expect const expected[] = { { 71, 1498, 10 }, { 7386, 1498, 10 }, { 355, 1498, 10 }, { 2458, 1719, 30 }, { 20252, 1719, 30 } };
    // The index gives a row only the quests that teach it.
    std::vector<Q::QuestRaw> const quests{ q1498, q1719 };
    for (Expect const& e : expected)
    {
        Q::AllowedSpell const* row = Q::FindRow(1, e.spellId);
        CHECK(kCase9, row != nullptr);
        if (!row)
            continue;
        std::vector<Q::Candidate> cands;
        for (Q::QuestRaw const& q : quests)
            for (uint32_t taught : Q::CollectTaught(q, 1))
                if (taught == e.spellId)
                    cands.push_back({ q.questId, true, uint8_t(q.classicMinLevel) });
        Q::Decision const d = Q::Decide(*row, 30, cands, false, true);
        CHECK(kCase9, d.verdict == Q::Verdict::Teach);
        CHECK(kCase9, d.gate == e.gate);
        CHECK(kCase9, d.questId == e.questId);
    }
}

// 10. The table itself: 39 reviewed rows, nothing duplicated, nothing retail-sized.
static void TestCase10()
{
    BeginCase(kCase10);
    CHECK(kCase10, Q::kAllowedCount == 39);

    std::set<uint8_t> const classIds{ 1, 2, 3, 4, 5, 7, 8, 9, 11 };
    std::set<std::pair<uint8_t, uint32_t>> seen;
    for (Q::AllowedSpell const& row : Q::kAllowed)
    {
        CHECK(kCase10, classIds.count(row.classId) != 0);
        CHECK(kCase10, row.level >= 1 && row.level <= 60);
        CHECK(kCase10, row.spellId < 100000);
        CHECK(kCase10, seen.insert(std::make_pair(row.classId, row.spellId)).second);
        CHECK(kCase10, Q::FindRow(row.classId, row.spellId) == &row);
        CHECK(kCase10, row.name != nullptr && row.name[0] != '\0');
    }
    for (uint8_t classId : { 1, 2, 3, 4, 5, 7, 9, 11 })
        CHECK(kCase10, !Q::RowsForClass(static_cast<uint8_t>(classId)).empty());
    CHECK(kCase10, Q::RowsForClass(8).empty());   // the mage has no row
    CHECK(kCase10, Q::VerdictName(Q::Verdict::Teach) == std::string("teach"));
    CHECK(kCase10, Q::VerdictName(Q::Verdict::NoRaceQuest) == std::string("no-race-quest"));
}

// 11. A classless quest teaches only the class whose allow-list holds the spell it gives, and never a repeatable one.
static void TestCase11()
{
    BeginCase(kCase11);
    // 9691 "Return to Silvermoon" teaches Sense Undead (5502, paladin only) and carries no class mask.
    Q::QuestRaw const senseUndead{ 9691, 0, false, 0, 5503, { 5502 }, {}, 0, {} };
    CHECK(kCase11, Q::CollectTaught(senseUndead, 2) == std::vector<uint32_t>{ 5502 });
    CHECK(kCase11, Q::CollectTaught(senseUndead, 5).empty());
    CHECK(kCase11, Q::CollectTaught(senseUndead, 9).empty());

    Q::QuestRaw const repeatable{ 9692, 0, true, 0, 5503, { 5502 }, {}, 0, {} };
    CHECK(kCase11, Q::CollectTaught(repeatable, 2).empty());

    Q::QuestRaw const unrelated{ 9693, 0, false, 0, 0, { 133 }, {}, 0, {} };
    CHECK(kCase11, Q::CollectTaught(unrelated, 2).empty());
    CHECK(kCase11, Q::CollectTaught(unrelated, 8).empty());

    Q::AllowedSpell const* row = Q::FindRow(2, 5502);
    CHECK(kCase11, row != nullptr);
    if (row)
    {
        std::vector<Q::Candidate> const cands{ { 9691, true, 20 } };
        Q::Decision const d = Q::Decide(*row, 20, cands, false, true);
        CHECK(kCase11, d.verdict == Q::Verdict::Teach);
        CHECK(kCase11, d.gate == 20);
        CHECK(kCase11, d.questId == 9691);
    }
}

int main()
{
    TestCase1();
    TestCase2();
    TestCase3();
    TestCase4();
    TestCase5();
    TestCase6();
    TestCase7();
    TestCase8();
    TestCase9();
    TestCase10();
    TestCase11();

    bool failed = false;
    for (auto const& entry : g_cases)
    {
        std::printf("%s: %s\n", entry.first.c_str(), entry.second ? "PASS" : "FAIL");
        failed = failed || !entry.second;
    }
    std::printf("%d checks\n", g_checks);
    if (failed)
    {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
